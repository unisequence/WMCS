#!/usr/bin/env bash
set -euo pipefail

# Run the normal ownership-safe WMCS lifecycle repeatedly on the isolated
# two-router lab pair. The script intentionally prints only sanitized state;
# pairing SAS values and WLAN credentials never reach stdout.

CONTROLLER="${WMCS_CONTROLLER:-192.168.1.1}"
AGENT="${WMCS_AGENT:-192.168.1.2}"
CYCLES="${WMCS_SOAK_CYCLES:-10}"

CONTROLLER_OWNER="e10745f50aede398cd0995287fadfe85"
CONTROLLER_PEER="7cab6f2d383f4f4bd1e5f6301a8df160"
AGENT_PEER="e10745f50aede398cd0995287fadfe85"
CONTROLLER_HASH="b7e0a6fa6ad75cd6dfb91560671b590fe45d152a06774f1fb78a4f170b5a6b36"
AGENT_SYNC_HASH="dcbc165ea344867f70ea550a0a5e1b62a750deca23c8cebb75269e3c1c7fb728"
AGENT_RELEASE_HASH="82718b7c06a628b05b830fbff1867213812798194d961a6d0969d36afef54c30"

SSH_OPTS=(
    -o BatchMode=yes
    -o ConnectTimeout=5
    -o LogLevel=ERROR
    -o StrictHostKeyChecking=accept-new
)

LAST_SEQUENCE=""
LAST_GENERATION=""

fail() {
    echo "SOAK status=FAIL reason=$*" >&2
    exit 1
}

remote() {
    local host="$1"
    shift
    ssh "${SSH_OPTS[@]}" "root@${host}" "$@"
}

call() {
    local host="$1"
    local method="$2"
    local payload="${3-}"
    if [[ -n "$payload" ]]; then
        remote "$host" "ubus -S call wmcs ${method} '${payload}'"
    else
        remote "$host" "ubus -S call wmcs ${method}"
    fi
}

hash_remote() {
    local host="$1"
    remote "$host" "uci export wireless | sha256sum" | awk '{print $1}'
}

peer_id_remote() {
    local host="$1"
    local role="$2"
    call "$host" peers | jq -r --arg role "$role" \
        '.peers[] | select(.role == $role and .state == "active") | .peer_id' | head -n1
}

stop_windows() {
    call "$CONTROLLER" wlan_sync_stop >/dev/null 2>&1 || true
    call "$AGENT" wlan_sync_stop >/dev/null 2>&1 || true
    call "$CONTROLLER" pairing_stop >/dev/null 2>&1 || true
    call "$AGENT" pairing_stop >/dev/null 2>&1 || true
    call "$CONTROLLER" discovery_stop >/dev/null 2>&1 || true
    call "$AGENT" discovery_stop >/dev/null 2>&1 || true
}

fresh_candidate() {
    call "$CONTROLLER" discovery_start '{"duration_seconds":30}' \
        | jq -e '.active == true' >/dev/null
    call "$AGENT" discovery_start '{"duration_seconds":30}' \
        | jq -e '.active == true' >/dev/null
    local nodes candidate=""
    for _ in $(seq 1 6); do
        nodes="$(call "$CONTROLLER" nodes)"
        candidate="$(jq -r '.nodes[] | select(.role == "agent") | .id' <<<"$nodes" | head -n1)"
        if [[ -n "$candidate" && "$candidate" != "null" ]]; then
            break
        fi
        sleep 1
    done
    [[ -n "$candidate" && "$candidate" != "null" ]] || fail "candidate_missing"

    call "$CONTROLLER" discovery_stop >/dev/null
    call "$AGENT" discovery_stop >/dev/null
    printf '%s\n' "$candidate"
}

wait_outcome() {
    local host="$1"
    local method="$2"
    local expected="$3"
    local raw state outcome

    for _ in $(seq 1 20); do
        raw="$(call "$host" "$method")"
        state="$(jq -r '.state // empty' <<<"$raw")"
        outcome="$(jq -r '.outcome // empty' <<<"$raw")"

        if [[ "$state" == "complete" && "$outcome" == "$expected" ]]; then
            LAST_SEQUENCE="$(jq -r '.sequence // empty' <<<"$raw")"
            LAST_GENERATION="$(jq -r '.generation // empty' <<<"$raw")"
            return 0
        fi

        if [[ "$state" == "complete" && -n "$outcome" && "$outcome" != "$expected" ]]; then
            fail "${method}_${host}_outcome_${outcome}"
        fi
        sleep 1
    done
    fail "${method}_${host}_timeout"
}

