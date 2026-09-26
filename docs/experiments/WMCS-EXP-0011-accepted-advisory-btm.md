# Experiment WMCS-EXP-0011

Status: one accepted advisory BTM transition observed; continuity followed in EXP-0016

## Question

After repairing the candidate radio and excluding stale source entries, will a
POCO F6 running Android 16 accept one public-hostapd advisory BTM from the
Globitel to the Cudy without a forced disconnect?

## Provenance

- Researcher: local WMCS development session
- Date and timezone: 2026-09-20, Europe/Moscow
- Interface source: public hostapd/OpenWrt ubus API
- Clean-room role: client compatibility experiment
- Data handling classification: sanitized local evidence

No roamd binary, source, configuration, trace, constant, or behavior capture
was used.

## Topology and prerequisites

| Role | Device | Runtime 5 GHz state |
|---|---|---|
| Source/controller | Globitel BT-RB300 | channel 149, HE80, operating class 128 |
| Target/agent | Cudy TR3000 v1 | channel 36, HE80 |
| Client | POCO F6, Android 16 | associated to Globitel; BTM and Neighbor Report advertised |

- Full wpad was active on both APs.
- The Cudy had completed a clean reboot, removing earlier stale station state.
- Runtime k/v was re-enabled after that reboot.
- Each AP held exactly one fresh Neighbor Report for the other AP.
- A corrected passive preflight showed one recently active station on the
  Globitel and no hostapd or kernel station entry on the Cudy.
- The Globitel candidate-radio repair from
  [EXP-0010](WMCS-EXP-0010-client-kv-steering-pilot.md) remained verified.

## Measurement limitation and experiment classification

The POCO acknowledged a short active 802.11k Beacon Measurement request, but
returned no Beacon Report. A simultaneous Cudy probe subscription also found
no event matching either the station's ephemeral salted MLD identity or link
identity. The production WMCS policy therefore had insufficient target-signal
evidence and would not have issued a BTM.

To isolate 802.11v client compatibility, this run was explicitly classified as
an operator-directed lab probe. It used the operator-confirmed physical
position beside the Cudy and did not change the production policy threshold.

## Controlled action

One BTM request was sent through `hostapd.phy1-ap0` on the Globitel with:

- one current Cudy Neighbor Report candidate;
- `disassociation_imminent=false`;
- `disassociation_timer=0`;
- `validity_period=30`;
- `abridged=true`.

The source had one recently active kernel station entry. The pre-request
passive samples were -64 and -63 dBm; the source subsequently reported -69 dBm
while the transition completed. The Cudy was empty immediately before the
run. No deauthentication, disassociation, client ban, radio reload, or UCI
mutation was used.

## Observation

- The Globitel BTM-request counter increased from 1 to 2.
- Its BTM-response counter increased from 0 to 1.
- Hostapd logged `status_code=0`, zero BSS termination delay, and the requested
  target BSSID.
- The corrected observer saw the Cudy association in the next polling interval
  at -22 to -25 dBm.
- The old Globitel station entry stopped carrying recent activity and its
  kernel inactivity increased monotonically, while the Cudy entry remained
  recent and carried the live path.
- Both hostapd objects remained available and enabled after the transition.

The transition appeared within the observer's approximately 1.2-second sample
resolution. That is association evidence, not a packet-loss or application
continuity measurement.

## Diagnostic harness failure

The one-use ucode subscriber received the event path but segfaulted while
formatting its delayed summary. This did not crash hostapd, remove either AP,
or affect the completed transition. The authoritative acceptance evidence is
the hostapd counter delta plus its status-0 log, not the failed helper's final
printout.

This failure means the ad-hoc ucode subscriber must not become the WMCS
implementation. Response handling belongs in the supervised WMCS daemon with
durable generation/state tracking and tests for subscriber teardown.

## Interpretation

- The POCO F6/Android 16 accepts a standards-based advisory BTM toward a live,
  compatible Cudy candidate.
- The earlier status-1 rejection is consistent with the then-broken Globitel
  candidate, not a general lack of 802.11v support.
- Hostapd station presence alone is insufficient because old source entries
  may remain authorized after the live path moves.
- This single run is not a compatibility claim and is not evidence of seamless
  roaming.

## Next gates

1. Build and validate the daemon-owned hostapd subscriber, including teardown,
   hostapd restart, and ubus reconnect behavior. The subscription and bounded
   response handling now live in `wmcsd` source; this is not yet build or
   hardware evidence.
2. Repeat the 802.11k request with the daemon enabled and check whether the
   POCO returns a Beacon Report. The prior active-request probe received no
   report; the new target-scoring path must continue to fail closed in that
   case.
3. Repeat the 100 ms accepted-transition continuity capture first completed in
   [EXP-0016](WMCS-EXP-0016-assisted-reverse-continuity.md).
4. Repeat at least 20 transitions in each direction, including rejection,
   timeout, cooldown, and ping-pong prevention cases.
5. Persist k/v through the owned WMCS configuration transaction and verify
   rollback and reboot recovery.
