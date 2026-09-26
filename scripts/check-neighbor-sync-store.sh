#!/bin/sh

set -eu

openwrt_dir=${OPENWRT_DIR:-/home/uni/openwrt-upstream}
psa_include="$openwrt_dir/staging_dir/target-aarch64_cortex-a53_musl/usr/include"
[ -f "$psa_include/psa/crypto.h" ] || {
	echo "Missing staged PSA header: $psa_include" >&2
	exit 1
}

mkdir -p build/tests
cc -std=c11 -Wall -Wextra -Werror -I "$psa_include" -I src/wmcsd \
	tests/neighbor_sync_store_test.c \
	src/wmcsd/neighbor_sync_store.c \
	src/wmcsd/neighbor_sync_wire.c \
	src/wmcsd/atomic_file.c \
	-o build/tests/neighbor_sync_store_test
build/tests/neighbor_sync_store_test
