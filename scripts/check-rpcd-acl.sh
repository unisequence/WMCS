#!/bin/sh

set -eu

acl=package/openwrt/files/wmcs.rpcd-acl.json

python3 - "$acl" <<'PY'
import json
import sys

path = sys.argv[1]
with open(path, encoding="utf-8") as stream:
    document = json.load(stream)

expected_read = {
    "status",
    "roaming_status",
    "capabilities",
    "nodes",
    "identity",
    "peers",
    "pairing_status",
    "wlan_sync_status",
    "release_status",
}
expected_write = {
    "discovery_start",
    "discovery_stop",
    "pairing_start",
    "pairing_confirm",
    "pairing_stop",
    "control_listen",
    "wlan_sync_start",
    "wlan_sync_stop",
    "release_start",
    "release_stop",
    "forget_orphan",
}

if set(document) != {"wmcs"}:
    raise SystemExit("rpcd ACL must contain only the wmcs group")
group = document["wmcs"]
if set(group) != {"description", "read", "write"}:
    raise SystemExit("rpcd ACL group has an unexpected capability class")
for access, expected in (("read", expected_read), ("write", expected_write)):
    section = group[access]
    if set(section) != {"ubus"} or set(section["ubus"]) != {"wmcs"}:
        raise SystemExit(f"rpcd ACL {access} scope escaped the wmcs ubus object")
    methods = section["ubus"]["wmcs"]
    if len(methods) != len(set(methods)) or set(methods) != expected:
        raise SystemExit(f"rpcd ACL {access} method set is not exact")

print("rpcd ACL contract: ok")
PY
