# WMCS-EXP-0031: r10 raw-list guard and package rollout

Date: 2026-09-26. Scope: source safety correction and non-steering package
smoke on the same BT-RB300/Cudy pair as [EXP-0030](WMCS-EXP-0030-r9-paired-neighbor-recovery.md).

The r9 sync reconciler read the complete hostapd Neighbor Report list every
10 seconds, but its 1-second steering poll examined only entries filtered to
the local SSID. A foreign entry with a different SSID could be added between
sync cycles without invalidating the filtered candidate count immediately.
The r10 policy now also counts **all raw list entries** in its pre-BTM check.
It rejects extra or malformed entries, pauses steering, and requests fresh
reconciliation. This is a fail-closed hardening change; no BTM was triggered
to test it on a client in this experiment.

`make check`, the BTM event test, aarch64/mipsel cross-builds, and both exact
OpenWrt package checks passed. The exact APKs were installed from local files
after package-manager dry-runs:

- BT-RB300 `wmcsd 0.2.0-r10` APK SHA-256:
  `6d6d428d8c13db191ad1b7cbad6b1c255735b4f16a240e0e470bd066a8e516a4`.
  Installed ELF and extracted APK payload SHA-256:
  `3dcb1b03a1e03f275d59bf967db0aee80eeca0e36b4db5abb10a2b70ce044778`.
- Cudy `wmcsd 0.2.0-r10` APK SHA-256:
  `d3be04390862ff8f6a70e922488df0208b3a72674f0dc6ea6f6c93d23752ac9c`.
  Installed ELF and extracted APK payload SHA-256:
  `f945e50c3c9419ff30c0a5dcd3e3266c216ef349df7ec427bff5f4b4f06007f9`.

Each daemon was restarted without a Wi-Fi reload. Both reported r10, no
degraded state, generation 84, one paired peer, `neighbor_sync_state=ready`,
one authenticated neighbor, zero neighbor apply failures, advisory steering
off, and forced disassociation off. The r9 exact APKs remain in root-only
`/tmp/wmcs-r9.*` for rollback. The r7 LuCI and Russian packages from EXP-0030
remain installed; their new status fields are present in the installed view.
The old untracked BT buildroot `package/wmcsd` directory was temporarily moved
for exact-package builds and restored afterward.

The `/etc/config/wmcs` and `/etc/config/wireless` SHA-256 values stayed at the
EXP-0030 final values. The workstation Internet route was still via
`192.168.64.1 dev enp9s0`; lab access used `enp10s0`. This smoke does not
exercise the new guard under a live steering decision or prove hostapd
restart/reboot recovery or the Cudy INT8 BTM response path.
