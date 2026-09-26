# WMCS-EXP-0028: full wpad on BT-RB300 after WMCS r7

Date: 2026-09-26. Scope: package replacement and immediate capability check;
no steering request or roam was tested.

BT-RB300 was running `wpad-basic-mbedtls
2026.08.07~831364bf-r2` on the `d019f0b2e3` firmware. Its matching
buildroot produced `wpad-mbedtls-2026.08.07~831364bf-r2.apk` (SHA-256
`039f9b698e1f76effeb643e16ce18ded955b88bcf0de1651ecbcb0198ba8016b`).
The exact basic package was staged for rollback (SHA-256
`5dec399255aac1b6e4c9f2432d4d79218723d396903a28b4984faa586194e070`).
The package and rollback artifact checksums matched on the router. A local
backup of `/etc/config/wireless`, `/etc/apk/world`, and the old wpad binary
was made before mutation. The wired management route remained independent of
the AP being restarted.

`apk add -s --no-network --allow-untrusted` selected only purge-basic and
install-full. The real transaction completed, followed by
`/etc/init.d/wpad restart`. During restart the hostapd BSS and Cudy's WDS
management path disappeared briefly, then both returned. The running
hostapd executable and installed `/usr/sbin/wpad` had the same SHA-256,
`fc9178f2f65c066d0fcca94c3a63359143ed3c08e3d6d67061b952b66adbdc39`.
The 5 GHz BSS reported `ENABLED`; `bss_transition_request` appeared in its
ubus method list. Cudy was reachable again and its 5 GHz BSS reported
`ENABLED`. The `/etc/config/wireless` hash remained identical to its backup.
The exact reverse package selection (purge-full, install-basic) passed
`apk add -s` after installation.

The station that had been on the BT-RB300 5 GHz BSS before the restart was
disconnected by that restart. It had **not re-associated** at the first
automatic check. After the user manually toggled Wi-Fi, the same station
authenticated and associated to BT-RB300 at 15:14:20; hostapd recorded a
completed SAE/RSN four-way handshake and the station was authorized at the
subsequent check (signal about -31 dBm). Cudy's 5 GHz BSS remained enabled
with no client. This establishes a successful manual reconnection, not
uninterrupted client continuity or a roam. The restart log also contained
`rmdir[ctrl_interface=/var/run/hostapd]: Permission denied` messages while
the old hostapd exited, but both APs returned to `ENABLED` and no subsequent
authentication failure was observed. Roaming policy and forced-disassociation
policy remained disabled, Neighbor Report lists remained empty, and no BTM
request was sent.
