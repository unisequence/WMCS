#!/bin/sh

set -eu

mkdir -p build/tests
cc -std=c11 -Wall -Wextra -Werror -I src/wmcsd \
	tests/neighbor_sync_wire_test.c src/wmcsd/neighbor_sync_wire.c \
	-o build/tests/neighbor_sync_wire_test
build/tests/neighbor_sync_wire_test
