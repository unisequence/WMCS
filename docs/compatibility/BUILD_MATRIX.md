# WMCS r6 OpenWrt package build matrix

As of 2026-09-26, `wmcsd 0.2.0-r6` passed the OpenWrt package checker for the
four target builds below. SHA-256 values identify the resulting package files;
the artifacts are local lab outputs, not published releases. Build success
alone does not establish installation, runtime behavior, or `supported` device
compatibility. See the [runtime compatibility matrix](MATRIX.md) for bounded
hardware observations.

| OpenWrt build environment | Target ABI | Package | SHA-256 |
|---|---|---|---|
| 24.10 local MediaTek Filogic buildroot `v24.10.8-8-g85cdef638c` | `aarch64_cortex-a53` | `wmcsd_0.2.0-r6_aarch64_cortex-a53.ipk` | `5f11e2d6fb8167917da5f117ccff148ea24c37f53418541129fae17e2a553c71` |
| Exact 25.12.5 MediaTek Filogic SDK | `aarch64_cortex-a53` | `wmcsd-0.2.0-r6.apk` | `a56cd8e702f7130c9e1ed9bce13c0c6eb6efffa446586ad133062f93938f261a` |
| Isolated 24.10.8 ramips/MT7621 SDK | `mipsel_24kc` | `wmcsd_0.2.0-r6_mipsel_24kc.ipk` | `a2e1c9e4b4e9b3484cffba2c309b3cce73fae7c4771b90b8700f928cecbf6aca` |
| Isolated 25.12.5 ramips/MT7621 SDK with minimal base feed | `mipsel_24kc` | `wmcsd-0.2.0-r6.apk` | `0c73a8c43bdda8cbde4bbf646c50a9d3ee106407fc8db7552e83d056e889ab01` |

The 24.10 Filogic package was checked in the local buildroot (run receipt P1).
Both MT7621 packages were checked in isolated SDKs (run receipt P2); the
25.12.5 shared SDK had unrelated feed Kconfig cycles, so the minimal base-feed
copy was used. All four artifact hashes were independently rechecked. Package
construction and checker usage are documented in
[the OpenWrt package README](../../package/openwrt/README.md).

The exact 25.12.5 Cudy r4 agent package had a live one-WLAN commit and normal
lifecycle with the r3 controller in [EXP-0019](../experiments/WMCS-EXP-0019-exact-cudy-package-live-sync.md),
[EXP-0020](../experiments/WMCS-EXP-0020-release-forget-repair-cycle.md), and
[EXP-0021](../experiments/WMCS-EXP-0021-ten-cycle-lifecycle-soak.md).
[EXP-0024](../experiments/WMCS-EXP-0024-recovery-fault-matrix.md) records a
healthy final r6 pair after a controller service restart, with two transient
SSH resets during parallel sampling. It does not prove that any particular
listed package file was the binary installed on the routers. There is no MIPS
hardware runtime evidence, no 24.10 Filogic runtime claim, and no 25.12.5
Filogic controller runtime claim. The 24-hour observation and physical fault
gates remain pending.

## r7 roaming-policy build addendum (2026-09-26)

The sole-neighbor advisory fallback and disabled-by-default forced-
disassociation option passed host policy tests, both standalone cross-builds,
the OpenWrt package checker, and LuCI/translation package builds. These are
build results, not a live steering or safety claim. The matching Cudy and
BT-RB300 r7 packages were subsequently installed in
[EXP-0027](../experiments/WMCS-EXP-0027-r7-package-rollout.md). Both routers
still have roaming disabled and their local Neighbor Report lists were empty
at the rollout check.

| Build environment | Artifact | SHA-256 |
|---|---|---|
| Exact 25.12.5 MediaTek Filogic SDK (Cudy target) | `wmcsd-0.2.0-r7.apk` | `2318b311e020bca41d82636eb344cad7ce5d1bf94e9196696530ae163e16db99` |
| BT-RB300 matching MediaTek Filogic buildroot | `wmcsd-0.2.0-r7.apk` | `1062de6442420cb5e59fac7c854ad422946ece6249d73f48d0de520805c24afc` |
| Local upstream MediaTek Filogic buildroot | `wmcsd-0.2.0-r7.apk` | `2770991293644c43493553addcfc4fdfee50518c469c80cf1bba0d5bd0770d97` |
| Local upstream MediaTek Filogic buildroot | `luci-app-wmcs-0.2.0-r6.apk` | `df63eb34e84f81d65ad72714137fc4d50fe5554518206915e922fda9719513fc` |
| Local upstream MediaTek Filogic buildroot | `luci-i18n-wmcs-ru-0.2.0-r6.apk` | `cf26d71d722f90aa11f9138625941d6e6043298e40fd152cf66eee52f13ca873` |

## r8 BTM event-width fix addendum (2026-09-26)

The Cudy 25.12 hostapd BTM response notification uses blobmsg `INT8` fields;
the newer BT-RB300 snapshot uses `INT32`. The r8 parser accepts either form
with an octet range check. Its host libubox regression test, aarch64/mipsel
cross-builds, and both exact package checks passed. Both APKs were installed,
their payloads matched the on-device binaries, and the paired daemons reported
`0.2.0-r8` with roaming disabled. The r8 response path has not yet been
retested with a live phone transition; see
[EXP-0029](../experiments/WMCS-EXP-0029-r7-automatic-roundtrip-and-r8-event-fix.md).

| Build environment | Artifact | SHA-256 |
|---|---|---|
| Exact 25.12.5 MediaTek Filogic SDK (Cudy target) | `wmcsd-0.2.0-r8.apk` | `9c1ed5a12de699da965031da8832e5fbe237f17e0d305427a3e5e90fbba4f6d3` |
| BT-RB300 matching MediaTek Filogic buildroot | `wmcsd-0.2.0-r8.apk` | `26bfe867b68fdcba829da817cb488a6c2ce787f852ebeda29a7bba436add629d` |
