#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path


control = Path("src/wmcsd/control.c").read_text(encoding="utf-8")
ubus = Path("src/wmcsd/ubus_api.c").read_text(encoding="utf-8")
identity = Path("src/wmcsd/identity.c").read_text(encoding="utf-8")


def section(source: str, start: str, end: str) -> str:
    first = source.find(start)
    last = source.find(end, first + len(start))
    if first < 0 or last < 0:
        raise SystemExit(f"forget journal section is missing: {start}")
    return source[first:last]


def ordered(body: str, *tokens: str) -> None:
    position = -1
    for token in tokens:
        current = body.find(token, position + 1)
        if current < 0:
            raise SystemExit(f"forget journal token is missing: {token}")
        if current <= position:
            raise SystemExit(f"forget journal ordering is invalid at: {token}")
        position = current


complete = section(
    control,
    "static int complete_forget(",
    "static int recover_forget(",
)
ordered(
    complete,
    "wmcs_wlan_peer_has_state",
    "wmcs_identity_load_peer",
    "remove_operation_for_peer",
    "remove_result_for_peer",
    "wmcs_identity_delete_peer",
    "wmcs_identity_forget_finish",
)

recover = section(
    control,
    "static int recover_forget(",
    "static int process_agent_request(",
)
ordered(recover, "wmcs_identity_forget_load", "complete_forget")

forget = section(
    control,
    "int wmcs_control_forget_peer(",
    "int wmcs_control_recover(",
)
ordered(
    forget,
    "wmcs_identity_forget_load",
    "wmcs_identity_load_peer",
    "wmcs_wlan_peer_has_state",
    "wmcs_identity_forget_begin",
)
if "complete_forget(control, &existing)" not in forget:
    raise SystemExit("explicit retry must resume an existing forget intent")
if "complete_forget(control, &record)" not in forget:
    raise SystemExit("new forget intent must enter the durable completion path")

recovery_entry = section(
    control,
    "int wmcs_control_recover(",
    "static int start_common(",
)
ordered(
    recovery_entry,
    "recover_forget",
    "recover_cached_result",
    "recover_controller_operation",
)

if "forget.pending" not in identity:
    raise SystemExit("forget journal path is missing")
if "wmcs_identity_forget_begin" not in identity:
    raise SystemExit("forget journal begin operation is missing")
if "wmcs_identity_forget_finish" not in identity:
    raise SystemExit("forget journal finish operation is missing")

orphan = section(
    ubus,
    "static int forget_orphan(",
    "static int wlan_sync_stop(",
)
for forbidden in (
    "wmcs_identity_delete_peer",
    "wmcs_result_store_remove",
):
    if forbidden in orphan:
        raise SystemExit(f"ubus orphan cleanup bypasses the durable journal: {forbidden}")
if "wmcs_control_forget_peer" not in orphan:
    raise SystemExit("ubus orphan cleanup must use the control journal path")

print("Durable forget journal contract: ok")
