#!/bin/sh

set -eu

output_dir=${1:-}
probe_ip=${2:-}
duration=${3:-120}
controller=${WMCS_CONTROLLER_SSH:-root@192.168.1.1}
agent=${WMCS_AGENT_SSH:-root@192.168.1.2}
script_dir=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
observer=$script_dir/observe-roaming.sh
observer_pid=

usage() {
	echo "Usage: $0 OUTPUT_DIRECTORY PHONE_IPV4 [DURATION_SECONDS]" >&2
	exit 2
}

[ -n "$output_dir" ] && [ -n "$probe_ip" ] || usage
case "$output_dir" in
	/|.|..|-*)
		echo "Refusing unsafe output directory: $output_dir" >&2
		exit 2
		;;
esac
case "$probe_ip" in
	''|*[!0-9.]*|.*|*.) usage ;;
esac
if [ "$(printf '%s' "$probe_ip" | awk -F. '
	NF != 4 { print 0; exit }
	{ for (i = 1; i <= 4; i++) if ($i !~ /^[0-9]+$/ || $i > 255) {
		print 0; exit
	} }
	{ print 1 }
')" != 1 ]; then
	echo "Invalid probe IPv4 address: $probe_ip" >&2
	exit 2
fi
case "$duration" in
	''|*[!0-9]*) usage ;;
esac
if [ "$duration" -lt 10 ] || [ "$duration" -gt 3600 ]; then
	echo "Duration must be between 10 and 3600 seconds" >&2
	exit 2
fi
if [ -e "$output_dir" ]; then
	echo "Refusing to overwrite existing output path: $output_dir" >&2
	exit 2
fi
if [ ! -x "$observer" ]; then
	echo "Observer is not executable: $observer" >&2
	exit 1
fi

cleanup() {
	if [ -n "$observer_pid" ] && kill -0 "$observer_pid" 2>/dev/null; then
		kill "$observer_pid"
		wait "$observer_pid" 2>/dev/null || true
	fi
}
trap cleanup EXIT INT TERM

mkdir -m 0700 "$output_dir"
LC_ALL=C
export LC_ALL

{
	echo "format=wmcs-roaming-baseline-v0"
	echo "started_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
	echo "duration_seconds=$duration"
	echo "probe_interval_ms=100"
	echo "probe_ip=$probe_ip"
	echo "controller=$controller"
	echo "agent=$agent"
	echo "station_identifiers_collected=false"
	echo "wlan_credentials_collected=false"
} >"$output_dir/metadata.txt"

"$observer" "$controller" "$agent" "$duration" \
	>"$output_dir/associations.tsv" 2>"$output_dir/observer.stderr" &
observer_pid=$!

ping_status=0
ping -D -n -i 0.1 -w "$duration" "$probe_ip" \
	>"$output_dir/probe.raw" 2>"$output_dir/probe.stderr" || ping_status=$?

observer_status=0
wait "$observer_pid" || observer_status=$?
observer_pid=

awk 'BEGIN {
	OFS="\t";
	print "timestamp_s", "icmp_sequence", "rtt_ms"
}
/bytes from/ {
	timestamp=$1;
	gsub(/^\[/, "", timestamp);
	gsub(/\]$/, "", timestamp);
	sequence="";
	rtt="";
	for (i=1; i<=NF; i++) {
		if ($i ~ /^icmp_seq=/) { split($i, value, "="); sequence=value[2] }
		if ($i ~ /^time=/) { split($i, value, "="); rtt=value[2] }
	}
	if (sequence != "" && rtt != "") print timestamp, sequence, rtt
}' "$output_dir/probe.raw" >"$output_dir/probe.tsv"

awk -F '\t' 'NR > 1 {
	if (previous != "") {
		gap=$2-previous-1;
		if (gap > maximum) maximum=gap
	}
	previous=$2;
	received++
}
END {
	print "received_replies=" received+0;
	print "max_internal_consecutive_missing_replies=" maximum+0;
	print "max_internal_missing_interval_ms=" (maximum+0)*100;
}' "$output_dir/probe.tsv" >"$output_dir/summary.txt"

{
	echo "ping_exit_status=$ping_status"
	echo "observer_exit_status=$observer_status"
	tail -n 2 "$output_dir/probe.raw" || true
} >>"$output_dir/summary.txt"

if [ "$observer_status" -ne 0 ]; then
	echo "Association observer failed; inspect $output_dir/observer.stderr" >&2
	exit 1
fi
probe_rows=$(awk 'END { print NR+0 }' "$output_dir/probe.tsv")
if [ "$probe_rows" -le 1 ]; then
	echo "Continuity probe produced no parseable output" >&2
	exit 1
fi

echo "Roaming baseline captured in: $output_dir"
cat "$output_dir/summary.txt"
