# OpenWrt package

This directory is an out-of-tree OpenWrt package. It supports both package
formats selected by the buildroot: `.ipk` on OpenWrt 24.10 and `.apk` on newer
branches.

Build and inspect it without permanently modifying the OpenWrt tree:

```sh
make check-openwrt-package OPENWRT_DIR=/path/to/openwrt
```

The checker creates a temporary `package/wmcsd` symlink, removes only that
symlink when finished, builds the package, verifies its dependencies and files,
and checks PIE, RELRO, and immediate symbol binding on the target binary.

The ELF is installed as `/usr/lib/wmcs/wmcsd.bin`; `/usr/sbin/wmcsd` is a small
launcher that gives WMCS a private runtime-library directory. Normal packages
use the buildroot's exact ABI and need no aliases. Controlled cross-release lab
deployment may add validated aliases under `/usr/lib/wmcs`, following the same
private compatibility pattern as Freenetic. The package also creates the empty
protected state directory `/etc/wmcs` with mode `0700`; no identity is generated
until an administrator explicitly starts pairing.

The package installs `/usr/share/rpcd/acl.d/wmcs.json`. Its read and write sets
name only the documented `wmcs` ubus methods; it grants no UCI, file, shell, or
other ubus access. A UI package must explicitly request the appropriate scope.

The optional `luci-app-wmcs` package provides a modern LuCI view for WMCS. It
shows daemon runtime, native capabilities, roaming counters, discovered nodes,
paired peers and bounded operation state. The view polls only read-only ubus
methods; its write scope is limited to the administrator-owned `wmcs` UCI
configuration. The UI package intentionally does not require a package-managed
`wmcsd`; the backend may be supplied by the native package or by a validated
manual deployment. The Russian catalog is shipped as the companion
`luci-i18n-wmcs-ru` package. It stores the source trigger in the `roaming 'policy'` section:

```text
config roaming 'policy'
	option enabled '0'
	option neighbor_sync_enabled '0'
	option source_trigger_dbm '-68'
	option improvement_margin_db '8'
	option force_after_timeout '0'
	option force_trigger_dbm '-78'
```

The source threshold range is `-95` through `-50` dBm. The target improvement
margin range is `1` through `20` dB and defaults to `8` dB. The init wrapper
validates the thresholds and passes them to `wmcsd`. `enabled` and
`force_after_timeout` remain `0` by default. When roaming is enabled, the
experimental source-gate adapter polls local hostapd station and Neighbor
Report state and requires five consecutive weak samples plus a 20-second dwell.
It requests bounded 802.11k Beacon Reports serially when the client supports
them. A fresh report must show the target at least the configured margin
stronger than the source. If no usable report arrives, WMCS may still send one
advisory 802.11v BTM when exactly one locally published, same-SSID
Neighbor Report remains; with multiple candidates it fails closed. A measured
weak target is never treated as an unmeasured fallback. The BTM itself never
sets Disassociation Imminent, and WMCS never retries BTM within an association.

The separate `neighbor_sync_enabled` switch opts in to authenticated paired
AP Neighbor Report exchange and hostapd runtime 802.11k/v recovery. It does
not enable steering, forced disassociation, or a Wi-Fi UCI edit. The controller
source AP is touched only after this explicit opt-in; the agent additionally
requires the exact WMCS-owned AP and active controller marker. A protected
last-applied-list record lets the daemon distinguish its own list from a
foreign one after restart. A foreign list blocks WMCS changes and steering.
Full wpad is required on both APs; missing platform methods fail closed. See
[neighbor sync](../../docs/protocol/NEIGHBOR_SYNC_V0.md) for its limits.

The separate `force_after_timeout` option permits one hostapd disassociation
only after an unanswered BTM, a further 10-second delay, a still-authorized
weak client at or below `force_trigger_dbm` (bounded to -95..-75 dBm), and an
unchanged sole same-SSID neighbor. It gives the source a 5-second ban and
applies a 5-minute per-station cooldown, including failed requests. It does
not deauthenticate, does not act on an explicit BTM rejection or acceptance,
and cannot guarantee a roam; it may interrupt service. Same SSID in the local
Neighbor Report is not cryptographic proof of identical security or reachability.
Only enable this advanced mode after verifying the administrator-owned neighbor
configuration on both APs. A BTM acceptance is telemetry, not proof that the
client completed a roam.

Installation is inert by default. OpenWrt may create the normal init symlinks,
but `/etc/config/wmcs` sets both `enabled` and `mutation_enabled` to `0`, so the
init script does not leave a daemon process running. Installing the package does
not change UCI wireless configuration or start network listeners.

Version 0.2 adds the bounded authenticated one-WLAN and release pilot. Even after the
service is enabled, apply remains unavailable until `mutation_enabled` is set
explicitly. An agent then accepts mutation only from a paired controller during
an explicit control window. It creates or updates only the owned
`wireless.wmcs_home_5g` section, verifies its runtime hostapd interface, and
restores a protected full-wireless backup on failure.
Release removes that section only with matching ownership and radio scope,
revokes further peer mutation authority, and retains a bounded result tombstone
until explicit guarded local forgetting.
The controller keeps one protected exact request/result record so timeout,
daemon restart, and reboot cannot change the operation occupying a peer
sequence. Pending work requires explicit rediscovery before retransmission and
is never resumed merely by service startup.
