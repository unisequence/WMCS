#!/bin/sh

# SPDX-License-Identifier: Apache-2.0

set -eu
umask 077

output=${1:-/tmp/wmcs-snapshot-$$}
suffix=${output#/tmp/wmcs-snapshot-}

case "$output" in
	/tmp/wmcs-snapshot-*)
		case "$suffix" in
			''|*[!A-Za-z0-9._-]*|*..*)
				echo "snapshot name contains unsafe characters" >&2
				exit 2
				;;
		esac
		;;
	*)
		echo "snapshot output must be a new /tmp/wmcs-snapshot-* path" >&2
		exit 2
		;;
esac

if [ -e "$output" ]; then
	echo "snapshot output already exists: $output" >&2
	exit 1
fi

mkdir -m 0700 "$output"

capture() {
	name=$1
	shift
	if command -v "$1" >/dev/null 2>&1; then
		"$@" >"$output/$name" 2>&1 || true
	else
		printf 'unavailable: %s\n' "$1" >"$output/$name"
	fi
}

{
	printf 'snapshot_version=1\n'
	printf 'created_utc='
	date -u '+%Y-%m-%dT%H:%M:%SZ'
	printf 'hostname='
	hostname 2>/dev/null || printf 'unknown\n'
	printf 'scope=read-only; no raw UCI values or logs\n'
} >"$output/metadata.txt"

capture uname.txt uname -a
if [ -r /etc/openwrt_release ]; then
	cp /etc/openwrt_release "$output/openwrt-release.txt"
else
	printf 'unavailable: /etc/openwrt_release\n' >"$output/openwrt-release.txt"
fi

capture ubus-system-board.json ubus call system board
capture ubus-objects.txt ubus list
capture network-interfaces.json ubus call network.interface dump
capture ip-link.txt ip -details link show
capture ip-address.txt ip address show
capture ip-route.txt ip route show table all
capture bridge-link.txt bridge link show
capture bridge-vlan.txt bridge vlan show
capture iw-dev.txt iw dev
capture iw-phy.txt iw phy
capture iwinfo.txt iwinfo

if command -v ubus >/dev/null 2>&1; then
	ubus list 'hostapd.*' 2>/dev/null | while IFS= read -r object; do
		case "$object" in
			hostapd.*) ;;
			*) continue ;;
		esac
		filename=$(printf '%s' "$object" | tr -c 'A-Za-z0-9_.-' '_')
		ubus call "$object" get_status >"$output/$filename-status.json" 2>&1 || true
	done
fi

: >"$output/uci-schema.txt"
: >"$output/uci-hashes.txt"
if command -v uci >/dev/null 2>&1; then
	for package in network wireless firewall dhcp; do
		printf '[%s]\n' "$package" >>"$output/uci-schema.txt"
		uci -q show "$package" 2>/dev/null | awk -F= '{ print $1 }' |
			sort -u >>"$output/uci-schema.txt" || true

		if command -v sha256sum >/dev/null 2>&1; then
			digest=$(uci -q export "$package" 2>/dev/null | sha256sum | awk '{ print $1 }')
			printf '%s %s\n' "$package" "$digest" >>"$output/uci-hashes.txt"
		fi
	done
else
	printf 'unavailable: uci\n' >>"$output/uci-schema.txt"
	printf 'unavailable: uci\n' >>"$output/uci-hashes.txt"
fi

if command -v apk >/dev/null 2>&1; then
	capture packages.txt apk info
elif command -v opkg >/dev/null 2>&1; then
	capture packages.txt opkg list-installed
else
	printf 'unavailable: apk/opkg\n' >"$output/packages.txt"
fi

if command -v sha256sum >/dev/null 2>&1; then
	(
		cd "$output"
		find . -type f ! -name SHA256SUMS -print | sort |
			while IFS= read -r path; do sha256sum "$path"; done
	) >"$output/SHA256SUMS"
fi

printf '%s\n' "$output"
