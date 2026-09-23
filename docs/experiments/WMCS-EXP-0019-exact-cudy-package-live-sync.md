# WMCS-EXP-0019 — exact Cudy 25.12.5 package and live one-WLAN sync

Status: observed; exact package and live commit passed, physical power-cut and
mutation rollback boundaries remain open

## Question

Can the WMCS APK built with the official OpenWrt 25.12.5 MediaTek Filogic SDK
run on the Cudy TR3000 v1, preserve its existing paired state, complete a
dry-run, resume a live request after an expired control window, commit the
owned WLAN transaction, and survive a daemon restart?

## Provenance

- Researcher: repository owner and Codex implementation assistant
- Date and timezone: 2026-09-22, Europe/Moscow
- Repository: WMCS current integrated tree
- SDK: OpenWrt 25.12.5 MediaTek Filogic, GCC 14.3.0 musl
- SDK archive SHA-256: `ff4a38a397caa2cfe1c39e18f84ddede14878221b3593c3f2c4cfe24e3ec4c25`
- SDK feed base commit: `f0a60eee2fe051741c643ea6118718aae1ef17fb`

## Topology

```text
development host
      |
      +--- controller: Globitel BT-RB300, 192.168.1.1, WMCS 0.2.0-r3
      |
      +--- agent: Cudy TR3000 v1, 192.168.1.2, OpenWrt 25.12.5,
                 exact WMCS APK 0.2.0-r4
```

The existing authenticated relationship was retained. The controller peer ID
for the Cudy was `7cab6f2d383f4f4bd1e5f6301a8df160`; the Cudy's controller peer
ID was `e10745f50aede398cd0995287fadfe85`.

## Package evidence

The package was built with the exact release SDK and passed the repository
package checker. The packaged ELF carries the target ABI names:

- `libubus.so.20251202`
- `libubox.so.20260213`
- `libuci.so.20250120`
- `libmbedcrypto.so.16`

The package was copied to Cudy with legacy SCP because the target does not ship
an SFTP server. Dependencies were already installed. The package was installed
with networking disabled; the previous binary, init script, wrapper, and UCI
configuration were backed up first. The user configuration was restored after
installation, while the package's exact init script was activated.

The exact packaged binary first passed the read-only target smoke with:

- daemon version `0.2.0-r4`;
- role `agent`;
- `mutation_enabled=false` in the temporary smoke process;
- `ubus_connected=true`, `degraded=false`;
- managed wireless SHA-256 unchanged:
  `41c1c818754621e71816ab6c59856beb482d0c5d1ca9faa34d7c2edb824fe3a3`.

## Controlled protocol run

1. Discovery found the Cudy at `192.168.1.2` as an ephemeral untrusted agent.
2. Agent `control_listen` opened a 60-second window.
3. Controller `wlan_sync_start` with `dry_run=true` completed at sequence `3`
   with `outcome=dry_run_ready`.
4. The same operation was started live with `dry_run=false`.
5. The first live frame met an expired control window. The controller retained
   sequence `4` as `reconciliation_pending`; no Cudy UCI change occurred.
6. A new agent control window was opened. The controller resumed the same
   durable request after discovery; it did not create a new operation.
7. The controller completed sequence `4` with `outcome=committed` and
   `reason=none`.

## State and recovery evidence

- Controller wireless SHA-256 before/after: `b7e0a6fa6ad75cd6dfb91560671b590fe45d152a06774f1fb78a4f170b5a6b36`.
- Cudy wireless SHA-256 before/after: `dcbc165ea344867f70ea550a0a5e1b62a750deca23c8cebb75269e3c1c7fb728`.
- Cudy `wireless.wmcs_home_5g` remained owned by the controller fingerprint,
  on `radio1`, enabled, and present after commit.
- Cudy restart after the live commit changed PID `21384 -> 21528` while
  preserving daemon `0.2.0-r4`, role `agent`, generation `15`, one paired peer,
  and the wireless SHA-256.
- Controller status after the agent restart retained sequence `4` as
  `committed` with `reconciliation_pending=false`.

## Interpretation

The exact Cudy package is now a real target artifact rather than an ABI
retargeted lab ELF. The asynchronous control path demonstrated the intended
durable behavior: an unavailable receiver did not cause a second operation or
an untracked mutation; reopening the bounded receiver allowed the stored
request to commit exactly once.

This does not close the physical power-loss matrix or prove rollback after a
mid-UCI interruption. Those remain the next fault-injection experiment. The
current run deliberately preserved the existing owned WLAN instead of testing
release or destructive cleanup.
