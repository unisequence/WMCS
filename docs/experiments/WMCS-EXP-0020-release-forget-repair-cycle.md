# WMCS-EXP-0020 — exact Cudy release, forget, re-pair, and repair cycle

Status: observed; process restart passed, physical power-cut and filesystem
fault boundaries remain open

## Question

Can the exact Cudy 25.12.5 target complete the full ownership-safe lifecycle:
release the WMCS-owned WLAN, reject a foreign owner, forget the released trust
relationship locally on both sides, pair again, recreate the WLAN through
authenticated control, and preserve the result across a daemon restart?

## Provenance

- Researcher: repository owner and Codex implementation assistant
- Date and timezone: 2026-09-22, Europe/Moscow
- Repository: WMCS current integrated tree
- Controller: Globitel BT-RB300 at `192.168.1.1`, daemon `0.2.0-r3`
- Agent: Cudy TR3000 v1 at `192.168.1.2`, OpenWrt `25.12.5`
- Agent package: exact `wmcsd-0.2.0-r4.apk` built with the official 25.12.5
  MediaTek Filogic SDK; packaged binary SHA-256
  `3fce827312e2bdd2f06df5045f22f8a1f1830908b35ce017a7c2cbbe64e803e7`
- No SAS, WLAN key, private key, or raw control payload is recorded here.

## Initial state

Both nodes were paired and synchronized before the run. The controller's
wireless export SHA-256 was
`b7e0a6fa6ad75cd6dfb91560671b590fe45d152a06774f1fb78a4f170b5a6b36`; the
Cudy's was
`dcbc165ea344867f70ea550a0a5e1b62a750deca23c8cebb75269e3c1c7fb728`.
The Cudy contained the owned `wireless.wmcs_home_5g` section on `radio1`, with
the expected WMCS owner marker. Both daemons were mutation-enabled and idle.

## Controlled lifecycle

### 1. Release dry-run and ownership guard

1. Fresh bounded discovery retained an ephemeral Cudy candidate.
2. `release_start` with the matching owner and `dry_run=true` completed at
   sequence `5` as `dry_run_ready`; neither wireless export changed.
3. The Cudy owner marker was temporarily replaced with a synthetic foreign
   fingerprint without reloading wireless. The foreign export hash was
   `d34fcbed63e22ccd5dc6ec349b76892069a53f2c4cd0896ac140246d67ce6cda`.
4. A second release dry-run completed at sequence `6` as
   `rejected / ownership_conflict` on both sides. The foreign marker and hash
   remained unchanged.
5. The expected controller owner was restored; the Cudy export returned exactly
   to `dcbc165ea344867f70ea550a0a5e1b62a750deca23c8cebb75269e3c1c7fb728`.

### 2. Live release

With the bounded agent control window open, the controller repeated the exact
release request with `dry_run=false`:

- controller and agent both returned sequence `7`, `outcome=committed`,
  `reason=none`;
- both peer records became durable `released` records at generation `18`;
- the controller wireless hash stayed
  `b7e0a6fa6ad75cd6dfb91560671b590fe45d152a06774f1fb78a4f170b5a6b36`;
- the Cudy-owned section disappeared and its post-release wireless hash was
  `82718b7c06a628b05b830fbff1867213812798194d961a6d0969d36afef54c30`;
- no reconciliation remained after the operation was stopped cleanly.

### 3. Local trust cleanup

After the release result was durable and all bounded windows were closed,
`forget_orphan` was run independently:

- controller: `forgotten=true`, generation `19`;
- agent: `forgotten=true`, generation `19`;
- both peer lists became empty;
- the Cudy wireless hash stayed at the post-release value; no UCI edit was
  performed by forget;
- no `forget.pending` marker remained afterward.

### 4. Re-pair and WLAN repair

The same two nodes were rediscovered and paired through a new bounded SAS
exchange. The SAS was compared and confirmed on both devices without recording
it. Pairing completed at generation `20`; both peer relationships were active.

The controller then performed a fresh authenticated WLAN cycle:

1. WLAN dry-run completed at sequence `2` with `dry_run_ready`; the Cudy
   remained without the managed section and retained hash
   `82718b7c06a628b05b830fbff1867213812798194d961a6d0969d36afef54c30`.
2. Live WLAN sync completed at sequence `3` with `committed` on both sides.
3. The Cudy recreated only `wireless.wmcs_home_5g` on `radio1`, with
   `wmcs_managed=1` and the expected controller owner. Its export returned to
   `dcbc165ea344867f70ea550a0a5e1b62a750deca23c8cebb75269e3c1c7fb728`.

## Restart and final state

The exact Cudy `wmcsd` service was restarted after the repair. Final evidence:

- Cudy daemon `0.2.0-r4`, `ubus_connected=true`, `degraded=false`, mutation
  available, generation `22`;
- controller daemon `0.2.0-r3`, generation `22`, active Cudy peer;
- both peer records were active and both control states were idle with no
  reconciliation pending;
- runtime `phy1-ap0` was up as the expected 5 GHz AP;
- controller wireless hash remained
  `b7e0a6fa6ad75cd6dfb91560671b590fe45d152a06774f1fb78a4f170b5a6b36`;
- Cudy wireless hash remained
  `dcbc165ea344867f70ea550a0a5e1b62a750deca23c8cebb75269e3c1c7fb728`;
- protected state contained only expected identity/generation data on the
  controller and the durable latest agent result on the Cudy; no pending
  release, WLAN, or forget marker remained.

## Interpretation

This exact pair now has observed evidence for the complete normal lifecycle:
ownership guard, live release, local trust cleanup, fresh adoption, live WLAN
recreation, and post-restart persistence. The foreign-owner path preserved the
configuration byte-for-byte at the export level, and the final managed WLAN
returned to its exact pre-release digest.

This does not close the physical power-cut/ENOSPC matrix, mid-UCI rollback
boundaries, repeated-cycle soak, or broad platform support. Those remain
separate release gates; the normal lifecycle is not being represented as a
power-loss guarantee.
