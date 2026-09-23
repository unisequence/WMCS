#!/bin/sh

set -eu

source_ssh=${1:-}
source_ap=${2:-hostapd.phy1-ap0}
threshold_dbm=${3:--68}
confirmations=${4:-5}
timeout_seconds=${5:-120}
dry_run=${WMCS_BTM_DRY_RUN:-0}
ssh_options='-o BatchMode=yes -o ConnectTimeout=5 -o LogLevel=ERROR'

usage() {
	echo "Usage: $0 SOURCE_SSH [SOURCE_AP [THRESHOLD_DBM [CONFIRMATIONS [TIMEOUT_SECONDS]]]]" >&2
	exit 2
}

[ -n "$source_ssh" ] || usage
case "$source_ssh" in
	-*|*[!A-Za-z0-9._@:-]*)
		echo "Invalid SSH target: $source_ssh" >&2
		exit 2
		;;
esac
case "$source_ap" in
	hostapd.*) ;;
	*)
		echo "Invalid hostapd ubus object: $source_ap" >&2
		exit 2
		;;
esac
case "$source_ap" in
	*[!A-Za-z0-9._-]*)
		echo "Invalid hostapd ubus object: $source_ap" >&2
		exit 2
		;;
esac

case "$threshold_dbm" in
	-*) threshold_magnitude=${threshold_dbm#-} ;;
	*) usage ;;
esac
case "$threshold_magnitude" in
	''|*[!0-9]*) usage ;;
esac
if [ "$threshold_dbm" -lt -95 ] || [ "$threshold_dbm" -gt -50 ]; then
	echo "Threshold must be between -95 and -50 dBm" >&2
	exit 2
fi
case "$confirmations" in
	''|*[!0-9]*) usage ;;
esac
if [ "$confirmations" -lt 2 ] || [ "$confirmations" -gt 10 ]; then
	echo "Confirmations must be between 2 and 10" >&2
	exit 2
fi
case "$timeout_seconds" in
	''|*[!0-9]*) usage ;;
esac
if [ "$timeout_seconds" -lt 10 ] || [ "$timeout_seconds" -gt 600 ]; then
	echo "Timeout must be between 10 and 600 seconds" >&2
	exit 2
fi
case "$dry_run" in
	0|1) ;;
	*)
		echo "WMCS_BTM_DRY_RUN must be 0 or 1" >&2
		exit 2
		;;
esac

# The station address and candidate BSSID remain inside this SSH process.  The
# only emitted fields are aggregate counts, signal, counters and a redacted
# response result.
ssh $ssh_options "$source_ssh" sh -s -- \
	"$source_ap" "$threshold_dbm" "$confirmations" "$timeout_seconds" \
	"$dry_run" <<'REMOTE'
# OpenWrt jshn intentionally probes unset shell variables internally, so the
# router-side process cannot use `set -u`.
set -e

object=$1
threshold_dbm=$2
required_confirmations=$3
timeout_seconds=$4
dry_run=$5
ifname=${object#hostapd.}
recent_ms=3000

JSON_PREFIX=
. /usr/share/libubox/jshn.sh

timestamp_ms() {
	ucode -e 'let t = clock(); print(t[0] * 1000 + int(t[1] / 1000000));'
}

load_client() {
	client_count=0
	recent_count=0
	client_signal=-
	client_btm=0
	client_neighbor_report=0
	client_json_key=
	client_addr=

	json_load "$(ubus -S call "$object" get_clients)"
	if json_select clients 2>/dev/null; then
		json_get_keys client_keys
		set -- $client_keys
		client_count=$#
		if [ "$client_count" -eq 1 ]; then
			client_json_key=$1
			# jshn maps punctuation in object keys to underscores when it
			# exposes them as shell variable names. Restore and validate the
			# MAC syntax entirely on the AP; the value is never emitted.
			client_addr=$(printf '%s' "$client_json_key" | tr '_' ':')
			if ! printf '%s\n' "$client_addr" | \
				grep -Eq '^([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}$'; then
				client_addr=
				client_count=0
				return
			fi
			json_select "$client_json_key"
			json_get_var client_signal signal || client_signal=-

			rrm_values=
			json_get_values rrm_values rrm || true
			set -- $rrm_values
			rrm0=${1:-0}
			case "$rrm0" in
				''|*[!0-9]*) rrm0=0 ;;
			esac

			extended_values=
			json_get_values extended_values extended_capabilities || true
			set -- $extended_values
			extended2=${3:-0}
			case "$extended2" in
				''|*[!0-9]*) extended2=0 ;;
			esac

			[ $((extended2 & 8)) -ne 0 ] && client_btm=1
			[ $((rrm0 & 2)) -ne 0 ] && client_neighbor_report=1
		fi
	fi

	station_data=$(iw dev "$ifname" station dump 2>/dev/null || true)
	recent_count=$(printf '%s\n' "$station_data" | awk -v threshold="$recent_ms" '
		$1 == "inactive" && $2 == "time:" && $3 ~ /^[0-9]+$/ && $3 <= threshold {
			count++
		}
		END { print count + 0 }
	')
}

