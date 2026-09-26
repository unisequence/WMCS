# Experimental paired Neighbor Report sync

This is an opt-in extension, not a supported roaming claim. It is disabled by
default with `wmcs.policy.neighbor_sync_enabled=0` and does not require or
enable the advisory BTM policy. Both APs need a full wpad/hostapd build with
`bss_mgmt_enable`, `rrm_nr_get_own`, `rrm_nr_list`, and `rrm_nr_set`.

## Ownership and recovery

WMCS uses the configured source BSS on a controller only after explicit opt-in.
On an agent, it additionally requires the exact `wmcs_home_5g` section and its
active controller ownership marker. It does not change `/etc/config/wireless`
on either router. Every cycle it resolves the current BSS through
`network.wireless status`, checks `get_status`, idempotently enables
802.11k/v hostapd runtime support, and reads the BSS's current own Neighbor
Report. This recreates runtime capability flags after hostapd or WMCS restart
without a Wi-Fi reload. The hostapd API only enables these flags; turning the
option off stops future reconciliation but does not instantly turn them off.
They disappear on a subsequent hostapd restart.

Every 10 seconds each node multicasts a 256-byte authenticated query on
`239.255.77.68:45126`, TTL 1, bound to the configured discovery interface.
Only active paired opposite-role identities are queried or accepted. A reply
echoes a fresh 16-byte random challenge within 15 seconds, and carries only
the local AP's SSID and own Neighbor Report (13..100 bytes). WLAN credentials
and station addresses do not enter this exchange. The packet uses the WMCX
envelope and AES-GCM with separate query/reply HKDF domains. A replayed reply
cannot match a new challenge. A paired node that is offline for over 30
seconds expires from the desired list. Same-SSID, non-self BSSID, bounded and
well-formed reports are required. Authentication asserts peer identity, not
physical RF reachability or equivalent security at the target.

`rrm_nr_set` replaces the entire hostapd neighbor list. WMCS therefore writes
only when the list is empty or equals its exact last-applied list. The latter
is recorded atomically and privately in `/etc/wmcs/neighbor-sync.state`; the
local AP BSSID and SSID bind that record to the intended BSS. A nonempty
foreign or malformed list is a conflict and is never overwritten. After a
write, WMCS reads the list back. On error, it restores the pre-write snapshot
only if the observed partial list is a subset of its own attempted write; a
concurrent foreign edit blocks rollback rather than being overwritten. Store
or rollback failures are visible, never reported as ready.

When the separate advisory steering policy is on, it cannot send a BTM while
sync is unready. Immediately before evaluating clients, it checks that the
observed hostapd list equals the authenticated fresh candidate set. A changed
list pauses steering and schedules a fresh reconciliation. `force_after_timeout`
remains off by default and is not implicitly enabled by neighbor sync.

Limits: at most 16 peers/reports, at most 100 Neighbor Report bytes each, and
no migration of a foreign list into WMCS ownership. Disabling sync or WMCS
does not erase another service's list. WPA/FT, channel planning, and wireless
backhaul are outside this extension. Live two-router reboot/rollback tests
and repeated client handoffs are still required before claiming readiness.
