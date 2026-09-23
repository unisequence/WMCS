#!/bin/sh

set -eu

menu=package/openwrt/luci-app-wmcs/root/usr/share/luci/menu.d/luci-app-wmcs.json
acl=package/openwrt/luci-app-wmcs/root/usr/share/rpcd/acl.d/luci-app-wmcs.json
view=package/openwrt/luci-app-wmcs/htdocs/luci-static/resources/view/wmcs/overview.js
style=package/openwrt/luci-app-wmcs/htdocs/luci-static/resources/wmcs/overview.css
translation=package/openwrt/luci-app-wmcs/po/ru/wmcs.po

python3 - "$menu" "$acl" <<'PY'
import json
import sys

menu_path, acl_path = sys.argv[1:]

with open(menu_path, encoding="utf-8") as stream:
    menu = json.load(stream)
with open(acl_path, encoding="utf-8") as stream:
    acl = json.load(stream)

entry = menu.get("admin/network/wmcs")
if not entry or entry.get("action") != {
    "type": "view",
    "path": "wmcs/overview",
}:
    raise SystemExit("LuCI menu must point to the wmcs/overview JS view")
if entry.get("depends") != {"acl": ["luci-app-wmcs"]}:
    raise SystemExit("LuCI menu must retain the luci-app-wmcs ACL dependency")

group = acl.get("luci-app-wmcs")
if not group:
    raise SystemExit("LuCI ACL group is missing")

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

if group.get("read", {}).get("uci") != ["wmcs"]:
    raise SystemExit("LuCI ACL must read only the wmcs UCI config")
if set(group.get("read", {}).get("ubus", {}).get("wmcs", [])) != expected_read:
    raise SystemExit("LuCI ACL read methods are not the read-only WMCS surface")
if group.get("write") != {"uci": ["wmcs"]}:
    raise SystemExit("LuCI ACL write scope must remain limited to wmcs UCI")
PY

for token in "'require form'" "'require poll'" "rpc.declare" "form.Map('wmcs'" \
	"roaming_status" "source_trigger_dbm" "poll.add" "wmcs-nodes" "wmcs-peers"; do
	if ! grep -Fq "$token" "$view"; then
		echo "LuCI view is missing required surface: $token" >&2
		exit 1
	fi
done

if [ ! -s "$style" ]; then
	echo "LuCI view stylesheet is missing" >&2
	exit 1
fi

if [ ! -s "$translation" ] || ! grep -Fq 'Language: ru' "$translation" || \
	! grep -Fq 'msgid "Wireless mesh coordination"' "$translation" || \
	! grep -Fq 'msgstr "Координация беспроводной сети"' "$translation"; then
	echo "Russian LuCI translation catalog is missing or incomplete" >&2
	exit 1
fi

if command -v node >/dev/null 2>&1; then
	node --check "$view"
fi

echo "LuCI WMCS view contract: ok"
