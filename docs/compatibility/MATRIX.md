# WMCS Compatibility Matrix

Status values: `planned`, `observed`, `partial`, `supported`, `regressed`, or
`unsupported`. Only `supported` is a compatibility claim, and it requires linked
repeatable evidence.

The [r6 package build matrix](BUILD_MATRIX.md) records four verified target
artifacts. A successful package build does not establish device runtime or move
the generic target rows below to `supported`.

## Controllers

| Platform | Version | Target | Role | Status | Evidence | Notes |
|---|---|---|---|---|---|---|
| OpenWrt | 24.10.x | MediaTek Filogic / aarch64 | WMCS controller | planned | [r6 build](BUILD_MATRIX.md) | Local 24.10 Filogic IPK built; no 24.10 controller runtime claim |
| OpenWrt | 24.10.x | MT7621 / mipsel_24kc | WMCS controller | planned | [r6 build](BUILD_MATRIX.md) | Isolated 24.10.8 SDK IPK built; no MIPS hardware runtime claim |
| OpenWrt | 25.12.x | MediaTek Filogic / aarch64 | WMCS controller | planned | [r6 build](BUILD_MATRIX.md) | Exact 25.12.5 SDK APK built; no 25.12 controller runtime claim |
| OpenWrt | 25.12.x | MT7621 / mipsel_24kc | WMCS controller | planned | [r6 build](BUILD_MATRIX.md) | Isolated 25.12.5 SDK APK built; no MIPS hardware runtime claim |
| OpenWrt | SNAPSHOT `r0+36056-d019f0b2e3` | Globitel BT-RB300 / MediaTek Filogic | Native WMCS controller pilot | observed | [EXP-0001](../experiments/WMCS-EXP-0001-globitel-read-only-runtime.md), [EXP-0002](../experiments/WMCS-EXP-0002-globitel-package-install.md), [EXP-0003](../experiments/WMCS-EXP-0003-two-router-bounded-discovery.md), [EXP-0004](../experiments/WMCS-EXP-0004-two-router-authenticated-pairing.md), [EXP-0005](../experiments/WMCS-EXP-0005-two-router-one-wlan-sync.md), [EXP-0021](../experiments/WMCS-EXP-0021-ten-cycle-lifecycle-soak.md), [EXP-0022](../experiments/WMCS-EXP-0022-process-restart-reconciliation.md), [EXP-0024](../experiments/WMCS-EXP-0024-recovery-fault-matrix.md) | Discovery, paired control, one-WLAN sync, real reboot persistence, r3/r4 normal lifecycle, and r6 controller service restart observed on the named pair; not a supported release |

## Agents

No controller/agent device pair is currently `supported`. The rows below are
bounded observations against named hardware, firmware, role, and evidence.

| Platform | Model / target | Firmware | Backhaul | Capability | Status | Evidence |
|---|---|---|---|---|---|---|
| OpenWrt | MediaTek Filogic / aarch64 | 24.10.x or 25.12.x | Ethernet | Native WMCS agent MVP | planned | [r6 builds](BUILD_MATRIX.md); exact Cudy observation below does not establish generic target support |
| OpenWrt | Cudy TR3000 v1 / MediaTek Filogic | 25.12.5 `r33051-f5dae5ece4` | Ethernet | Native WMCS one-WLAN agent pilot | observed | [EXP-0003](../experiments/WMCS-EXP-0003-two-router-bounded-discovery.md), [EXP-0004](../experiments/WMCS-EXP-0004-two-router-authenticated-pairing.md), [EXP-0005](../experiments/WMCS-EXP-0005-two-router-one-wlan-sync.md), [EXP-0019](../experiments/WMCS-EXP-0019-exact-cudy-package-live-sync.md), [EXP-0020](../experiments/WMCS-EXP-0020-release-forget-repair-cycle.md), [EXP-0021](../experiments/WMCS-EXP-0021-ten-cycle-lifecycle-soak.md), [EXP-0022](../experiments/WMCS-EXP-0022-process-restart-reconciliation.md), [EXP-0024](../experiments/WMCS-EXP-0024-recovery-fault-matrix.md) |
| OpenWrt | MT7621 / mipsel_24kc | 24.10.x or 25.12.x | Ethernet | Native WMCS agent MVP | planned | [r6 builds](BUILD_MATRIX.md); no MIPS hardware runtime evidence |

The Cudy normal lifecycle and restart/reconciliation results used the exact
25.12.5 r4 agent package. EXP-0024 observes the r6 pair's final healthy state
after a controller service restart; it skipped the r6 agent restart after two
transient parallel SSH resets. These are bounded observations, not a broader
agent or release claim.

Keenetic MWS interoperability is not part of the first MVP. It will receive
separate controller/member rows only when its adapter research begins.

## Capability status

Current non-roaming gate (2026-09-26): [EXP-0021](../experiments/WMCS-EXP-0021-ten-cycle-lifecycle-soak.md)
passed ten consecutive normal release/forget/re-pair/repair cycles on the
Globitel/Cudy pair; [EXP-0022](../experiments/WMCS-EXP-0022-process-restart-reconciliation.md)
passed daemon restart and explicit reconciliation on the r3 controller/r4
agent pair. In [EXP-0024](../experiments/WMCS-EXP-0024-recovery-fault-matrix.md), host
atomic-write/store checks passed and an r6 controller service restart ended
with both routers paired, idle, and unchanged UCI exports; two initial parallel
SSH reads reset, so the r6 agent restart was skipped. A read-only 24-hour
monitor started at 2026-09-26T09:37:58Z and its first sample was healthy; the
24-hour result is pending. Physical power loss, actual router flash ENOSPC or
read-only behavior, and mid-UCI interruption/rollback remain untested. The
Cudy has no UART and controlled power interruption is unavailable.

