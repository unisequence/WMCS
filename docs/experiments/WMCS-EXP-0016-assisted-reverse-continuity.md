# Experiment WMCS-EXP-0016

Status: one accepted advisory Cudy-to-Globitel BTM captured with one lost 100 ms probe

## Question

Will the POCO F6 accept one non-disruptive BTM from the Cudy to the repaired
Globitel while it is sticky on a weak Cudy link, and what packet interruption
is visible at 100 ms resolution?

## Provenance

- Researcher: local WMCS development session
- Date and timezone: 2026-09-20, Europe/Moscow
- Interface source: public OpenWrt hostapd ubus, nl80211, logread, and ICMP
- Clean-room role: client compatibility and continuity experiment
- Data handling classification: sanitized local evidence

No roamd binary, source, configuration, trace, constant, or behavior capture
was used.

## Initial state

- The hardware, full-wpad versions, shared L2 ESS, 14 dBm AP power, and runtime
  reciprocal Neighbor Reports matched EXP-0014/0015.
- The POCO F6/Android 16 was beside the Globitel but remained associated to the
  Cudy after the complete passive reverse walk in
  [EXP-0015](WMCS-EXP-0015-reverse-passive-stickiness.md).
- The Cudy client advertised BTM and Neighbor Report support.
- Cudy BTM counters began at zero requests and zero responses.
- The Globitel candidate was live on channel 149 at HE80 and had already
  accepted a fresh association after its radio repair in EXP-0010.

## Gate and request

The operator-directed lab gate required five consecutive Cudy samples at or
below -60 dBm. The corrected sample window was -67, -67, -69, -70, and -69 dBm.
At timestamp `1789926493942`, the helper recorded `request_attempt`; hostapd's
request counter advanced from 0 to 1 at `1789926493978`.

The request contained exactly one current Globitel Neighbor Report and used:

- `disassociation_imminent=false`;
- `disassociation_timer=0`;
- `validity_period=30`;
- `abridged=true`.

No deauthentication, disassociation, client ban, retry, radio reload, or UCI
mutation was used.

## Harness defect separated from radio evidence

An earlier local method call in the same capture was rejected as invalid before
hostapd's transmit counter advanced. OpenWrt `jshn` exposes punctuation in JSON
object keys as underscores; the first helper version passed that shell-safe key
instead of restoring MAC syntax. Thus the malformed call never became an
802.11 frame and is not counted as a client attempt.

The helper was corrected to restore and validate the address entirely on the
router. Its ubus failure path now suppresses the private payload and records
only `request_failed`. The retained first-error artifact was sanitized in place
before hashing; no station or BSS identifier remains in the evidence directory.

## BTM response and association transition

- The Cudy request counter advanced exactly once, from 0 to 1.
- Its response counter advanced from 0 to 1.
- At `1789926494008`, the sanitized result recorded `status_code=0`, zero BSS
  termination delay, and a target matching the requested Globitel candidate.
- The last observer sample whose live path was Cudy was at
  `1789926493987`, nine milliseconds after the request-counter observation.
- The first Globitel live-path sample was at `1789926495211`, 1.233 seconds
  after the request. Its signal was -48 dBm with 60 ms inactivity; the old Cudy
  entry had already accumulated 1220 ms inactivity.
- All remaining 26 samples used the Globitel live path. The Cudy entry remained
  visible but became stale, reaching 21 seconds of inactivity in the displayed
  post-transition series.
- A separate passive spot check approximately five minutes after the request
  still showed the Globitel as the only recent path. The old Cudy entry had
  about 300 seconds of inactivity. This one transition therefore passed the
  60-second anti-ping-pong condition.

Sanitized logs independently recorded the Cudy `BSS-TM-RESP` with status 0 at
17:48:13 and the Globitel association at 17:48:13, followed by
`AP-STA-CONNECTED` and an RSN pairwise-key handshake at 17:48:14. No FT exchange
was configured or observed.

## Packet continuity

- 1193 probes were transmitted and 1189 received: 0.3353% total loss.
- Every loss was isolated; the maximum consecutive loss was one nominal
  100 ms probe.
