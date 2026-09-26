# WMCS-EXP-0030: r9 authenticated neighbor recovery

Date: 2026-09-26. Status: bounded two-node runtime/daemon-restart and peer-loss
observation; **not** a real hostapd restart, reboot, or client roam test.

## Topology and package gate

- BT-RB300 `192.168.1.1`: OpenWrt SNAPSHOT `d019f0b2e3`, controller,
  `wmcsd 0.2.0-r9` from its exact buildroot, APK SHA-256
  `f45d235bfc76466fcca3aa0081022facfbaace558d5e2ca05e1a705d21d02241`.
- Cudy TR3000 v1 `192.168.1.2`: OpenWrt 25.12.5 `r33051-f5dae5ece4`,
  agent, `wmcsd 0.2.0-r9` from the exact SDK, APK SHA-256
  `df526e70634b6c4be8e6f60bd44eb9ec69bf10d63b3ad2cb0a46b7007eb50c77`.
- Full wpad is present on both. The selected 5 GHz hostapd objects each
  exposed `bss_mgmt_enable`, `rrm_nr_get_own`, `rrm_nr_list`, `rrm_nr_set`, and
  `bss_transition_request`. Both own reports were 18 bytes and matched their
  current enabled BSS SSID/BSSID. The existing neighbor lists each contained
  exactly the current reciprocal AP report; no foreign entries were found.
- `luci-app-wmcs 0.2.0-r7` and Russian catalog r7 were installed on both.
  Their APK SHA-256 values are
  `c2f5faa5525c78a803c2e7dc35ee35f5374f303c39e9dc5d579ca95e53a19792`
  and `96e1d20206dd156dd5ceaa85ea55c0c476b40dd67449d24d341a82b6091c7aff`.
  The LuCI packages were built in the BT snapshot tree and are `noarch`.

The workstation reached both lab addresses directly through `enp10s0`; its
Internet route remained `via 192.168.64.1 dev enp9s0`. The package manager
dry-run proposed only the named upgrades. Exact r8 daemon APKs and r6 LuCI
APKs plus pre-upgrade files were staged under root-only `/tmp/wmcs-r9.*` on
each router. Active WMCS configuration survived package install; the new
default was left as `/etc/config/wmcs.apk-new`. Both daemons were explicitly
restarted after installation because the package post-upgrade did not replace
the running r8 process. Both then reported r9, generation 84, one paired peer,
no degraded state, advisory steering off, and forced disassociation off.

## Runtime reconciliation

The opt-in `wmcs.policy.neighbor_sync_enabled=1` was enabled in WMCS UCI on
both routers. The Cudy's older active WMCS config had no `policy` section, so
one was created with the already-effective default thresholds and both
steering/force switches explicitly off. Both r9 daemons exchanged encrypted
challenge/reply records and reported `neighbor_sync_state=ready`, one fresh
authenticated neighbor, and zero apply failures. The preexisting reciprocal
hostapd records matched the authenticated reports; WMCS left them unchanged.

With steering off, one hostapd runtime neighbor list was cleared at a time
using `rrm_nr_set {"list":[]}`. The target list became empty immediately, then
WMCS restored the exact original reciprocal report on its next reconciliation
cycle. Each node wrote a mode-`0600` protected
`/etc/wmcs/neighbor-sync.state` (181 bytes for one record). Each WMCS daemon
was restarted separately, without Wi-Fi reload; after each restart its
authenticated sync returned to `ready`, generation remained 84, and the WLAN
list was unchanged.

For a peer-loss boundary, only Cudy's WMCS sync option was temporarily set to
zero and its daemon restarted. After the 30-second freshness TTL, BT changed
to `no_fresh_peer` and removed its own previously applied neighbor list. Its
protected state became an 80-byte empty record. Re-enabling the Cudy option
restored an authenticated exchange; both nodes returned to `ready` with one
reciprocal record and no apply failures. This was a daemon-level peer silence,
not a physical power or radio link loss.

The `/etc/config/wireless` SHA-256 remained unchanged throughout:

- BT-RB300: `6f5cb6bc766e1488abadbe7109f24be14d1485e430e069eccc52459b6f6359f9`.
- Cudy: `a442b743b552f5dc2e53230fddd975830a38525adcc1aa77f713f24949d7817c`.

The final active `/etc/config/wmcs` SHA-256 values are
`01b0507cf15672dadebc05f38edd44b9584c19e39cd79a19af4f1201c95f3e05`
on BT and `1d705b6f1944814975d566f13fc14546c1f5f20e7e17f1c48602ea1bee461c68`
on Cudy. This change is intentional and limited to the WMCS opt-in policy.

## Verification and limits

`make check`, `make check-roaming-event`, both aarch64/mipsel cross-builds,
exact BT/Cudy r9 package checks, LuCI view checks, package-manager simulations,
runtime status, protected state permissions, and reciprocal record equality
passed. No Wi-Fi reload, AP restart, router reboot, client walk, BTM request,
or forced disconnect was performed. Therefore hostapd **restart recovery** and
the r8/r9 Cudy `INT8` BTM response fix still need live verification. No
repeatable roaming or seamlessness claim follows from this experiment.
