#!/bin/sh

# Install a staged WMCS lab runtime using private ABI aliases. This is intended
# for controlled hardware experiments, not unattended production deployment.

set -eu
umask 077

stage=${1:-}
home=/usr/lib/wmcs
launcher=/usr/sbin/wmcsd
init_script=/etc/init.d/wmcsd
config=/etc/config/wmcs
state_home=/etc/wmcs
activated=0

fail() {
	echo "wmcs native install: $*" >&2
	exit 1
}

remove_if_present() {
	[ ! -e "$1" ] && [ ! -L "$1" ] || rm "$1"
}

rollback() {
	[ "$activated" -eq 0 ] && return 0
	remove_if_present "$launcher"
	remove_if_present "$init_script"
	remove_if_present "$config"
	[ ! -d "$state_home" ] || rmdir "$state_home" 2>/dev/null || true
	remove_if_present "$home/libubus.so.20250102"
	remove_if_present "$home/libubox.so.20240329"
	remove_if_present "$home/libuci.so.20250120"
	remove_if_present "$home/wmcsd.bin"
	[ ! -d "$home" ] || rmdir "$home" 2>/dev/null || true
}

cleanup() {
	status=$?
	if [ "$status" -ne 0 ]; then
		rollback
	fi
	exit "$status"
}
trap cleanup EXIT INT TERM

[ "$(id -u)" -eq 0 ] || fail "must run as root"
[ -n "$stage" ] && [ -d "$stage" ] || fail "staging directory is missing"

for file in wmcsd.bin wmcsd.wrapper wmcsd.init wmcs.config read-only-smoke.sh; do
	[ -f "$stage/$file" ] || fail "staged file is missing: $file"
done

for path in "$home" "$launcher" "$init_script" "$config" "$state_home"; do
	if [ -e "$path" ] || [ -L "$path" ]; then
		fail "refusing to replace existing path: $path"
	fi
done

chmod 0700 "$stage/wmcsd.bin" "$stage/read-only-smoke.sh"
compat=$stage/compat
mkdir -m 0700 "$compat"

link_runtime_library() {
	required=$1
	if [ -e "/lib/$required" ] || [ -e "/usr/lib/$required" ]; then
		return 0
	fi

	case "$required" in
		*.so.*) prefix=${required%%.so.*}.so. ;;
		*) fail "invalid runtime library name: $required" ;;
	esac

	for candidate in /lib/${prefix}* /usr/lib/${prefix}*; do
		[ -e "$candidate" ] || continue
		ln -s "$candidate" "$compat/$required"
		echo "wmcs native install: alias $required -> $candidate"
		return 0
	done

	fail "runtime library is missing: $required"
}

# The portable lab binary is built against the oldest supported OpenWrt 24.10
# ABI. Newer candidates are accepted only through a private alias and only
# after the executable smoke test below succeeds.
link_runtime_library libubus.so.20250102
link_runtime_library libubox.so.20240329
link_runtime_library libuci.so.20250120
link_runtime_library libmbedcrypto.so.16

LD_LIBRARY_PATH="$compat:/lib:/usr/lib" "$stage/wmcsd.bin" --version >/dev/null
LD_LIBRARY_PATH="$compat:/lib:/usr/lib" \
	"$stage/read-only-smoke.sh" "$stage/wmcsd.bin" standalone >/dev/null

mkdir -m 0755 "$home"
mkdir -m 0700 "$state_home"
activated=1
cp "$stage/wmcsd.bin" "$home/wmcsd.bin"
chmod 0755 "$home/wmcsd.bin"

for alias in "$compat"/*.so.*; do
	[ -L "$alias" ] || continue
	ln -s "$(readlink "$alias")" "$home/${alias##*/}"
done

cp "$stage/wmcsd.wrapper" "$launcher"
cp "$stage/wmcsd.init" "$init_script"
cp "$stage/wmcs.config" "$config"
chmod 0755 "$launcher" "$init_script"
chmod 0600 "$config"

"$launcher" --version >/dev/null
"$stage/read-only-smoke.sh" "$launcher" standalone >/dev/null

if pidof wmcsd.bin >/dev/null 2>&1 || ubus -S list wmcs >/dev/null 2>&1; then
	fail "runtime remained active after smoke test"
fi

enabled=$(uci -q get wmcs.core.enabled)
mutation=$(uci -q get wmcs.core.mutation_enabled)
[ "$enabled" = 0 ] || fail "installed config is not disabled"
[ "$mutation" = 0 ] || fail "installed mutation gate is not disabled"

activated=0
trap - EXIT INT TERM
echo "wmcs native install: ok"
sha256sum "$home/wmcsd.bin"