preflight() {
    local controller_hash agent_hash owner controller_status agent_status
    controller_hash="$(hash_remote "$CONTROLLER")"
    agent_hash="$(hash_remote "$AGENT")"
    [[ "$controller_hash" == "$CONTROLLER_HASH" ]] || fail "controller_hash_preflight"
    [[ "$agent_hash" == "$AGENT_SYNC_HASH" ]] || fail "agent_hash_preflight"

    owner="$(remote "$AGENT" "uci -q get wireless.wmcs_home_5g.wmcs_owner")"
    [[ "$owner" == "$CONTROLLER_OWNER" ]] || fail "owner_preflight"
    [[ "$(remote "$AGENT" "uci -q get wireless.wmcs_home_5g.device")" == "radio1" ]] || fail "radio_preflight"

    controller_status="$(call "$CONTROLLER" status)"
    agent_status="$(call "$AGENT" status)"
    jq -e '
        .mutation_enabled == true and .identity_ready == true and
        .paired_peer_count == 1 and .discovery_active == false and
        .pairing_active == false and .control_active == false and
        .control_reconciliation_pending == false
    ' <<<"$controller_status" >/dev/null || fail "controller_status_preflight"
    jq -e '
        .mutation_enabled == true and .mutation_available == true and
        .identity_ready == true and .paired_peer_count == 1 and
        .discovery_active == false and .pairing_active == false and
        .control_active == false and .control_reconciliation_pending == false
    ' <<<"$agent_status" >/dev/null || fail "agent_status_preflight"

    [[ "$(peer_id_remote "$CONTROLLER" agent)" == "$CONTROLLER_PEER" ]] || fail "controller_peer_preflight"
    [[ "$(peer_id_remote "$AGENT" controller)" == "$AGENT_PEER" ]] || fail "agent_peer_preflight"
}

release_cycle() {
    local candidate before after cseq aseq
    local payload

    candidate="$(fresh_candidate)"
    before="$(hash_remote "$AGENT")"
    [[ "$before" == "$AGENT_SYNC_HASH" ]] || fail "release_dry_hash_before"

    call "$AGENT" control_listen '{"duration_seconds":60}' >/dev/null
    payload="{\"candidate_id\":\"${candidate}\",\"peer_id\":\"${CONTROLLER_PEER}\",\"dry_run\":true,\"duration_seconds\":30}"
    call "$CONTROLLER" release_start "$payload" >/dev/null
    wait_outcome "$CONTROLLER" release_status dry_run_ready
    cseq="$LAST_SEQUENCE"
    wait_outcome "$AGENT" wlan_sync_status dry_run_ready
    aseq="$LAST_SEQUENCE"
    [[ "$cseq" == "$aseq" ]] || fail "release_dry_sequence_mismatch"
    after="$(hash_remote "$AGENT")"
    [[ "$after" == "$before" ]] || fail "release_dry_mutated_wireless"
    stop_windows

    candidate="$(fresh_candidate)"
    call "$AGENT" control_listen '{"duration_seconds":60}' >/dev/null
    payload="{\"candidate_id\":\"${candidate}\",\"peer_id\":\"${CONTROLLER_PEER}\",\"dry_run\":false,\"duration_seconds\":30}"
    call "$CONTROLLER" release_start "$payload" >/dev/null
    wait_outcome "$CONTROLLER" release_status committed
    cseq="$LAST_SEQUENCE"
    wait_outcome "$AGENT" wlan_sync_status committed
    aseq="$LAST_SEQUENCE"
    [[ "$cseq" == "$aseq" ]] || fail "release_sequence_mismatch"
    stop_windows

    [[ "$(hash_remote "$CONTROLLER")" == "$CONTROLLER_HASH" ]] || fail "release_changed_controller"
    [[ "$(hash_remote "$AGENT")" == "$AGENT_RELEASE_HASH" ]] || fail "release_agent_hash"
    [[ -z "$(remote "$AGENT" "uci -q get wireless.wmcs_home_5g.wmcs_managed || true")" ]] || fail "release_section_remains"
    printf 'release_seq=%s ' "$cseq"
}

