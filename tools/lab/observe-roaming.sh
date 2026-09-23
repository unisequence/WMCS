#!/bin/sh

set -eu

controller=${1:-root@192.168.1.1}
agent=${2:-root@192.168.1.2}
duration=${3:-120}
interval=${WMCS_ROAM_INTERVAL_SECONDS:-1}
recent_ms=${WMCS_ROAM_RECENT_MS:-10000}
controller_ap=${WMCS_CONTROLLER_AP:-hostapd.phy1-ap0}
agent_ap=${WMCS_AGENT_AP:-hostapd.phy1-ap0}
ssh_options='-o BatchMode=yes -o ConnectTimeout=3 -o LogLevel=ERROR'

for target in "$controller" "$agent"; do
	case "$target" in
		-*|*[!A-Za-z0-9._@:-]*|'')
			echo "Invalid SSH target: $target" >&2
			exit 2
			;;
	esac
done

case "$duration" in
	''|*[!0-9]*)
		echo "Duration must be an integer number of seconds" >&2
		exit 2
		;;
esac
if [ "$duration" -lt 5 ] || [ "$duration" -gt 3600 ]; then
	echo "Duration must be between 5 and 3600 seconds" >&2
	exit 2
fi

case "$interval" in
	''|*[!0-9]*)
		echo "WMCS_ROAM_INTERVAL_SECONDS must be an integer" >&2
		exit 2
		;;
esac
if [ "$interval" -lt 1 ] || [ "$interval" -gt 10 ]; then
	echo "WMCS_ROAM_INTERVAL_SECONDS must be between 1 and 10" >&2
	exit 2
fi

case "$recent_ms" in
	''|*[!0-9]*)
		echo "WMCS_ROAM_RECENT_MS must be an integer" >&2
		exit 2
		;;
esac
if [ "$recent_ms" -lt 1000 ] || [ "$recent_ms" -gt 60000 ]; then
	echo "WMCS_ROAM_RECENT_MS must be between 1000 and 60000" >&2
	exit 2
fi

for object in "$controller_ap" "$agent_ap"; do
	case "$object" in
		*[!A-Za-z0-9._-]*|'')
			echo "Invalid hostapd ubus object: $object" >&2
			exit 2
			;;
	esac
done

controller_sample=$(mktemp /tmp/wmcs-roam-controller.XXXXXX)
agent_sample=$(mktemp /tmp/wmcs-roam-agent.XXXXXX)

cleanup() {
	rm -f "$controller_sample" "$agent_sample"
}
trap cleanup EXIT INT TERM

check_ap() {
	target=$1
	object=$2
	# The object name is restricted above before it enters the fixed command.
	ssh $ssh_options "$target" "ubus -S list '$object'" |
		grep -Fqx "$object"
}

sample_ap() {
	target=$1
	object=$2
	# Aggregate on the router so station MAC addresses never cross SSH.  The
	# hostapd ubus response exposes raw RRM and Extended Capabilities arrays,
	# not synthetic "btm" or "rrm_neighbor_report" fields.  Decode the public
	# IEEE capability bits while each client object is selected locally:
	#
	#   WLAN_EXT_CAPAB_BSS_TRANSITION       bit 19 (octet 2, mask 0x08)
	#   WLAN_RRM_CAPS_NEIGHBOR_REPORT       bit 1  (octet 0, mask 0x02)
	ssh $ssh_options "$target" sh -s -- "$object" "$recent_ms" <<'REMOTE'
set -e

object=$1
recent_ms=$2
case "$object" in
	hostapd.*) ifname=${object#hostapd.} ;;
	*) exit 2 ;;
esac
JSON_PREFIX=
. /usr/share/libubox/jshn.sh

data=$(ubus -S call "$object" get_clients)
json_load "$data"

if ! json_select clients 2>/dev/null; then
	printf '0\t0\t-\t-\t-\t-\n'
	exit 0
fi

