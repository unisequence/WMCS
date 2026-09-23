#!/bin/sh

# SPDX-License-Identifier: Apache-2.0

set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
test_root=$(mktemp -d /tmp/wmcs-snapshot-test.XXXXXX)
output="${test_root}.output"

cleanup() {
	rm -rf "$test_root" "$output"
}
trap cleanup EXIT INT TERM

if "$root/tools/snapshot/wmcs-snapshot.sh" "$test_root/not-allowed" >/dev/null 2>&1; then
	echo "snapshot accepted a path outside the required prefix" >&2
	exit 1
fi

"$root/tools/snapshot/wmcs-snapshot.sh" "$output" >/dev/null

test -f "$output/metadata.txt"
test -f "$output/uci-schema.txt"
test -f "$output/uci-hashes.txt"

if grep -qE '(^|[^[])[[:alnum:]_.-]+=' "$output/uci-schema.txt"; then
	echo "UCI schema output contains values" >&2
	exit 1
fi

echo "Read-only snapshot test: ok"
