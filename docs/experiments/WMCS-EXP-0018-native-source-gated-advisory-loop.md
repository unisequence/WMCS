# WMCS-EXP-0018 — native source-gated advisory loop

Status: accepted historical pilot; follow-up daemon changes are unbuilt and
unvalidated; production target scoring remains open

## Question

Will the first native WMCS roaming loop observe a weak source, wait for its
guard window, send one non-disruptive 802.11v request, and preserve LAN
continuity while the POCO F6 moves to the neighboring AP?

## Topology

- Globitel BT-RB300 controller: `192.168.1.1`, 5 GHz channel 149.
- Cudy TR3000 v1 agent: `192.168.1.2`, 5 GHz channel 36.
- Client: POCO F6, Android 16.
- Both APs used the shared `OpenWrt_5G` ESS and one runtime Neighbor Report
  entry in each direction.

The runtime 802.11k/v helper enabled public hostapd features without changing
UCI. The controller then ran `wmcsd 0.2.0-r3` with
`roaming.policy.enabled=1` and `source_trigger_dbm=-68`. The Cudy daemon was
not required for this controller-side advisory pilot.

## Safety gate

The native loop required all of the following:

1. an associated and authorized source client;
2. client-advertised 802.11v BSS Transition and 802.11k Neighbor Report
   support;
3. exactly one trusted local Neighbor Report candidate;
4. source RSSI at or below `-68 dBm`;
5. five consecutive weak samples after a 20-second minimum dwell;
6. one attempt maximum for that association.

The request used `disassociation_imminent=false`, timer `0`, validity period
`30`, and one candidate. No deauthentication, radio reload, wireless UCI
change, or retry occurred.

## Observation

The controller reported:

- `gate_passes=1`;
- `requests_sent=1`;
- `request_failures=0`;
- hostapd `bss_transition_request_tx`: `2 -> 3`;
- hostapd `bss_transition_response_rx`: `1 -> 2`;
- response `status_code=0`.

The privacy-preserving observer then saw one recently active client on the
Cudy at approximately `-19` to `-32 dBm`. The old Globitel station entry
remained in hostapd but had `recent_activity_count=0`, so it was treated as a
stale authorization entry rather than a second live association.

A 15-second ICMP continuity probe to the phone completed with `150/150`
replies and `0%` loss. The measured round-trip average was 17.613 ms and the
maximum was 112.162 ms.

## Interpretation

This is the first live evidence that the WMCS daemon, rather than the shell
pilot, crossed the source threshold, applied the confirmation guard, and sent
one accepted advisory BTM. The result supports the current non-disruptive
source-gate path and its one-attempt protection.

It is not a universal seamless-roaming claim. This historical build did not
score a fresh target measurement or correlate the BTM response inside `wmcsd`.
Follow-up source now adds a bounded 802.11k Beacon Report path, a target margin,
and daemon-owned BTM/measurement event handling. That code has not been built or
validated on the routers. It still does not exchange station observations
through authenticated WMCS control; this remains a local hostapd/client
measurement path.
