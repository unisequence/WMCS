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
	option source_trigger_dbm '-68'
```

The accepted range is `-95` through `-50` dBm. The init wrapper validates the
value and passes it to `wmcsd`. `enabled` remains `0` by default. When enabled,
the experimental source-gate adapter polls the local hostapd station and
Neighbor Report interfaces, requires five consecutive samples at or below the
threshold plus a 20-second dwell, and may send one advisory 802.11v request.
It never sets Disassociation Imminent, never deauthenticates, and never retries
within the same association. Target signal scoring and response correlation
remain a later capability.

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
