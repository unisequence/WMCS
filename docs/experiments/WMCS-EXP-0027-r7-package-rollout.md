# WMCS-EXP-0027: r7 package rollout to the two-router lab

Date: 2026-09-26. Scope: package installation and immediate non-roaming smoke;
not a roaming transition experiment.

## Packages and preparation

- BT-RB300 (`192.168.1.1`, controller, OpenWrt SNAPSHOT
  `d019f0b2e3`): `wmcsd-0.2.0-r7.apk` built in the matching BT-RB300
  buildroot, SHA-256
  `1062de6442420cb5e59fac7c854ad422946ece6249d73f48d0de520805c24afc`.
- Cudy TR3000 (`192.168.1.2`, agent, OpenWrt 25.12.5):
  `wmcsd-0.2.0-r7.apk` built in the exact 25.12.5 MediaTek Filogic SDK,
  SHA-256
  `2318b311e020bca41d82636eb344cad7ce5d1bf94e9196696530ae163e16db99`.
- Both received `luci-app-wmcs-0.2.0-r6.apk` and
  `luci-i18n-wmcs-ru-0.2.0-r6.apk`. The LuCI packages were built in the
  local upstream tree and are architecture-independent. Cudy's prior LuCI
  packages used date-based version numbers, so APK described their replacement
  as a downgrade despite the new view containing `force_after_timeout`.
- A root-only temporary archive of the installed WMCS files and UCI config
  was made on each router before installation. Package SHA-256 values were
  rechecked on-device. `apk add -s --no-network --allow-untrusted` proposed
  exactly the three named package changes on each router.

## Result

The Cudy update completed first; the BT-RB300 update completed after the
agent's smoke check. Both now report `wmcsd 0.2.0-r7`, the new
`force_after_timeout` and `force_trigger_dbm` status fields, transactional
runtime state, generation 84, an active reciprocal peer, and idle WLAN-sync
operation. The SHA-256 of `/etc/config/wmcs` on each router exactly matched
its pre-install backup. No WLAN settings, policy enable flag, or force flag
were changed by the rollout. One associated station remained visible on the
BT-RB300 after its package upgrade; Cudy had no associated station in this
check. The user-facing Wi-Fi continuity was not independently packet-probed.

The roaming policy and forced-disassociation option are both **off**. Both
local hostapd Neighbor Report lists were empty before rollout. BT-RB300 still
uses `wpad-basic-mbedtls` and does not expose `bss_transition_request`;
Cudy uses full `wpad-mbedtls` and does expose it. Therefore this installation
does not demonstrate automatic roaming or make the BT-RB300 a BTM-capable
source AP. A controlled Cudy-source test requires a valid Neighbor Report,
policy activation, and a client associated to Cudy. Force should remain off
for that first live test.