- Probe 869 returned at `1789926493.889021`, 53 ms before `request_attempt`,
  with 6.80 ms RTT.
- Probe 870 was missing.
- Probe 871 returned at `1789926494.143628`, 166 ms after `request_sent`, with
  56.6 ms RTT.
- The accepted assisted handoff therefore lost one 100 ms probe, with 254.6 ms
  between the surrounding successful replies.
- Three other isolated losses occurred at +44.9, +99.2, and +115.6 seconds and
  do not overlap the request/transition window.
- RTT min/average/max/mdev was 1.923/12.051/120.680/14.908 ms. The maximum RTT
  occurred about 74 seconds before the BTM, not during the handoff.

This single transition is inside the provisional non-FT budget of at most
eight consecutive lost probes and 800 ms disruption and passed a later
anti-ping-pong spot check. It does not establish a p95, voice/TCP quality, DHCP
behavior, or a supported/seamless claim. The continuous post-transition probe
window was approximately 31 seconds.

## Captures and artifacts

External evidence directory:
`/home/uni/quarantine/wmcs-exp0016-poco-f6-a16-assisted-c2g-01`

| Artifact | SHA-256 |
|---|---|
| `associations.tsv` | `6ac8b748f97043568d1c4f44f3c7b8d7e9bcce2b15564429948db4c0d43f1564` |
| `probe.raw` | `8cd21e800e850bde6bf02a946f365bf358c270d546963bc70636e21377cf859f` |
| `probe.tsv` | `e29d4c42d1d9274a4218c7e12ea219feea418ea9b7bc5ebba9b3e5ea665380b1` |
| `summary.txt` | `17233973165c840d27dc74600fad5e042a03868b5d4932f527bce3745c3cd915` |
| `btm.tsv` | `5ba10f1db8bcc2125a8f05fdec1ba3341947e3d91f48a649509e02027294e741` |
| `btm-attempt-classification.txt` | `ea577b8820b9ad712bf091732393524fe367134f25ad36fc7f3070c58d195b44` |
| `btm-corrected-exit.txt` | `d88a3d391bbf76f367f7823fb9e5e2da0571a97b36199119359915024f3ab33a` |
| `btm-invalid-address.tsv` | `a46e121a70144f8197756cd7519536eda4e49518ff98d95a7b0a9250fde5f5d2` |
| `btm-invalid-address.stderr` (sanitized) | `deda964433c32f6b267ce15c4cafab9b4d363dd29407141e018e1b7a06ac2b66` |
| `controller-events.sanitized.log` | `958720ac94a13108f37cfdea1b6ad5a4640a9c67b435564b26b381692a71e86c` |
| `agent-events.sanitized.log` | `47b86e4f69c4a3caf049c3d2f6357340d2a572b9e800e7f95bf3bf6d00570318` |
| `post-60s-associations.tsv` | `817dd1113543bd975fb8a4e3ebfc8874d6073a9797a231d58b90fca378fb87e9` |
| `post-60s-observer.stderr` | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |
| `metadata.txt` | `a96db951f4a0dead569249281aa39b87a791a354c4c210b8130ad6fef6e0b14a` |

## State diff and recovery

- Persistent AP configuration: unchanged.
- Runtime Neighbor Reports: retained.
- BTM frames transmitted: exactly one.
- Forced disconnects: zero.
- Services restarted: zero.
- Phone retained the probed private address and live L2 path.
- Manual recovery: none.

## Interpretation and next gates

Public hostapd 802.11v was sufficient to move this sticky POCO from a weak Cudy
link to a strong, valid Globitel candidate without a forced disconnect, and the
measured interruption was one probe. This is the first run that joins request,
accepted response, intended target, AP-path transition, and packet continuity
in one timestamped evidence set.

Before support can be claimed, WMCS still needs independent target-quality
evidence, durable daemon-side event handling, at least 20 transitions in each
direction, long-lived TCP/voice traffic, DHCP/address monitoring,
rejection/timeout/cooldown cases, and owned persistent k/v configuration with
rollback.

## Compatibility impact

The `802.11k/v roaming assistance` capability remains `partial`, but measured
continuity during one accepted advisory transition is no longer an open
unknown for this exact client/AP direction.
