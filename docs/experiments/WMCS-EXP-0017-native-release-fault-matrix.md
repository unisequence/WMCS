# Experiment WMCS-EXP-0017: Native ownership-safe release fault matrix

Status: partial hardware run; exact-boundary power fault matrix pending

## Question

Does native v0 release remove only the authenticated controller's owned WLAN,
revoke its future mutation authority, reconcile a lost result after restart,
and preserve all unrelated OpenWrt configuration across every tested failure
boundary?

## Provenance

- Researcher: repository owner and Codex implementation assistant
- Date and timezone: 2026-09-21, Europe/Moscow
- Public references: ADR 0009, ADR 0011, native protocol v0, ubus API v0
- Clean-room role: implementation and validation from public OpenWrt APIs
- Data handling classification: public record; raw snapshots remain sensitive
  external artifacts

## Topology

Use the same isolated Ethernet/L2 management topology as EXP-0005:

```text
development host
      |
      +--- controller LAN (Globitel BT-RB300)
      |
      +--- agent LAN (Cudy TR3000 v1)
```

The development host must retain a specific route to the lab subnet and must
not use either router as its default Internet gateway. Verify an independent
recovery path before injecting process or power failures.

## Devices

| Role | Vendor and model | Hardware revision | Firmware / OpenWrt build | MAC alias | Notes |
|---|---|---|---|---|---|
| Controller | Globitel BT-RB300 | record at run | record at run | router-a | Native controller, mutation enabled only for run |
| Candidate/member | Cudy TR3000 v1 | record at run | record at run | router-b | Native agent, target radio recorded |
| Capture host | development workstation | n/a | record kernel/tools | capture-host | Isolated lab interface |

## Initial state

- Start from the known post-EXP-0005 paired, synchronized WLAN state or repeat a
  clean pairing and live sync with the release candidate package.
- Record full redacted snapshots and SHA-256 of `/etc/config/wireless` on both
  nodes before every case.
- Record `wmcs status`, `wmcs identity`, `wlan_sync_status`, peer IDs, package
  version, target radio, runtime BSS, bridge membership, and default routes.
- Add one unrelated wireless section and one unrelated option whose exact bytes
  must survive every case.
- Preserve recovery images/configuration outside Git.
- Independent management path verified: required before execution.

## Controlled action

For each case, rediscover the same agent, open a bounded agent control window,
and perform release dry-run followed—only where specified—by live release.

Run each case from a restored, synchronized starting snapshot:

1. Dry-run with exact ownership; expect no file or runtime change.
2. Replace `wmcs_owner` with a synthetic foreign ID; expect ownership conflict
   and byte-identical configuration.
3. Remove `wmcs_managed`; expect ownership conflict and preservation.
4. Change the owned section's `device`; expect ownership conflict and
   preservation.
5. Live successful release; expect only `wireless.wmcs_home_5g` removed.
6. Kill `wmcsd` after backup creation and before result persistence; restart and
   expect exact rollback.
7. Kill or power-cycle after UCI deletion and before result persistence;
   restart and expect exact rollback.
8. Drop the release result after durable result persistence but before the
   controller receives it; restart the agent, rediscover, retry the same
   release, and expect the original result without another deletion.
9. Kill the controller after `control.operation` is durable and before the
   first request is captured; restart and prove `recovery_pending` causes no
   automatic transmission, then rediscover and resume the exact stored frame.
10. Drop the result after request transmission, restart the controller, and
    prove that rediscovery plus the matching start call sends the exact same
    request bytes and reconciles once.
11. Kill the controller after its completed operation record is durable but
    before local peer generation advances; restart and expect local completion
    from the authenticated record without retransmission.
12. After reconciled commit, attempt `wlan_sync_start` using the old
    relationship; expect rejection/no mutation because the peer is released.
13. Invoke `forget_orphan` while a synthetic owned/pending state names the peer;
    expect refusal and preservation.
14. Remove the synthetic blocker, stop all bounded windows, and invoke
    `forget_orphan` independently on controller and agent; expect only the peer
    and matching result/operation tombstone removed.
15. Interrupt a local forget after `forget.pending` is durable and before any
    record removal; restart and expect the same named cleanup to resume without
    touching UCI.
16. Interrupt after one matching journal record is removed and before the peer
    unlink; restart and expect idempotent completion for that peer only.
17. Interrupt after the peer unlink and before `forget.pending` removal; restart
    and expect the marker to finish after confirming no owned or pending state.
18. Replace the marker with a malformed or generation-mismatched record; expect
    startup to remain fail-closed and no peer, journal, or UCI state to be
    consumed.

Every bounded operation uses a 30-second deadline. Record the exact injected
point from filesystem state and process/log timestamps rather than sleep timing
alone.

## Expected observation

