# WMCS Compatibility Matrix

Status values: `planned`, `observed`, `partial`, `supported`, `regressed`, or
`unsupported`. Only `supported` is a compatibility claim, and it requires linked
repeatable evidence.

## Controllers

| Platform | Version | Target | Role | Status | Evidence | Notes |
|---|---|---|---|---|---|---|
| OpenWrt | 24.10.x | MediaTek Filogic / aarch64 | WMCS controller | planned | — | Initial toolchain target |
| OpenWrt | 24.10.x | MT7621 / mipsel_24kc | WMCS controller | planned | — | Subject to footprint spike |
| OpenWrt | 25.12.x | MediaTek Filogic / aarch64 | WMCS controller | planned | — | Initial APK target |
| OpenWrt | 25.12.x | MT7621 / mipsel_24kc | WMCS controller | planned | — | Subject to footprint spike |
| OpenWrt | SNAPSHOT `r0+36056-d019f0b2e3` | Globitel BT-RB300 / MediaTek Filogic | Native WMCS controller pilot | observed | [EXP-0001](../experiments/WMCS-EXP-0001-globitel-read-only-runtime.md), [EXP-0002](../experiments/WMCS-EXP-0002-globitel-package-install.md), [EXP-0003](../experiments/WMCS-EXP-0003-two-router-bounded-discovery.md), [EXP-0004](../experiments/WMCS-EXP-0004-two-router-authenticated-pairing.md), [EXP-0005](../experiments/WMCS-EXP-0005-two-router-one-wlan-sync.md) | Discovery, paired control, transactional one-WLAN sync and real reboot persistence observed; not a supported release |

## Agents

No controller/agent device pair is currently `supported`. The rows below are
bounded observations against named hardware, firmware, role, and evidence.

| Platform | Model / target | Firmware | Backhaul | Capability | Status | Evidence |
|---|---|---|---|---|---|---|
| OpenWrt | MediaTek Filogic / aarch64 | 24.10.x or 25.12.x | Ethernet | Native WMCS agent MVP | planned | — |
| OpenWrt | Cudy TR3000 v1 / MediaTek Filogic | 25.12.5 `r33051-f5dae5ece4` | Ethernet | Native WMCS one-WLAN agent pilot | observed | [EXP-0003](../experiments/WMCS-EXP-0003-two-router-bounded-discovery.md), [EXP-0004](../experiments/WMCS-EXP-0004-two-router-authenticated-pairing.md), [EXP-0005](../experiments/WMCS-EXP-0005-two-router-one-wlan-sync.md), [EXP-0019](../experiments/WMCS-EXP-0019-exact-cudy-package-live-sync.md) |
| OpenWrt | MT7621 / mipsel_24kc | 24.10.x or 25.12.x | Ethernet | Native WMCS agent MVP | planned | — |

Keenetic MWS interoperability is not part of the first MVP. It will receive
separate controller/member rows only when its adapter research begins.

## Capability status

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
| 802.11k/v roaming assistance | partial | [EXP-0007](../experiments/WMCS-EXP-0007-hostapd-roaming-capabilities.md) shows the basic-wpad boundary; [EXP-0008](../experiments/WMCS-EXP-0008-full-wpad-package-gate.md) proves full-wpad BTM on both nodes and exact live rollback on Globitel; [EXP-0009](../experiments/WMCS-EXP-0009-runtime-kv-neighbor-pilot.md) observes runtime k/v and reciprocal Neighbor Reports; [EXP-0010](../experiments/WMCS-EXP-0010-client-kv-steering-pilot.md) records measurement limits, fail-closed gates, stale-source handling, and a repaired candidate; [EXP-0011](../experiments/WMCS-EXP-0011-accepted-advisory-btm.md) observes one status-0 advisory transition; [EXP-0012](../experiments/WMCS-EXP-0012-passive-roundtrip-continuity.md) demonstrates client stickiness; [EXP-0013](../experiments/WMCS-EXP-0013-strict-source-gate-calibration.md) shows a strict source-only gate failing closed; [EXP-0014](../experiments/WMCS-EXP-0014-natural-handoff-continuity.md) captures one natural non-FT handoff with one lost 100 ms probe; [EXP-0015](../experiments/WMCS-EXP-0015-reverse-passive-stickiness.md) observes reverse-direction stickiness and zero movement-path loss; [EXP-0016](../experiments/WMCS-EXP-0016-assisted-reverse-continuity.md) joins one status-0 advisory request, intended-target transition, and one lost 100 ms probe; [EXP-0023](../experiments/WMCS-EXP-0023-final-roaming-pass.md) records a clean 0%-loss walk with no natural handoff at RSSI down to -76 dBm; owned persistence, daemon integration, assisted-BTM decision, and repetition remain required |
| 802.11r | planned | Separate opt-in evaluation |
| Coordinated updates | planned | Only after signed release path and rollback |

## Evidence rule

Every `observed`, `partial`, `supported`, or `regressed` row must link to an
experiment record containing topology, exact versions, initial-state hashes,
capture hashes, expected and observed behavior, and recovery results.
