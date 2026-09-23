# Experiment WMCS-EXP-0014

Status: one natural Globitel-to-Cudy handoff captured with one lost 100 ms probe

## Question

What packet interruption occurs when the POCO F6 finally roams naturally from
the Globitel to the Cudy while both APs share the ESS and advertise runtime
802.11k/v information, but no BTM frame is transmitted?

## Provenance

- Researcher: local WMCS development session
- Date and timezone: 2026-09-20, Europe/Moscow
- Interface source: public OpenWrt hostapd, nl80211, logread, and ICMP interfaces
- Clean-room role: client behavior and continuity experiment
- Data handling classification: sanitized local evidence

No roamd binary, source, configuration, trace, constant, or behavior capture
was used.

## Topology and initial state

The AP pair, shared L2 ESS, WDS backhaul, 14 dBm transmit power, full-wpad
packages, and POCO F6/Android 16 client matched
[EXP-0012](WMCS-EXP-0012-passive-roundtrip-continuity.md). Runtime k/v remained
enabled with one reciprocal Neighbor Report per AP. The phone began on the
Globitel and retained the same private LAN address throughout the probe.

Reference managed-UCI digests remained:

| Node | SHA-256 |
|---|---|
| Globitel | `05a661c5739b02981e4bd12297c343a1a3445d2623fc782af5d2acd7dfb802cd` |
| Cudy | `5b108c340af411cf591a6fc49106544e4f8a4e98261606c4d4f007174e340223` |

## Controlled action and BTM exclusion

A fresh 120-second capture sampled both APs approximately every 1.2 seconds
and sent one ICMP probe every 100 ms. The operator remained beside the Cudy.
For this explicitly operator-directed run, a one-shot helper used a -60 dBm,
five-consecutive-sample source gate.

The helper reached that gate at timestamp `1789925649036`, then ended before
recording `request_sent`. Globitel's public counters stayed at two BTM requests
and one response before and after; the Cudy stayed at zero and zero. Therefore
no BTM transmission can be attributed to this run. The original helper did not
retain its exit status because its supervisor used fail-fast shell handling.
This limitation is recorded in `btm-attempt-note.txt` rather than inferred
away.

After this run, the helper was changed to emit `request_attempt` and
`request_failed` explicitly and to preserve a generic ubus error. No retry was
made during this capture.

## Observed association transition

- The first 61 observer samples used the Globitel live path.
- The last source-only sample was at `1789925717965` (+74.540 seconds) with a
  source signal of -77 dBm.
- The first sample containing the live Cudy path was at `1789925719205`
  (+75.780 seconds): Cudy was -45 dBm with 20 ms inactivity, while the old
  Globitel entry was -79 dBm with 250 ms inactivity.
- The observer therefore bounds discovery of the association change to a
  1.240-second polling interval; it does not claim that the RF handoff itself
  lasted that long.
- All remaining 36 samples used the Cudy live path. The old Globitel entry
  remained visible but its inactivity increased to 43.77 seconds, proving it
  was stale rather than a simultaneous live association.
- The observed post-transition window was about 43.5 seconds, so this run
  cannot satisfy the 60-second anti-ping-pong gate by itself.

Sanitized Cudy logs recorded association at 17:35:18, followed by
`AP-STA-CONNECTED` and an RSN pairwise-key handshake at 17:35:19. No FT exchange
was configured or observed. The Globitel had not yet removed its stale station
entry, so no source disconnect event appeared in the retained log window.

## Packet continuity

- 1193 probes were transmitted and 1189 replies received: 0.3353% total loss.
- RTT min/average/max/mdev was 1.788/11.965/115.102/17.022 ms.
- Probe 751 returned at `1789925718.907435` with 2.89 ms RTT.
- Probe 752 was the only missing probe in the association-change window.
- Probe 753 returned at `1789925719.114135` with 7.03 ms RTT.
- The handoff therefore lost one nominal 100 ms probe, with 206.7 ms between
  the surrounding successful replies.
- A separate three-probe loss occurred at +30.8 seconds while the phone was
  still solely on the Globitel. It is ordinary movement-path loss and must not
  be charged to the handoff.
- The maximum 115 ms RTT occurred near the end of the run on the Cudy, not at
  the association change.

This single handoff is inside the provisional non-FT loss budget, but one
sample cannot establish p95, voice quality, TCP continuity, DHCP behavior, or
a seamless-roaming compatibility claim.

## Captures and artifacts

External evidence directory:
`/home/uni/quarantine/wmcs-exp0014-poco-f6-a16-assisted-g2c-01`

| Artifact | SHA-256 |
|---|---|
| `associations.tsv` | `1319405aa3eb23a74907bc79a9d0f70520cd76c2b141c92f0f94af7f3e68c91b` |
| `probe.raw` | `2c396f186d91aedbcf4f3fad69eb33d202eda91e804b277a7f86421e858b3f05` |
| `probe.tsv` | `ed989cfe8f0ce4e7adbee4bc25d8bb4b663f1c765a282c6d9a9117978f714c6f` |
| `summary.txt` | `a08d90dcf9a25f535934bc22d163071a45f4bb8c970e3d99359844f91e1d0cfc` |
| `btm.tsv` | `7bd667fd0fb52488dc208d25e0f2b7da497ce20d06e4df4246148e8ad48426b6` |
| `btm-attempt-note.txt` | `22e77a8df51b8a58db06974a5aba3f9a4cc720f5a2458eccd42ff70ae2a1b2a0` |
| `agent-events.sanitized.log` | `d090bd1106493c6b78f1c8529f5cee013399a9e6b99324ea8971679a69bb3e60` |
| `controller-events.sanitized.log` | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |
| `metadata.txt` | `2ed7933555932e921a8cee8819912112e8abd459f22e02f15be325da1cd5aded` |

The event logs were filtered and MAC addresses were replaced on each router
before crossing SSH. Empty stderr and controller-event artifacts have the
standard empty-file SHA-256.

## State diff and recovery

- Persistent controller and agent configuration: unchanged by the capture.
- Radio or service restart: none.
- Forced disconnect or deauthentication: none.
- BTM request transmitted: no.
- Manual recovery: none.

## Interpretation and next gate

The POCO is demonstrably sticky but not permanently pinned: once the Globitel
fell to approximately -77 to -79 dBm and the Cudy was about -45 dBm, it roamed
on its own with one lost 100 ms probe. Static neighbor information alone did
not make the transition prompt or deterministic; the phone waited roughly 75
seconds in this run.

The next controlled run should start on the Cudy, walk toward the Globitel,
and measure an actual accepted advisory BTM using the improved failure log.
The method counter must advance exactly once before any transition is credited
to WMCS. Reverse natural-roam evidence should remain separately classified.

## Compatibility impact

`802.11k/v roaming assistance` remains `partial`. This adds the first measured
real handoff on the pair, but it was client-directed and is only one sample.