forget_cycle() {
    local result
    stop_windows

    result="$(call "$CONTROLLER" forget_orphan "{\"peer_id\":\"${CONTROLLER_PEER}\"}")"
    jq -e '.forgotten == true' <<<"$result" >/dev/null || fail "controller_forget"
    result="$(call "$AGENT" forget_orphan "{\"peer_id\":\"${AGENT_PEER}\"}")"
    jq -e '.forgotten == true' <<<"$result" >/dev/null || fail "agent_forget"

    jq -e '.paired_peer_count == 0' <<<"$(call "$CONTROLLER" status)" >/dev/null || fail "controller_peers_remain"
    jq -e '.paired_peer_count == 0' <<<"$(call "$AGENT" status)" >/dev/null || fail "agent_peers_remain"
    [[ "$(hash_remote "$AGENT")" == "$AGENT_RELEASE_HASH" ]] || fail "forget_mutated_wireless"
    printf 'forget=ok '
}

pair_cycle() {
    local candidate controller_pairing agent_pairing sas_controller sas_agent
    local result controller_peer agent_peer

    candidate="$(fresh_candidate)"
    call "$AGENT" pairing_start '{"duration_seconds":120}' >/dev/null
    call "$CONTROLLER" pairing_start \
        "{\"duration_seconds\":120,\"candidate_id\":\"${candidate}\"}" >/dev/null

    for _ in $(seq 1 20); do
        controller_pairing="$(call "$CONTROLLER" pairing_status)"
        agent_pairing="$(call "$AGENT" pairing_status)"
        if [[ "$(jq -r '.state // empty' <<<"$controller_pairing")" == "awaiting_confirmation" &&
              "$(jq -r '.state // empty' <<<"$agent_pairing")" == "awaiting_confirmation" ]]; then
            break
        fi
        sleep 1
    done

    [[ "$(jq -r '.state // empty' <<<"$controller_pairing")" == "awaiting_confirmation" ]] || fail "controller_pairing_timeout"
    [[ "$(jq -r '.state // empty' <<<"$agent_pairing")" == "awaiting_confirmation" ]] || fail "agent_pairing_timeout"

    sas_controller="$(jq -r '.sas // empty' <<<"$controller_pairing")"
    sas_agent="$(jq -r '.sas // empty' <<<"$agent_pairing")"
    [[ -n "$sas_controller" && "$sas_controller" == "$sas_agent" ]] || fail "pairing_sas_mismatch"

    result="$(call "$AGENT" pairing_confirm "{\"sas\":\"${sas_agent}\"}")"
    jq -e '.paired == true' <<<"$result" >/dev/null || fail "agent_pairing_confirm"
    result="$(call "$CONTROLLER" pairing_confirm "{\"sas\":\"${sas_controller}\"}")"
    jq -e '.paired == true' <<<"$result" >/dev/null || fail "controller_pairing_confirm"
    stop_windows

    controller_peer="$(peer_id_remote "$CONTROLLER" agent)"
    agent_peer="$(peer_id_remote "$AGENT" controller)"
    [[ "$controller_peer" == "$CONTROLLER_PEER" ]] || fail "controller_peer_after_pair"
    [[ "$agent_peer" == "$AGENT_PEER" ]] || fail "agent_peer_after_pair"
    printf 'pair=sas-match '
}

