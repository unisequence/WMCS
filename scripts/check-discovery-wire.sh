#!/bin/sh

set -eu

mkdir -p build/tests
${CC:-cc} -std=c11 -Wall -Wextra -Werror -pedantic \
	-Isrc/wmcsd \
	tests/discovery_wire_test.c src/wmcsd/discovery_wire.c \
	-o build/tests/discovery_wire_test
build/tests/discovery_wire_test

echo "Discovery wire codec tests: ok"
