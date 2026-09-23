#!/bin/sh

set -eu

mode=${1:-}
[ "$#" -eq 0 ] || shift
controller=${1:-root@192.168.1.1}
agent=${2:-root@192.168.1.2}
controller_ap=${WMCS_CONTROLLER_AP:-hostapd.phy1-ap0}
agent_ap=${WMCS_AGENT_AP:-hostapd.phy1-ap0}
ssh_options='-o BatchMode=yes -o ConnectTimeout=5 -o LogLevel=ERROR'
done=false
controller_touched=false
agent_touched=false

usage() {
	echo "Usage: $0 enable|disable [CONTROLLER_SSH AGENT_SSH]" >&2
	exit 2
}

[ "$mode" = enable ] || [ "$mode" = disable ] || usage
command -v python3 >/dev/null 2>&1 || {
	echo "python3 is required on the workstation" >&2
	exit 1
}
for target in "$controller" "$agent"; do
	case "$target" in
		-*|*[!A-Za-z0-9._@:-]*|'')
			echo "Invalid SSH target: $target" >&2
			exit 2
			;;
	esac
done
for object in "$controller_ap" "$agent_ap"; do
	case "$object" in
		*[!A-Za-z0-9._-]*|'')
			echo "Invalid hostapd ubus object: $object" >&2
			exit 2
			;;
	esac
done

uci_digest() {
	ssh $ssh_options "$1" sh -s <<'REMOTE'
set -e
for package in network wireless firewall dhcp; do
	uci -q export "$package"
done | sha256sum | cut -d' ' -f1
REMOTE
}

method_preflight() {
	ssh $ssh_options "$1" sh -s -- "$2" <<'REMOTE'
set -e
object=$1
methods=$(ubus -v list "$object")
for method in bss_mgmt_enable rrm_nr_get_own rrm_nr_list rrm_nr_set \
	bss_transition_request; do
	printf '%s\n' "$methods" | grep -q "\"$method\""
done
REMOTE
}

remote_read() {
	ssh $ssh_options "$1" sh -s -- "$2" "$3" <<'REMOTE'
set -e
ubus -S call "$1" "$2"
REMOTE
}

remote_call() {
	target=$1
	object=$2
	method=$3
	payload=$4
	printf '%s\n' "$payload" | ssh $ssh_options "$target" \
		"IFS= read -r payload; ubus -S call '$object' '$method' \"\$payload\" >/dev/null"
}

enable_node() {
	remote_call "$1" "$2" bss_mgmt_enable \
		'{"neighbor_report":true,"beacon_report":true,"link_measurement":true,"bss_transition":true}'
}

disable_node() {
	remote_call "$1" "$2" rrm_nr_set '{"list":[]}'
	remote_call "$1" "$2" bss_mgmt_enable \
		'{"neighbor_report":false,"beacon_report":false,"link_measurement":false,"bss_transition":false}'
}

neighbor_payload() {
	python3 -c '
import json
import sys

record = json.load(sys.stdin).get("value")
if not isinstance(record, list) or len(record) != 3:
    raise SystemExit("invalid Neighbor Report record")
if not all(isinstance(value, str) and value for value in record):
    raise SystemExit("invalid Neighbor Report field")
print(json.dumps({"list": [record]}, separators=(",", ":")))
'
}

neighbor_count() {
	python3 -c '
import json
import sys

records = json.load(sys.stdin).get("list")
if not isinstance(records, list):
    raise SystemExit("invalid Neighbor Report list")
if not all(isinstance(record, list) and len(record) == 3 for record in records):
    raise SystemExit("invalid Neighbor Report record")
print(len(records))
'
}

cleanup() {
	status=$?
	trap - EXIT
	if [ "$mode" = enable ] && [ "$done" != true ]; then
		set +e
		[ "$controller_touched" != true ] || \
			disable_node "$controller" "$controller_ap"
		[ "$agent_touched" != true ] || disable_node "$agent" "$agent_ap"
		set -e
	fi
	exit "$status"
}
trap cleanup EXIT
trap 'exit 130' HUP INT TERM

method_preflight "$controller" "$controller_ap"
method_preflight "$agent" "$agent_ap"
controller_before=$(uci_digest "$controller")
agent_before=$(uci_digest "$agent")

if [ "$mode" = disable ]; then
	disable_node "$controller" "$controller_ap"
	disable_node "$agent" "$agent_ap"
	controller_count=$(remote_read "$controller" "$controller_ap" rrm_nr_list |
		neighbor_count)
	agent_count=$(remote_read "$agent" "$agent_ap" rrm_nr_list |
		neighbor_count)
else
	enable_node "$controller" "$controller_ap"
	controller_touched=true
	enable_node "$agent" "$agent_ap"
	agent_touched=true

	controller_own=$(remote_read "$controller" "$controller_ap" rrm_nr_get_own)
	agent_own=$(remote_read "$agent" "$agent_ap" rrm_nr_get_own)
	controller_payload=$(printf '%s' "$agent_own" | neighbor_payload)
	agent_payload=$(printf '%s' "$controller_own" | neighbor_payload)
	remote_call "$controller" "$controller_ap" rrm_nr_set "$controller_payload"
	remote_call "$agent" "$agent_ap" rrm_nr_set "$agent_payload"

	controller_count=$(remote_read "$controller" "$controller_ap" rrm_nr_list |
		neighbor_count)
	agent_count=$(remote_read "$agent" "$agent_ap" rrm_nr_list |
		neighbor_count)
	[ "$controller_count" -eq 1 ] && [ "$agent_count" -eq 1 ] || {
		echo "Expected exactly one remote Neighbor Report per AP" >&2
		exit 1
	}
fi

controller_after=$(uci_digest "$controller")
agent_after=$(uci_digest "$agent")
[ "$controller_before" = "$controller_after" ] || {
	echo "Controller UCI changed during runtime operation" >&2
	exit 1
}
[ "$agent_before" = "$agent_after" ] || {
	echo "Agent UCI changed during runtime operation" >&2
	exit 1
}

done=true
printf 'mode=%s\n' "$mode"
printf 'controller_neighbor_entries=%s\n' "$controller_count"
printf 'agent_neighbor_entries=%s\n' "$agent_count"
printf 'uci_unchanged=true\n'
