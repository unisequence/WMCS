#!/bin/sh

set -eu

temporary=$(mktemp -d /tmp/wmcs-pairing-wire.XXXXXX)
cleanup() {
	rm -rf "$temporary"
}
trap cleanup EXIT INT TERM

${CC:-cc} -std=c11 -Wall -Wextra -Werror \
	-Isrc/wmcsd tests/pairing_wire_test.c src/wmcsd/pairing_wire.c \
	-o "$temporary/pairing-wire-test"
"$temporary/pairing-wire-test"

echo "Pairing wire codec tests: ok"
