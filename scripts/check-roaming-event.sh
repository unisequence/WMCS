#!/bin/sh

set -eu

openwrt_dir=${OPENWRT_DIR:-/home/uni/openwrt-upstream}
host_dir=$openwrt_dir/staging_dir/host

if [ ! -f "$host_dir/include/libubox/blobmsg.h" ] ||
   [ ! -f "$host_dir/lib/libubox.a" ]; then
	echo "OpenWrt host libubox is required: $host_dir" >&2
	exit 1
fi

mkdir -p build/tests
cc -std=c11 -Wall -Wextra -Werror \
	-I"$host_dir/include" -I src/wmcsd \
	tests/roaming_event_test.c src/wmcsd/roaming_event.c \
	"$host_dir/lib/libubox.a" \
	-o build/tests/roaming_event_test
build/tests/roaming_event_test