json_get_keys client_keys
count=0
signals=
btm_values=
neighbor_values=

for client_key in $client_keys; do
	json_select "$client_key"

	signal=-
	json_get_var signal signal || true

	rrm_values=
	json_get_values rrm_values rrm || true
	set -- $rrm_values
	rrm0=${1:-0}
	case "$rrm0" in
		''|*[!0-9]*) rrm0=0 ;;
	esac

	ext_values=
	json_get_values ext_values extended_capabilities || true
	set -- $ext_values
	ext2=${3:-0}
	case "$ext2" in
		''|*[!0-9]*) ext2=0 ;;
	esac

	btm=0
	neighbor=0
	[ $((ext2 & 8)) -ne 0 ] && btm=1
	[ $((rrm0 & 2)) -ne 0 ] && neighbor=1

	if [ "$count" -gt 0 ]; then
		signals="$signals,$signal"
		btm_values="$btm_values,$btm"
		neighbor_values="$neighbor_values,$neighbor"
	else
		signals=$signal
		btm_values=$btm
		neighbor_values=$neighbor
	fi

	count=$((count + 1))
	json_select ..
done

# A Wi-Fi 7/MLO-capable station may be keyed by an MLD address in hostapd and
# by a link address in nl80211. Aggregate kernel inactivity independently so
# neither address crosses SSH and no unreliable address join is required.
station_data=$(iw dev "$ifname" station dump 2>/dev/null || true)
recent_count=$(printf '%s\n' "$station_data" | awk -v threshold="$recent_ms" '
	$1 == "inactive" && $2 == "time:" && $3 ~ /^[0-9]+$/ && $3 <= threshold {
		count++
	}
	END { print count + 0 }
')
inactive_values=$(printf '%s\n' "$station_data" | awk '
	$1 == "inactive" && $2 == "time:" && $3 ~ /^[0-9]+$/ {
		if (count++) printf ","
		printf "%s", $3
	}
	END { if (!count) printf "-"; printf "\n" }
')

printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$count" "$recent_count" \
	"${signals:--}" "${inactive_values:--}" "${btm_values:--}" \
	"${neighbor_values:--}"
REMOTE
}

if ! check_ap "$controller" "$controller_ap"; then
	echo "Controller AP object is unavailable: $controller_ap" >&2
	exit 1
fi
if ! check_ap "$agent" "$agent_ap"; then
	echo "Agent AP object is unavailable: $agent_ap" >&2
	exit 1
fi

echo "# WMCS passive roaming observation"
echo "# No station MAC address or WLAN credential is collected."
echo "# A clean single-phone run should have at most one associated client in total."
echo "# recent_activity_count means kernel inactivity <= ${recent_ms} ms; it is not proof of association state."
printf '%b\n' 'timestamp_ms\tcontroller_count\tcontroller_recent_activity_count\tcontroller_signal_dbm\tcontroller_inactive_ms\tcontroller_btm\tcontroller_neighbor_report\tagent_count\tagent_recent_activity_count\tagent_signal_dbm\tagent_inactive_ms\tagent_btm\tagent_neighbor_report'

started=$(date +%s)
while :; do
	now=$(date +%s)
	if [ $((now - started)) -ge "$duration" ]; then
		break
	fi

	sample_ap "$controller" "$controller_ap" >"$controller_sample" &
	controller_pid=$!
	sample_ap "$agent" "$agent_ap" >"$agent_sample" &
	agent_pid=$!
	controller_status=0
	agent_status=0
	wait "$controller_pid" || controller_status=$?
	wait "$agent_pid" || agent_status=$?
	if [ "$controller_status" -ne 0 ] || [ "$agent_status" -ne 0 ]; then
		echo "Failed to sample one or both APs" >&2
		exit 1
	fi

	timestamp_ms=$(date +%s%3N)
	printf '%s\t' "$timestamp_ms"
	tr '\n' '\t' <"$controller_sample"
	cat "$agent_sample"
	sleep "$interval"
done
