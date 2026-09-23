#!/bin/sh

set -eu

openwrt_dir=${OPENWRT_DIR:-/home/uni/openwrt-upstream}
target=${1:-}

case "$target" in
	aarch64)
		arch=aarch64_cortex-a53
		compiler_name=aarch64-openwrt-linux-musl-gcc
		target_flags='-mcpu=cortex-a53'
		;;
	mipsel)
		arch=mipsel_24kc
		compiler_name=mipsel-openwrt-linux-musl-gcc
		target_flags='-mips32r2 -mtune=24kc -msoft-float -mno-mips16'
		;;
	*)
		echo "usage: $0 aarch64|mipsel" >&2
		exit 2
		;;
esac

if [ ! -f "$openwrt_dir/rules.mk" ]; then
	echo "OPENWRT_DIR is not an OpenWrt tree: $openwrt_dir" >&2
	exit 1
fi

compiler=$(find "$openwrt_dir/staging_dir" -maxdepth 3 -type f \
	-name "$compiler_name" -print | sort -V | tail -n 1)
target_dir="$openwrt_dir/staging_dir/target-${arch}_musl"
export STAGING_DIR="$openwrt_dir/staging_dir"

if [ -z "$compiler" ] || [ ! -x "$compiler" ]; then
	echo "missing compiler: $compiler_name" >&2
	exit 1
fi

if [ ! -f "$target_dir/usr/include/libubus.h" ]; then
	echo "missing target libubus staging for $arch" >&2
	exit 1
fi

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
out_dir="$script_dir/out/$target"
mkdir -p "$out_dir"

# target_flags is a fixed value selected above, not caller-controlled input.
# shellcheck disable=SC2086
"$compiler" \
	-Os -Wall -Wextra -Werror \
	-fPIE -fstack-protector-strong -D_FORTIFY_SOURCE=2 \
	$target_flags \
	-I"$target_dir/usr/include" \
	-L"$target_dir/usr/lib" \
	-Wl,-rpath-link,"$target_dir/usr/lib" \
	-Wl,-z,relro,-z,now -pie \
	"$script_dir/wmcs_ubus_spike.c" \
	-lubus -lubox \
	-o "$out_dir/wmcs-ubus-spike"

strip_name=${compiler_name%-gcc}-strip
strip_tool=$(dirname "$compiler")/$strip_name
if [ -x "$strip_tool" ]; then
	"$strip_tool" "$out_dir/wmcs-ubus-spike"
fi

file "$out_dir/wmcs-ubus-spike"
wc -c "$out_dir/wmcs-ubus-spike"
