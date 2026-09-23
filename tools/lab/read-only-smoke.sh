#!/bin/sh

set -eu

binary=${1:-/usr/sbin/wmcsd}
role=${2:-standalone}
pid=

hash_managed_config() {
	for config in network wireless firewall dhcp; do
		printf '\n-- %s --\n' "$config"
		uci -q export "$config" || true
	done | sha256sum | awk '{print $1}'
}

cleanup() {
	if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
		kill "$pid"
		wait "$pid" 2>/dev/null || true
	fi
}
trap cleanup EXIT INT TERM

if [ ! -x "$binary" ]; then
	echo "wmcsd is not executable: $binary" >&2
	exit 2
fi

case "$role" in
	standalone)
		expected_native_protocol=false
		;;
	controller|agent)
		expected_native_protocol=true
		;;
	*)
		echo "Invalid role: $role" >&2
		exit 2
		;;
esac

if ubus -S list wmcs >/dev/null 2>&1; then
	echo "Refusing to interfere with an existing wmcs ubus object" >&2
	exit 2
fi

before=$(hash_managed_config)
log=/tmp/wmcsd-read-only-smoke.log
: > "$log"
"$binary" --role "$role" >"$log" 2>&1 &
pid=$!

attempt=0
while ! ubus -S list wmcs >/dev/null 2>&1; do
	attempt=$((attempt + 1))
	if [ "$attempt" -ge 5 ] || ! kill -0 "$pid" 2>/dev/null; then
		echo "wmcsd did not register its ubus object" >&2
		sed -n '1,40p' "$log" >&2
		exit 1
	fi
	sleep 1
done

status=$(ubus -S call wmcs status)
capabilities=$(ubus -S call wmcs capabilities)
nodes=$(ubus -S call wmcs nodes)

[ "$(jsonfilter -s "$status" -e '@.api_version')" = 0 ]
[ "$(jsonfilter -s "$status" -e '@.state')" = observe_and_pair ]
[ "$(jsonfilter -s "$status" -e '@.role')" = "$role" ]
[ "$(jsonfilter -s "$status" -e '@.mutation_enabled')" = false ]
[ "$(jsonfilter -s "$capabilities" -e '@.features.read_only_inventory')" = true ]
[ "$(jsonfilter -s "$capabilities" -e '@.features.native_protocol')" = "$expected_native_protocol" ]
[ "$(jsonfilter -s "$nodes" -e '@.api_version')" = 0 ]

after=$(hash_managed_config)
if [ "$before" != "$after" ]; then
	echo "Managed UCI configuration changed during read-only smoke test" >&2
	exit 1
fi

printf 'Configuration SHA-256: %s (unchanged)\n' "$before"
printf 'Status: %s\n' "$status"
printf 'Capabilities: %s\n' "$capabilities"
printf 'Nodes: %s\n' "$nodes"
echo "Read-only target smoke test: ok"