- Release request/result types and sequences match on both devices.
- Dry-run returns `dry_run_ready`; live success returns `committed`.
- A conflicting or partial marker never authorizes deletion.
- A pre-result crash restores the exact wireless file and runtime AP.
- A durable lost result advances/reconciles once and never repeats mutation.
- Controller startup never transmits a pending mutation without explicit
  rediscovery and resume.
- Resumed request bytes, operation type, sequence, nonce, and ciphertext are
  identical to the durable pending frame.
- A completed controller record repairs a missing local peer transition and
  preserves the exact result across restart.
- A committed peer reports no active controller authority on the agent.
- The old relationship cannot perform WLAN sync.
- Local forgetting refuses owned or pending state and never changes UCI.
- A partial local forget resumes from its generation-bound marker, removes only
  matching records, and leaves malformed or stale cleanup state untouched.
- Unrelated sections/options, management addressing, bridge, DHCP, firewall,
  default routes, and the development host's Internet route remain unchanged.

## Captures and artifacts

| Artifact | Storage reference | SHA-256 | Sanitized fixture committed? |
|---|---|---|---|
| Control packet capture | assign at run | | no |
| Controller logs/status timeline | assign at run | | no |
| Agent logs/status timeline | assign at run | | no |
| Before/after/failure snapshots | assign at run | | no |
| Wireless file hashes and redacted diffs | assign at run | | no |

## Observed behavior

Partial run completed on the paired Globitel BT-RB300 and Cudy TR3000:

- The controller upgraded from `0.2.0-r1` to APK `0.2.0-r3`. The Cudy's
  25.12.5 package ABI did not accept that snapshot-built APK, so the same
  source was cross-built against the 24.10 ABI and run through the existing
  private runtime aliases. Its read-only smoke test passed without changing
  UCI; the installed lab binary was `bc601ff4b51f186c948fe2751a48e182e24e05bf1e232a1acac0f8a56b95c714`.
- Release dry-run completed as `dry_run_ready` at sequence 6 without changing
  the wireless digest. Live release completed as `committed` at sequence 7;
  both peers became `released`, the Cudy-owned AP disappeared, and no pending
  transaction remained.
- Local `forget_orphan` completed independently on both sides with
  `forgotten:true`; both peer lists became empty and both local generations
  advanced to 8. The guard path was also exercised while the owned AP existed:
  it refused cleanup and did not create `forget.pending`.
- A valid generation-bound `forget.pending` was injected on the controller and
  the daemon was restarted. Startup consumed the marker, removed the named
  peer, preserved the controller wireless digest, and left no marker.
- The asymmetric orphan left on the agent was resolved by removing only the
  previously verified WMCS-owned section, after which local forgetting
  completed. A fresh SAS pairing and live WLAN sync then recreated the owned
  AP; the final sync returned `committed` at sequence 2.
- Both daemons were restarted after the final sync. The AP, peer records,
  generation 2 relationship state, and management connectivity survived. The
  final Cudy state reports `wmcs_managed=1`, the expected controller owner, and
  `hostapd.phy1-ap0`. Only the expected agent `control.result` journal remains;
  `forget.pending`, `control.operation`, and `wireless.pending` are absent.

## State diff

- Controller before/after: identity and source wireless digest preserved;
  peer lifecycle moved active → released → forgotten → active.
- Member before/after: the owned AP was deliberately released and recreated;
  final ownership markers and radio selection match the expected profile.
- Unrelated state preserved: measured network management remained reachable and
  the controller wireless digest stayed unchanged.
- Services restarted or interrupted: both daemons restarted several times;
  no router power-cut boundary was injected.

## Failure and recovery

- Failure injected: startup recovery from a durable local forget marker and an
  asymmetric orphan cleanup path.
- Expected rollback: exact pre-transaction wireless file and native reload for
  all failures before durable release commit.
- Observed rollback: release commit and local forget completed; pre-result
  power-cut rollback remains untested.
- Management connectivity preserved: yes during the recorded run.
- Manual recovery required: only the intentionally asymmetric agent-owned AP
  required the documented local owned-section cleanup before forgetting.

## Interpretation

- Conclusion: the durable forget intent and normal release/forget lifecycle
  work on this exact router pair.
- Confidence: medium for process-restart recovery; low for power-cut timing
  boundaries until the complete matrix passes on both routers.
- Alternative explanations: timing-only injection may miss the intended durable
  boundary; verify each point from protected record presence and generation.
- Unknown fields or transitions: concurrent manual UCI edits are outside v0 and
  require a separate field-level journal experiment.
- Follow-up experiment: ten complete clean adoption/reboot/release/forget
  cycles, then a 24-hour soak.

## Compatibility impact

Implementation plus this run moves the matrix entry from `planned` to `partial`.
Successful execution can add observed evidence but does not make release
`supported` until repetition and recovery gates also pass.
