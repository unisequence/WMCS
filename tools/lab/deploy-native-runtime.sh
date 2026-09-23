#!/bin/sh

set -eu

target=${1:-}
binary=${2:-}
root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)

if [ -z "$target" ] || [ -z "$binary" ]; then
	echo "Usage: $0 root@router /path/to/wmcsd" >&2
	exit 2
fi

[ -f "$binary" ] && [ -x "$binary" ] || {
	echo "wmcsd binary is not executable: $binary" >&2
	exit 2
}

ssh_options='-o BatchMode=yes -o ConnectTimeout=5 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null'
stage=$(ssh $ssh_options "$target" 'mktemp -d /tmp/wmcs-native-install.XXXXXX')

case "$stage" in
	/tmp/wmcs-native-install.*) ;;
	*) echo "Unsafe remote staging path: $stage" >&2; exit 1 ;;
esac

cleanup_stage() {
	ssh $ssh_options "$target" "
for file in wmcsd wmcsd.bin wmcsd.wrapper wmcsd.init wmcs.config read-only-smoke.sh install-native-target.sh; do
	[ ! -e '$stage'/\"\$file\" ] || rm '$stage'/\"\$file\"
done
[ ! -d '$stage/compat' ] || {
	for alias in '$stage'/compat/*.so.*; do [ ! -L \"\$alias\" ] || rm \"\$alias\"; done
	rmdir '$stage/compat' 2>/dev/null || true
}
rmdir '$stage' 2>/dev/null || true
" >/dev/null 2>&1 || true
}
trap cleanup_stage EXIT INT TERM

scp -O -q $ssh_options "$binary" "$target:$stage/wmcsd.bin"
scp -O -q $ssh_options \
	"$root/package/openwrt/files/wmcsd.wrapper" \
	"$root/package/openwrt/files/wmcsd.init" \
	"$root/package/openwrt/files/wmcs.config" \
	"$root/tools/lab/read-only-smoke.sh" \
	"$root/tools/lab/install-native-target.sh" \
	"$target:$stage/"

ssh $ssh_options "$target" "chmod 0700 '$stage/install-native-target.sh'; '$stage/install-native-target.sh' '$stage'"
