# WMCS-EXP-0032: BT-RB300 5 GHz BSS restart recovery

Date: 2026-09-26. Scope: one targeted radio down/up on the paired BT-RB300
controller and Cudy TR3000 v1 agent. No client walk, steering request, router
reboot, or global hostapd process restart was attempted. Both nodes ran the
exact `wmcsd 0.2.0-r10` packages recorded in [EXP-0031](WMCS-EXP-0031-r10-raw-neighbor-guard.md).

## Preflight and action

The workstation reached BT `192.168.1.1` via `enp10s0`; its Internet default
route was via `192.168.64.1 dev enp9s0`. BT `radio0` was 2.4 GHz and carried
the Cudy backhaul AP. BT `radio1` was 5 GHz and carried both the home AP
`phy1-ap0` and a backup Wi-Fi WAN STA `phy1-sta0`. BT's primary default route
was through wired `wan` (metric 10), with the STA backup at metric 30. Cudy's
backhaul STA was on its `radio0` and associated. The user approved a short
BT 5 GHz interruption; no Cudy radio was restarted.

Before the action, both WMCS instances reported generation 84, one paired
peer, `neighbor_sync_state=ready`, one authenticated neighbor, zero apply
failures, steering disabled, and forced disassociation disabled. BT's
`hostapd.phy1-ap0` was `ENABLED`, with one Neighbor Report. Its ubus object ID
was `ad38dde4`; BT's 2.4 GHz AP object ID was `f92e595c`.

Only `network.wireless down {"device":"radio1"}` followed by a short pause
and `network.wireless up {"device":"radio1"}` was issued on BT. This uses the
radio-specific ubus method, not `wifi up radio1`, whose installed script also
calls a global network reload. A shell exit trap requested `radio1` up if
the action exited early. During the down window, `radio1.up=false` and
`radio0.up=true` were observed. The hostapd AP object still appeared in one
sample taken immediately after `radio1.up=false`, so that sample alone does
not establish the precise BSS teardown time.

## Result

After the up request, BT `radio1.up=true`, its AP reported `ENABLED` on channel
149, and the 5 GHz backup STA interface `fnwwan` was up again. The
`hostapd.phy1-ap0` ubus object ID changed to `b44d0cc4` and its RRM/WNM
counters reset; the 2.4 GHz AP object ID stayed `f92e595c`. The global
hostapd process PID remained 3409. Thus the 5 GHz BSS was recreated, but
this is **not** evidence of a whole-process hostapd restart.

Both WMCS daemons returned `neighbor_sync_state=ready`, one authenticated
neighbor, one reciprocal runtime Neighbor Report, and zero apply failures.
Each runtime list contained the other AP's **exact current own report**, with
no extra entry. The r10 refresh path calls hostapd `bss_mgmt_enable` before
reading the own report and reconciling the list; its successful `ready` state
after the new AP object is evidence that this call succeeded. We did not
independently capture over-the-air 802.11k/v advertisement or trigger BTM.
The Cudy backhaul STA remained associated when checked after the action;
no continuous packet-loss trace was captured during the brief down window.

Both nodes still reported `degraded=false`, generation 84, and one paired
peer. The primary BT default route remained wired `wan`, the backup Wi-Fi WAN
route returned, and the workstation Internet route was unchanged. The
`/etc/config/wireless` and `/etc/config/wmcs` SHA-256 values were unchanged:

| Node | wireless | wmcs |
|---|---|---|
| BT-RB300 | `6f5cb6bc766e1488abadbe7109f24be14d1485e430e069eccc52459b6f6359f9` | `01b0507cf15672dadebc05f38edd44b9584c19e39cd79a19af4f1201c95f3e05` |
| Cudy | `a442b743b552f5dc2e53230fddd975830a38525adcc1aa77f713f24949d7817c` | `1d705b6f1944814975d566f13fc14546c1f5f20e7e17f1c48602ea1bee461c68` |

This closes the **BT 5 GHz BSS recreation** recovery case only. Cudy AP
restart, full hostapd process restart, router reboot/power interruption,
live Cudy INT8 BTM response handling, and repeatable roaming quality remain
open. No seamlessness or supported-release claim follows from this result.