| Capability | Status | Release gate |
|---|---|---|
| Candidate discovery | partial | Appearance in both window-start orders and window expiry observed in [EXP-0003](../experiments/WMCS-EXP-0003-two-router-bounded-discovery.md); 300-second candidate eviction remains untested on hardware |
| Read-only identity and capabilities | observed | Initial Globitel smoke passed; long-running observation still required |
| Authenticated identity pairing | partial | Mutual signed exchange, matching SAS, wrong-SAS rejection, protected persistence, daemon restart and real router reboot observed in [EXP-0004](../experiments/WMCS-EXP-0004-two-router-authenticated-pairing.md) and [EXP-0005](../experiments/WMCS-EXP-0005-two-router-one-wlan-sync.md); coordinated confirmation and hostile-network tests remain |
| Explicit secure adoption | partial | Bounded discovery, explicit SAS trust, paired encrypted control and mutation gate observed; durable agent results, controller operation reconciliation, and non-authorizing release tombstones are implemented and host/cross-build tested but still need hardware power-cut evidence |
| One home WLAN synchronization | partial | Dry-run, owned apply, runtime verification, exact rollback and unrelated UCI preservation observed in [EXP-0005](../experiments/WMCS-EXP-0005-two-router-one-wlan-sync.md); exact 25.12.5 APK live commit and durable receiver-window resume observed in [EXP-0019](../experiments/WMCS-EXP-0019-exact-cudy-package-live-sync.md); ten-cycle normal lifecycle soak passed in [EXP-0021](../experiments/WMCS-EXP-0021-ten-cycle-lifecycle-soak.md); power-cut matrix remains |
| Reboot persistence | partial | Sequential real controller and agent reboots plus post-reboot authenticated control observed in [EXP-0005](../experiments/WMCS-EXP-0005-two-router-one-wlan-sync.md); exact Cudy `0.2.0-r4` restart after live commit preserved generation, peer, and WLAN state in [EXP-0019](../experiments/WMCS-EXP-0019-exact-cudy-package-live-sync.md); final restart after ten lifecycle cycles passed in [EXP-0021](../experiments/WMCS-EXP-0021-ten-cycle-lifecycle-soak.md); controller `recovery_pending`/explicit resume and agent result preservation passed in [EXP-0022](../experiments/WMCS-EXP-0022-process-restart-reconciliation.md); power-cut matrix remains |
| Release and forget | partial | Operation-typed encrypted release, exact ownership checks, backup/verify/rollback, durable result recovery, authority revocation, and guarded local forget are implemented and cross-build tested; the normal exact-pair release → forget → re-pair → repair lifecycle was observed in [EXP-0020](../experiments/WMCS-EXP-0020-release-forget-repair-cycle.md) and passed ten consecutive cycles in [EXP-0021](../experiments/WMCS-EXP-0021-ten-cycle-lifecycle-soak.md); physical power-cut and preservation boundaries remain in [EXP-0017](../experiments/WMCS-EXP-0017-native-release-fault-matrix.md) |
| Multiple members | planned | Post-MVP |
| VLAN and additional segments | planned | Post-MVP |
| Wireless backhaul | planned | Post-MVP |
| 802.11k/v roaming assistance | partial | [EXP-0007](../experiments/WMCS-EXP-0007-hostapd-roaming-capabilities.md) through [EXP-0029](../experiments/WMCS-EXP-0029-r7-automatic-roundtrip-and-r8-event-fix.md) cover full-wpad, manual and automatic BTM, measurement limits, and the r8 event-width fix. [EXP-0030](../experiments/WMCS-EXP-0030-r9-paired-neighbor-recovery.md) verifies opt-in encrypted reciprocal Neighbor Reports, bounded ownership-safe runtime recovery, daemon restart, peer expiry and rejoin on the named pair. [EXP-0031](../experiments/WMCS-EXP-0031-r10-raw-neighbor-guard.md) records the r10 raw-list guard and package rollout. [EXP-0032](../experiments/WMCS-EXP-0032-btrb-radio1-bss-restart.md) verifies BT's 5 GHz BSS recreation and reciprocal report recovery. Global hostapd process restart, Cudy AP restart, live r10 Cudy BTM response, and repeated client quality trials remain required. |
| 802.11r | planned | Separate opt-in evaluation |
| Automatic two-direction advisory BTM on BT-RB300/Cudy | observed | [EXP-0029](../experiments/WMCS-EXP-0029-r7-automatic-roundtrip-and-r8-event-fix.md) records one r7 measured BTM transition each way. Hostapd accepted both, but r7 WMCS missed Cudy's INT8 response and counted a timeout. The r8 parser correction is still not live-response tested. [EXP-0030](../experiments/WMCS-EXP-0030-r9-paired-neighbor-recovery.md) and [EXP-0031](../experiments/WMCS-EXP-0031-r10-raw-neighbor-guard.md) add paired neighbor recovery and an exact raw-list guard while policies remain off; no repeatable or supported roaming claim. |
| Coordinated updates | planned | Only after signed release path and rollback |

## Evidence rule

Every `observed`, `partial`, `supported`, or `regressed` row must link to an
experiment record containing topology, exact versions, initial-state hashes,
capture hashes, expected and observed behavior, and recovery results.
