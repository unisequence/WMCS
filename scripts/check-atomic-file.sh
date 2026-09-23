#!/bin/sh

set -eu

test_binary=$(mktemp /tmp/wmcs-atomic-file.XXXXXX)

cleanup() {
	if [ -e "$test_binary" ]; then
		busybox rm "$test_binary" 2>/dev/null || rm "$test_binary"
	fi
}
trap cleanup EXIT INT TERM


${CC:-cc} -std=c11 -Wall -Wextra -Werror -DWMCS_ATOMIC_FILE_TEST \
	-Isrc/wmcsd tests/atomic_file_test.c src/wmcsd/atomic_file.c \
	-o "$test_binary"
"$test_binary"
echo "Atomic file tests: ok"