load_neighbor() {
	neighbor_count=0
	neighbor_record=
	json_load "$(ubus -S call "$object" rrm_nr_list)"
	if json_select list 2>/dev/null; then
		json_get_keys neighbor_keys
		set -- $neighbor_keys
		neighbor_count=$#
		if [ "$neighbor_count" -eq 1 ]; then
			json_select "$1"
			json_get_var neighbor_record 3 || neighbor_record=
		fi
	fi
}

load_counters() {
	request_count=-
	response_count=-
	json_load "$(ubus -S call "$object" get_status)"
	if json_select wnm 2>/dev/null; then
		json_get_var request_count bss_transition_request_tx || request_count=-
		json_get_var response_count bss_transition_response_rx || response_count=-
	fi
	case "$request_count:$response_count" in
		*[!0-9:]*|:*|*:) return 1 ;;
	esac
	return 0
}

send_advisory_btm() {
	json_init
	json_add_string addr "$client_addr"
	json_add_boolean disassociation_imminent 0
	json_add_int disassociation_timer 0
	json_add_int validity_period 30
	json_add_array neighbors
	json_add_string '' "$neighbor_record"
	json_close_array
	json_add_boolean abridged 1
	payload=$(json_dump)
	# Simplified mode suppresses ubus echoing the private request payload on an
	# error. The caller records a sanitized request_failed event instead.
	ubus -S call "$object" bss_transition_request "$payload" >/dev/null
}

response_from_log() {
	response_status=-
	termination_delay=-
	target_match=-
	line=$(logread -l 80 -e 'BSS-TM-RESP' 2>/dev/null | tail -n 1 || true)
	case "$line" in
		*status_code=*)
			response_status=${line#*status_code=}
			response_status=${response_status%% *}
			;;
	esac
	case "$line" in
		*bss_termination_delay=*)
			termination_delay=${line#*bss_termination_delay=}
			termination_delay=${termination_delay%% *}
			;;
	esac
	case "$line" in
		*target_bssid=*)
			actual_target=${line#*target_bssid=}
			actual_target=${actual_target%% *}
			actual_target=$(printf '%s' "$actual_target" | tr -d ':' | tr 'A-F' 'a-f')
			expected_target=$(printf '%.12s' "$neighbor_record" | tr 'A-F' 'a-f')
			if [ "$actual_target" = "$expected_target" ]; then
				target_match=1
			else
				target_match=0
			fi
			;;
	esac
	case "$response_status" in ''|*[!0-9]*) response_status=- ;; esac
	case "$termination_delay" in ''|*[!0-9]*) termination_delay=- ;; esac
}

if ! ubus -S list "$object" | grep -Fqx "$object"; then
	echo "Source AP object is unavailable" >&2
	exit 1
fi
if ! command -v ucode >/dev/null 2>&1; then
	echo "ucode is required for millisecond event timestamps" >&2
	exit 1
fi
if ! ubus -v list "$object" | grep -q '"bss_transition_request"'; then
	echo "Source AP does not expose bss_transition_request" >&2
	exit 1
fi

load_neighbor
if [ "$neighbor_count" -ne 1 ]; then
	echo "Expected exactly one source Neighbor Report" >&2
	exit 1
fi
neighbor_length=${#neighbor_record}
case "$neighbor_record" in
	''|*[!0-9A-Fa-f]*)
		echo "Source Neighbor Report is not hexadecimal" >&2
		exit 1
		;;
esac
if [ "$neighbor_length" -lt 26 ] || [ $((neighbor_length % 2)) -ne 0 ]; then
	echo "Source Neighbor Report has an invalid length" >&2
	exit 1
fi

load_client
if [ "$client_count" -ne 1 ] || [ "$recent_count" -ne 1 ]; then
	echo "Expected exactly one recently active source client" >&2
	exit 1