sync_cycle() {
    local candidate before after cseq aseq payload

    candidate="$(fresh_candidate)"
    before="$(hash_remote "$AGENT")"
    [[ "$before" == "$AGENT_RELEASE_HASH" ]] || fail "sync_dry_hash_before"

    call "$AGENT" control_listen '{"duration_seconds":60}' >/dev/null
    payload="{\"candidate_id\":\"${candidate}\",\"peer_id\":\"${CONTROLLER_PEER}\",\"dry_run\":true,\"duration_seconds\":30}"
    call "$CONTROLLER" wlan_sync_start "$payload" >/dev/null
    wait_outcome "$CONTROLLER" wlan_sync_status dry_run_ready
    cseq="$LAST_SEQUENCE"
    wait_outcome "$AGENT" wlan_sync_status dry_run_ready
    aseq="$LAST_SEQUENCE"
    [[ "$cseq" == "$aseq" ]] || fail "sync_dry_sequence_mismatch"
    after="$(hash_remote "$AGENT")"
    [[ "$after" == "$before" ]] || fail "sync_dry_mutated_wireless"
    stop_windows

    candidate="$(fresh_candidate)"
    call "$AGENT" control_listen '{"duration_seconds":60}' >/dev/null
    payload="{\"candidate_id\":\"${candidate}\",\"peer_id\":\"${CONTROLLER_PEER}\",\"dry_run\":false,\"duration_seconds\":30}"
    call "$CONTROLLER" wlan_sync_start "$payload" >/dev/null
    wait_outcome "$CONTROLLER" wlan_sync_status committed
    cseq="$LAST_SEQUENCE"
    wait_outcome "$AGENT" wlan_sync_status committed
    aseq="$LAST_SEQUENCE"
    [[ "$cseq" == "$aseq" ]] || fail "sync_sequence_mismatch"
    stop_windows

    [[ "$(hash_remote "$CONTROLLER")" == "$CONTROLLER_HASH" ]] || fail "sync_changed_controller"
    [[ "$(hash_remote "$AGENT")" == "$AGENT_SYNC_HASH" ]] || fail "sync_agent_hash"
    [[ "$(remote "$AGENT" "uci -q get wireless.wmcs_home_5g.wmcs_managed")" == "1" ]] || fail "sync_managed_marker"
    [[ "$(remote "$AGENT" "uci -q get wireless.wmcs_home_5g.wmcs_owner")" == "$CONTROLLER_OWNER" ]] || fail "sync_owner"
    [[ "$(remote "$AGENT" "uci -q get wireless.wmcs_home_5g.device")" == "radio1" ]] || fail "sync_radio"
    printf 'sync_seq=%s ' "$cseq"
}

verify_final() {
    local label="$1"
    local controller_status agent_status controller_peers agent_peers
    controller_status="$(call "$CONTROLLER" status)"
    agent_status="$(call "$AGENT" status)"
    controller_peers="$(call "$CONTROLLER" peers)"
    agent_peers="$(call "$AGENT" peers)"

    jq -e '
        .paired_peer_count == 1 and .discovery_active == false and
        .pairing_active == false and .control_active == false and
        .control_reconciliation_pending == false
    ' <<<"$controller_status" >/dev/null || fail "${label}_controller_status"
    jq -e '
        .paired_peer_count == 1 and .active_controller_count == 1 and
        .ubus_connected == true and .degraded == false and
        .discovery_active == false and .pairing_active == false and
        .control_active == false and .control_reconciliation_pending == false
    ' <<<"$agent_status" >/dev/null || fail "${label}_agent_status"
    jq -e '.peers | length == 1 and .[0].state == "active"' <<<"$controller_peers" >/dev/null || fail "${label}_controller_peer"
    jq -e '.peers | length == 1 and .[0].state == "active"' <<<"$agent_peers" >/dev/null || fail "${label}_agent_peer"
    [[ "$(hash_remote "$CONTROLLER")" == "$CONTROLLER_HASH" ]] || fail "${label}_controller_hash"
    [[ "$(hash_remote "$AGENT")" == "$AGENT_SYNC_HASH" ]] || fail "${label}_agent_hash"
}

main() {
    [[ "$CYCLES" =~ ^[1-9][0-9]*$ ]] || fail "invalid_cycle_count"
    trap stop_windows EXIT
    echo "SOAK start cycles=${CYCLES} controller=${CONTROLLER} agent=${AGENT}"
    preflight

    local cycle
    for cycle in $(seq 1 "$CYCLES"); do
        printf 'cycle=%s ' "$cycle"
        release_cycle
        forget_cycle
        pair_cycle
        sync_cycle
        verify_final "cycle_${cycle}"
        echo 'status=pass'
    done

    remote "$AGENT" "/etc/init.d/wmcsd restart" >/dev/null
    sleep 5
    verify_final post_restart
    echo 'SOAK status=PASS final=paired-active-managed-ap'
}

main "$@"
