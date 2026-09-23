#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path


source = Path("src/wmcsd/control.c").read_text(encoding="utf-8")


def section(start: str, end: str) -> str:
    first = source.find(start)
    last = source.find(end, first + len(start))
    if first < 0 or last < 0:
        raise SystemExit(f"controller journal contract section is missing: {start}")
    return source[first:last]


def ordered(body: str, *tokens: str) -> None:
    position = -1
    for token in tokens:
        current = body.find(token, position + 1)
        if current < 0:
            raise SystemExit(f"controller journal contract token is missing: {token}")
        if current <= position:
            raise SystemExit(f"controller journal ordering is invalid at: {token}")
        position = current


start = section(
    "static int start_controller_operation(",
    "int wmcs_control_sync_start(",
)
ordered(
    start,
    "start_common(control, duration_seconds)",
    "wmcs_operation_store_save_pending",
    "control->journal_phase = WMCS_OPERATION_PENDING",
    "send_packet(control, &control->request",
)

result = section(
    "static int process_controller_result(",
    "static void receive_packets(",
)
ordered(
    result,
    "decode_result_payload",
    "wmcs_operation_store_save_complete",
    "reconcile_controller_peer",
    "control->journal_phase = WMCS_OPERATION_COMPLETE",
    "control->state = WMCS_CONTROL_COMPLETE",
)

recovery = section(
    "static int recover_controller_operation(",
    "static void complete_agent_wlan_operation(",
)
for token in (
    "wmcs_operation_store_load",
    "decode_request_payload",
    "WMCS_CONTROL_RECOVERY_PENDING",
    "reconcile_controller_peer",
):
    if token not in recovery:
        raise SystemExit(f"controller recovery invariant is missing: {token}")
if "send_packet" in recovery or "open_socket" in recovery:
    raise SystemExit("startup recovery must not transmit or open a control socket")

resume = section(
    "static int resume_controller_operation(",
    "static int start_controller_operation(",
)
ordered(
    resume,
    "parse_peer_address",
    "start_common(control, duration_seconds)",
    "send_packet(control, &control->request",
)

close = section("void wmcs_control_close(", "int wmcs_control_forget_peer(")
if "wmcs_operation_store_remove" in close:
    raise SystemExit("daemon shutdown must preserve the controller journal")

cached = section("static int load_cached_result(", "static int finalize_cached_result(")
ordered(
    cached,
    "memcmp(response->recipient_id, peer_binary",
    "response->sequence != expected_sequence",
    "response->type != expected_type",
    "wmcs_control_open",
)

print("Durable controller journal contract: ok")
