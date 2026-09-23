# WMCS-EXP-0021 — ten-cycle normal lifecycle soak

Status: observed; physical power-cut, ENOSPC, and mid-UCI fault boundaries
remain open

## Question

Does the normal WMCS lifecycle remain stable across ten consecutive cycles on
the exact Globitel BT-RB300 / Cudy TR3000 pair, including release, local trust
cleanup, fresh pairing, WLAN dry-run, live WLAN recreation, and a final daemon
restart?

## Provenance and harness

- Researcher: repository owner and Codex implementation assistant
- Date and timezone: 2026-09-22, Europe/Moscow
- Controller: Globitel BT-RB300 at `192.168.1.1`, daemon `0.2.0-r3`
- Agent: Cudy TR3000 v1 at `192.168.1.2`, OpenWrt `25.12.5`, exact daemon
  package `0.2.0-r4`
- Harness: [`tools/lab/lifecycle-soak.sh`](../../tools/lab/lifecycle-soak.sh)
- Run command: `bash tools/lab/lifecycle-soak.sh`
- Cycle count: `10`
- No SAS, WLAN key, private key, or raw control payload was printed or stored.

The harness uses bounded discovery and control windows, verifies every result
on both sides, compares the controller and agent sequence numbers, checks
wireless export hashes, and stops all bounded windows before the next phase.
Discovery announcements are allowed a bounded six-second observation period.

## Per-cycle procedure

Each cycle performed all of the following:

1. Fresh discovery and release dry-run; verify `dry_run_ready` and byte-level
   wireless export preservation by SHA-256.
2. Fresh discovery and live release; verify `committed`, matching sequence,
   disappearance of the owned section, and the expected post-release hash.
3. Close control windows and call `forget_orphan` independently on both nodes;
   verify empty peer lists and unchanged post-release UCI.
4. Fresh discovery and new pairing; compare SAS internally, confirm both sides,
   and verify the original peer identities return active.
5. Fresh WLAN dry-run; verify `dry_run_ready` and no UCI mutation.
6. Fresh live WLAN sync; verify `committed`, matching sequence, owner marker,
   `radio1`, and the expected restored hash.
7. Verify both daemons are idle, paired, and free of reconciliation state.

## Result

All ten cycles passed:

| Check | Result |
|---|---|
| Release dry-run | 10/10 `dry_run_ready`; no wireless hash mutation |
| Live release | 10/10 `committed`; each new relationship used sequence `5` |
| Local forget | 20/20 independent calls returned `forgotten=true` |
| Fresh pairing | 10/10 matching SAS confirmations; SAS values not recorded |
| WLAN dry-run | 10/10 `dry_run_ready`; no wireless hash mutation |
| Live WLAN sync | 10/10 `committed`; each new relationship used sequence `3` |
| Final state checks | 10/10 paired, active, idle, no reconciliation pending |

The harness then restarted the exact Cudy `wmcsd` service and repeated the
final state checks successfully.

## Final state

- Both daemons reported generation `82`.
- The controller peer and agent peer were active; each side reported one peer.
- Controller wireless export SHA-256 remained
  `b7e0a6fa6ad75cd6dfb91560671b590fe45d152a06774f1fb78a4f170b5a6b36`.
- Cudy wireless export SHA-256 remained
  `dcbc165ea344867f70ea550a0a5e1b62a750deca23c8cebb75269e3c1c7fb728`.
- Cudy reported `ubus_connected=true`, `degraded=false`, and mutation
  available.
- `wireless.wmcs_home_5g` was present with `wmcs_managed=1`, the expected
  controller owner, and target `radio1`.
- Runtime `phy1-ap0` was up as the expected 5 GHz AP.
- Controller protected state contained only identity and generation files.
  The Cudy contained those plus its expected durable `control.result`; no
  release, WLAN, or forget pending marker remained.

## Interpretation

The normal lifecycle is stable for ten consecutive process-level cycles on the
exact tested pair. Release and recreation repeatedly returned the agent to the
same managed WLAN digest, and the final restart preserved the active mesh
relationship and runtime AP.

This is process/restart soak evidence, not a power-loss guarantee. Physical
power-cut timing, ENOSPC, mid-UCI rollback, 24-hour observation, and broader
platform coverage remain separate release gates.
