# Experiment WMCS-EXP-0012

Status: passive round trip captured; no AP transition occurred

## Question

Will a POCO F6 running Android 16 roam autonomously from the Globitel to the
Cudy and back during a physical round trip when both APs advertise runtime
802.11k/v support and one Neighbor Report for the other AP, but no BTM request
is sent? If it does, what packet interruption is visible at 100 ms resolution?

## Provenance

- Researcher: local WMCS development session
- Date and timezone: 2026-09-20, Europe/Moscow
- Interface source: public OpenWrt hostapd, nl80211, and ICMP interfaces
- Clean-room role: client behavior and continuity experiment
- Data handling classification: sanitized local evidence

No roamd binary, source, configuration, trace, constant, or behavior capture
was used.

## Topology and devices

```text
Linux probe host ---- isolated lab LAN ---- Globitel controller
                              |              5 GHz ch 149 / HE80
                              |
                         2.4 GHz WDS
                              |
                         Cudy agent
                         5 GHz ch 36 / HE80
```

| Role | Device and firmware | Runtime role |
|---|---|---|
| Source AP | Globitel BT-RB300, OpenWrt SNAPSHOT `r0+36056-d019f0b2e3` | controller |
| Candidate AP | Cudy TR3000 v1, OpenWrt 25.12.5 `r33051-f5dae5ece4` | agent |
| Client | POCO F6, Android 16 | station |
| Probe | Linux workstation on isolated static-address interface | observer and ICMP source |

The probe host had no default route or DNS through the lab interface. The
phone's private address appears only in the external evidence metadata. No
station MAC address or WLAN credential was collected.

## Initial state

- Both 5 GHz APs used the same bridged ESS and 14 dBm configured/runtime
  transmit power.
- The Globitel AP was live on channel 149 at HE80. Its persisted channel value
  remained 112 because the existing station/backhaul role selected the runtime
  channel; this experiment did not change it.
- The Cudy AP was live on channel 36 at HE80.
- Full wpad was active on both nodes. Runtime k/v was enabled and each AP held
  exactly one Neighbor Report for the other AP.
- The phone began on the Globitel and advertised BTM and Neighbor Report
  support.
- No BTM request, forced disconnect, radio reload, or configuration write was
  part of this run.

The capture did not embed a pre-run configuration digest. Immediately after
the passive run, the reference digests were:

| Node | Managed UCI SHA-256 | Installed-package inventory SHA-256 |
|---|---|---|
| Globitel | `05a661c5739b02981e4bd12297c343a1a3445d2623fc782af5d2acd7dfb802cd` | `33dd8ba838f4a295dd574a9efa02fa910f69a783cd9a20936f913d21594324e7` |
| Cudy | `5b108c340af411cf591a6fc49106544e4f8a4e98261606c4d4f007174e340223` | `b09a4911ca811755b960a1d2864f02c695009e5de6373a58665dd9c14f5c6f9a` |

These are post-run state references, not proof of byte-identical pre/post
state. The capture program itself is read-only.

## Controlled action

`capture-roaming-baseline.sh` observed both APs for 120 seconds while sending
ICMP probes to the phone every 100 ms. The operator walked from the Globitel to
the Cudy, remained beside the Cudy, and returned to the Globitel before the
capture ended. The observer sampled aggregate hostapd and kernel station state
approximately every 1.2 seconds.

## Expected observation

If the client roamed autonomously, the recent live path would move from the
Globitel to the Cudy and later return. The ICMP sequence around each association
change would bound user-visible disruption. If no association change occurred,
the run would instead measure client stickiness and could not be used as
handoff-continuity evidence.

## Captures and artifacts

External evidence directory:
`/home/uni/quarantine/wmcs-exp0012-poco-f6-a16-continuity-01`

| Artifact | SHA-256 |
|---|---|
| `associations.tsv` | `c6ba41f10c76a561c1bb21ee8f4bb68d7f043f668a8cf8ea144316576b17f4e1` |
| `metadata.txt` | `a474efda70e2d0774e74b6ad3c645f3a1b22da528ba19b1d9d1d802c142c63f8` |
| `observer.stderr` | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |
| `probe.raw` | `fa6eb23c4f970780badd8a4533848f50a5de9897e32afb0a05d3e7d0298c3ba8` |
| `probe.stderr` | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |
| `probe.tsv` | `863ef87538811ae33769a06f9297ede1ad1696cb11e5e2b6a51945b9b21737ee` |
| `summary.txt` | `867a9ffb0a42e1f0c60b9ff8805f35414c57cbbc6b1cfd6ab04f9093c4a7bacb` |

The two empty stderr files have the standard SHA-256 of an empty file.

## Observed behavior

- All 98 association samples showed one station on the Globitel and zero on
  the Cudy. No stale or recent Cudy station entry appeared at any point.
- The Globitel signal ranged from -40 to -70 dBm. The weakest sample occurred
  30.014 seconds after the first probe; the live path still remained on the
  Globitel.
- The phone returned 1192 of 1193 probes: 0.0838% loss. The run had one missing
  sequence, number 1110, between replies at +111.445 and +111.701 seconds.
- The summary's largest internal missing run was one nominal 100 ms probe.
  RTT was 2.091/10.226/148.137/15.060 ms min/average/max/mdev.
- The largest RTT, 148 ms at +23.540 seconds, was nearest an association sample
  showing -62 dBm on the Globitel and no station on the Cudy.
- The missing probe and all RTT spikes occurred without an observed AP change.
  They therefore cannot be attributed to roaming.
- Both observer and probe stderr captures were empty, and both capture
  processes exited successfully.

## State diff and recovery

- Controller configuration: no intentional mutation.
- Agent configuration: no intentional mutation.
- Runtime Neighbor Reports after the run: one on each AP.
- Services restarted or interrupted: none.
- Failure injected: none.
- Manual recovery required: none.

## Interpretation

This client was sticky in this geometry: a shared ESS plus static 802.11k
neighbor information did not cause an autonomous transition, even when the
source reached -70 dBm. This independently reinforces the need for a
conservative 802.11v steering policy with a multi-sample source threshold,
candidate validation, hysteresis, dwell time, and cooldown.

The low packet loss is useful movement-path baseline data, but it is not a
handoff result because no handoff occurred. It must not be described as
seamless roaming. Confidence is high that no transition occurred during the
captured window and low that this single walk predicts all client behavior;
phone scan timing, screen/power state, traffic level, path timing, and RF
geometry remain uncontrolled alternatives.

The next experiment should keep the same 100 ms probe running while issuing
one operator-directed, non-disruptive BTM after a sustained weak-source window,
then repeat in the reverse direction. The BTM event, response status, selected
target, live-path change, and packet sequence must share one timestamped
record. This remains a lab compatibility probe until WMCS has independent
target-quality evidence.

## Compatibility impact

The `802.11k/v roaming assistance` row remains `partial`. This run adds direct
sticky-client and continuity-baseline evidence, but it does not advance a
seamless-roaming or supported-client claim.
