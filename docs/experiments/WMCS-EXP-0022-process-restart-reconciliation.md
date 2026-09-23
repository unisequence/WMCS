# WMCS-EXP-0022 — hardware process-restart and reconciliation boundaries

Status: observed; physical power-cut, ENOSPC, and exact mid-UCI boundaries
remain pending

## Question

Does the exact Globitel BT-RB300 / Cudy TR3000 pair preserve the durable WMCS
operation contract across clean daemon restarts, a controller operation left
pending without a receiver, explicit reconciliation after restart, and an
agent restart after a completed authenticated result?

## Topology and baseline

- Controller: Globitel BT-RB300 at `192.168.1.1`, daemon `0.2.0-r3`
- Agent: Cudy TR3000 v1 at `192.168.1.2`, OpenWrt `25.12.5`, daemon `0.2.0-r4`
- Baseline before this experiment: generation `82`, active peer on both sides
- Controller wireless export SHA-256:
  `b7e0a6fa6ad75cd6dfb91560671b590fe45d152a06774f1fb78a4f170b5a6b36`
- Cudy wireless export SHA-256:
  `dcbc165ea344867f70ea550a0a5e1b62a750deca23c8cebb75269e3c1c7fb728`
- No SAS, WLAN key, private key, or raw control payload was recorded.

## Case A — clean daemon restart

Both `wmcsd` services were restarted while idle. After recovery:

- controller and agent remained paired with active peer records;
- generation stayed `82` on both nodes;
- controller and Cudy wireless hashes were unchanged;
- Cudy r4 returned `ubus_connected=true`, `degraded=false`, and one active
  controller;
- runtime `phy1-ap0` remained up as the expected 5 GHz AP.

## Case B — controller pending operation and explicit resume

1. A fresh candidate was discovered on both bounded windows.
2. The controller started `wlan_sync_start` with `dry_run=true` and a closed
   agent control window. The request became sequence `4`,
   `reconciliation_pending=true`; the agent stayed idle and its UCI hash did
   not change.
3. The controller service was restarted before any receiver was opened. After
   boot it exposed `control_state=recovery_pending`, operation `wlan_sync`,
   sequence `4`, and did not transmit automatically.
4. A fresh discovery and a new bounded agent control window were opened. The
   administrator repeated the same `wlan_sync_start` operation with the same
   peer and `dry_run=true`.
5. The preserved operation resumed at sequence `4` and completed as
   `dry_run_ready` on both sides. Generation advanced to `83`; both wireless
   hashes remained unchanged.

This verifies the important safety boundary: restart does not silently send a
stored mutation to an old address, while an explicit fresh observation can
resume the exact operation.

## Case C — agent restart after completed result

1. A new authenticated WLAN dry-run completed at sequence `5` with
   `dry_run_ready`; no UCI mutation occurred.
2. The Cudy agent was restarted while its durable `control.result` existed and
   after both sides had completed the peer transition.
3. After startup, the Cudy reported generation `84`, one active controller,
   `ubus_connected=true`, `degraded=false`, and an idle control state. Its peer
   generation was `5`, proving that the completed result was not replayed as a
   second operation.
4. The Cudy wireless hash stayed at the baseline value and the protected
   `control.result` remained present for the latest authenticated result.

## Final state

The controller and agent were paired and idle at generation `84`. The Cudy
managed WLAN remained present on `radio1`, `phy1-ap0` was up, and both
configuration hashes were unchanged from the baseline. No discovery, pairing,
control, release, WLAN, or forget operation remained active.

## Interpretation

Process-level restart and explicit controller reconciliation now have direct
hardware evidence on the exact pair. The tests are intentionally limited to
dry-run operations so that the recovery boundary itself cannot remove the
working AP.

The following remain open and require a separately armed maintenance window:

- power loss after `wireless.pending` backup and during UCI reload;
- power loss after UCI deletion but before durable result persistence;
- ENOSPC/read-only filesystem behavior at each atomic write;
- live release rollback and 24-hour observation.

## Physical fault runbook

Before a physical fault case, capture controller/agent status, peer records,
wireless hashes, runtime AP, default routes, and protected state filenames.
Use an independent management path. Arm only one case at a time, label the
intended durable boundary, and physically remove power from the named router.
After boot, wait for network recovery, verify the exact hash and runtime AP,
then inspect `wireless.pending`, `control.operation`, `control.result`, and
`forget.pending` before allowing any new mutation. Restore the known-good
paired state after every case and do not combine a power cut with a firmware or
package change.