fi
if [ "$client_btm" -ne 1 ]; then
	echo "The source client did not advertise BTM support" >&2
	exit 1
fi
if ! load_counters; then
	echo "Unable to read source BTM counters" >&2
	exit 1
fi
request_before=$request_count
response_before=$response_count

echo "# One-shot advisory BTM gate; no station or BSS identifier is emitted."
printf '%b\n' 'timestamp_ms\tevent\tsignal_dbm\trecent_count\tconsecutive_weak\trequest_tx\tresponse_rx\tstatus_code\tbss_termination_delay\ttarget_match'
printf '%s\tpreflight\t%s\t%s\t0\t%s\t%s\t-\t-\t-\n' \
	"$(timestamp_ms)" "$client_signal" "$recent_count" \
	"$request_before" "$response_before"

started=$(date +%s)
consecutive=0
sent=0
while [ "$(date +%s)" -lt $((started + timeout_seconds)) ]; do
	load_client
	valid_signal=0
	case "$client_signal" in
		-*)
			signal_magnitude=${client_signal#-}
			case "$signal_magnitude" in
				''|*[!0-9]*) ;;
				*) valid_signal=1 ;;
			esac
			;;
	esac

	if [ "$client_count" -eq 1 ] && [ "$recent_count" -eq 1 ] && \
		[ "$client_btm" -eq 1 ] && [ "$valid_signal" -eq 1 ] && \
		[ "$client_signal" -le "$threshold_dbm" ]; then
		consecutive=$((consecutive + 1))
	else
		consecutive=0
	fi

	printf '%s\tsample\t%s\t%s\t%s\t%s\t%s\t-\t-\t-\n' \
		"$(timestamp_ms)" "$client_signal" "$recent_count" "$consecutive" \
		"$request_before" "$response_before"

	if [ "$consecutive" -ge "$required_confirmations" ]; then
		load_neighbor
		if [ "$neighbor_count" -ne 1 ]; then
			echo "Neighbor Report set changed before the request" >&2
			exit 1
		fi
		if [ "$dry_run" -eq 1 ]; then
			printf '%s\tdry_run_gate_passed\t%s\t%s\t%s\t%s\t%s\t-\t-\t-\n' \
				"$(timestamp_ms)" "$client_signal" "$recent_count" \
				"$consecutive" "$request_before" "$response_before"
			exit 0
		fi
		printf '%s\trequest_attempt\t%s\t%s\t%s\t%s\t%s\t-\t-\t-\n' \
			"$(timestamp_ms)" "$client_signal" "$recent_count" \
			"$consecutive" "$request_before" "$response_before"
		if ! send_advisory_btm; then
			if load_counters; then
				printf '%s\trequest_failed\t%s\t%s\t%s\t%s\t%s\t-\t-\t-\n' \
					"$(timestamp_ms)" "$client_signal" "$recent_count" \
					"$consecutive" "$request_count" "$response_count"
			fi
			echo "Hostapd rejected the advisory BTM request before its transmit counter advanced" >&2
			exit 1
		fi
		sent=1
		break
	fi
	sleep 1
done

if [ "$sent" -eq 0 ]; then
	printf '%s\tgate_timeout\t-\t%s\t%s\t%s\t%s\t-\t-\t-\n' \
		"$(timestamp_ms)" "$recent_count" "$consecutive" \
		"$request_before" "$response_before"
	exit 0
fi

load_counters
request_after=$request_count
response_after=$response_count
printf '%s\trequest_sent\t%s\t%s\t%s\t%s\t%s\t-\t-\t-\n' \
	"$(timestamp_ms)" "$client_signal" "$recent_count" "$consecutive" \
	"$request_after" "$response_after"
if [ "$request_after" -ne $((request_before + 1)) ]; then
	echo "BTM request counter did not increase exactly once" >&2
	exit 1
fi

waited=0
while [ "$waited" -lt 15 ]; do
	load_counters
	if [ "$response_count" -gt "$response_before" ]; then
		response_from_log
		printf '%s\tresponse_received\t-\t-\t-\t%s\t%s\t%s\t%s\t%s\n' \
			"$(timestamp_ms)" "$request_count" "$response_count" \
			"$response_status" "$termination_delay" "$target_match"
		exit 0
	fi
	sleep 1
	waited=$((waited + 1))
done

printf '%s\tresponse_timeout\t-\t-\t-\t%s\t%s\t-\t-\t-\n' \
	"$(timestamp_ms)" "$request_count" "$response_count"
REMOTE
