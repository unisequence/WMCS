#!/bin/sh

set -eu

test_binary=$(mktemp /tmp/wmcs-result-store.XXXXXX)

cleanup() {
	if [ -e "$test_binary" ]; then
		busybox rm "$test_binary" 2>/dev/null || rm "$test_binary"
	fi
}
trap cleanup EXIT INT TERM

${CC:-cc} -std=c11 -Wall -Wextra -Werror \
	-Isrc/wmcsd tests/result_store_test.c src/wmcsd/result_store.c \
	src/wmcsd/atomic_file.c src/wmcsd/control_wire.c -o "$test_binary"
"$test_binary"
