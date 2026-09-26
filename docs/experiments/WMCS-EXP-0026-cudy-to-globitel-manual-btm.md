# WMCS-EXP-0026 — sticky Poco F6, Cudy to Globitel advisory BTM

Status: one controlled transition observed; automatic Beacon Report gate did
not pass. This is not a supported roaming or seamlessness claim.

## Question and topology

Can a Poco F6 that remains associated to the weaker Cudy AP while physically
near the Globitel AP be moved with one non-disruptive 802.11v suggestion, and
what prevents the current WMCS automatic policy from making that suggestion?

- Date: 2026-09-26.
- Source: Cudy TR3000 v1, OpenWrt 25.12.5 `r33051-f5dae5ece4`, 5 GHz channel
  36, full `wpad-mbedtls`, WMCS `0.2.0-r6` agent.
- Target: Globitel BT-RB300, OpenWrt SNAPSHOT `r0+36056-d019f0b2e3`, 5 GHz
  channel 149, `wpad-basic-mbedtls`, WMCS `0.2.0-r6` controller.
- One phone: Poco F6 on the shared 5 GHz ESS, DHCP address `192.168.1.166`.
  The user confirmed that the phone was physically beside the Globitel for
  the entire first observation. It stayed there during the second run.
- Cudy hostapd runtime had 802.11k/v features enabled and exactly one Neighbor
  Report, pointing to the Globitel. No wireless UCI edit, deauthentication,
  Disassociation Imminent, or router reboot was used. The controller's Wi-Fi
  package and runtime were not changed.

The initial Cudy UCI export hashes were `wireless`
`dcbc165ea344867f70ea550a0a5e1b62a750deca23c8cebb75269e3c1c7fb728`,
`network` `3e1140173cd6ad351da6439caa4d88623265736a5765719692a76106c5332f2a`,
and `wmcs` `5f166ae918c44802abb0475809eaa257dfb265a926508a671c84dc5a3f4ab01e`.
The Globitel `wireless` and `network` hashes were respectively
`b7e0a6fa6ad75cd6dfb91560671b590fe45d152a06774f1fb78a4f170b5a6b36`
and `e701a13108b281b52606f6a0729045a0695a3cc7437cf54dd0f5e2aaeb28bb66`.

## Run A — automatic source gate, phone stationary by target

Cudy `wmcs.policy` was temporarily enabled with source threshold `-68 dBm`
and target improvement margin `8 dB`. Its roaming ubus status showed the
response monitor active and one Neighbor Report. The phone was the only
associated source client, advertised BTM, Neighbor Report, and Beacon
Measurement capabilities, and remained active on the Cudy at `-71` to
`-70 dBm` in all 99 association samples over 120 seconds. The Globitel had
zero associated clients throughout. The phone's physical position was only
confirmed by the user after this capture, so it was not used as a live gate.

WMCS sent Beacon Measurement Requests and received no Beacon Report. By the
final status read after this run, cumulative counters since enabling the
policy were 13 requests, 13 timeouts, zero reports, and zero BTM requests.
Those counters include pre-capture probes and must not be interpreted as 13
requests inside the 120-second capture. The policy therefore failed closed.
The ping probe received 1193 of 1194 replies; there was no internal sequence
gap. This was connectivity without a handoff, not a roaming success.

## Run B — exactly one administrator-controlled advisory BTM

The WMCS automatic policy was disabled and the original `wmcs` configuration
restored. Cudy's temporary hostapd 802.11k/v features and its single Neighbor
Report were enabled for a separate manual request. A dry run passed five
consecutive weak-source samples with one recently active BTM-capable client.
The 75-second association/ping capture began before the request.

The lab helper sent exactly one `bss_transition_request` on the Cudy with
`disassociation_imminent=false`, timer `0`, validity period `30`, and the
Globitel as the single candidate. Hostapd's request counter increased `0 ->
1`; its response counter increased `0 -> 1`. The BTM response reported status
`0` and a target matching the private Neighbor Report. In the first observer
sample about 1.2 seconds after request transmission, the Globitel was the
recent active AP at approximately `-58 dBm`. It remained recent for the rest
of the capture (53 samples, no later controller-recent drop). The Cudy kept a
stale hostapd station entry for a while; its kernel recent-activity flag then
fell to zero. Thus the two hostapd entries are not evidence of simultaneous
active associations.

The same phone IP answered 744 of 746 probes over 75 seconds. The two missing
probes were consecutive and around the transition, corresponding to roughly
200 ms at the configured 100 ms interval. Maximum reported RTT was 85.975 ms.
No address change or return to the Cudy was observed within the capture. This
single non-FT transition meets the provisional packet-loss budget for that one
run, but does not establish a p95, DHCP-renewal absence, voice quality, or
repeatability. An accepted BTM response alone would not establish a handoff;
the independent AP activity timeline is what supports it here.

## Recovery, limits, and evidence

After both runs, Cudy's runtime Neighbor Report list was cleared and its
temporary 802.11k/v flags disabled. The Cudy `wmcs`, `wireless`, and `network`
UCI hashes matched their initial values above. Both WMCS peers were active,
both APs enabled, neither daemon degraded, and the Globitel's wireless/network
hashes remained unchanged. The workstation's Internet default route remained
via `192.168.64.1`, not the lab routers.

The missing Beacon Reports cannot yet be attributed specifically to the
phone, hostapd, request mode, or WMCS event processing. Manual BTM bypassed
the automatic policy's fresh-target-measurement gate; this is a diagnostic,
not authorization to remove that safety gate by default. The exact deployed
binary/source correspondence of the r6 lab packages was not re-established
in this experiment. Only one client, AP pair, direction, and manual transition
were measured; 802.11r was not enabled or inferred.

Sanitized artifacts (no station MAC or Wi-Fi credential) are outside Git:

- Run A: `/home/uni/quarantine/wmcs-assisted-cudy-to-btrb-20260926T124830Z/`.
  `associations.tsv` SHA-256
  `344868a5315bea5ae92bed45a1153a900ca48a436119c9f9927e0820062408cf`;
  `probe.tsv` SHA-256
  `0cf24f7c5807d523245d898b46f182d3d1b75d9ad86ae0d581c37897b2182f40`.
- Run B: `/home/uni/quarantine/wmcs-manual-btm-cudy-to-btrb-20260926T130800Z/`.
  `associations.tsv` SHA-256
  `047060b3dcfc34031f4c6b9bbbcb5b5b10accbe2ea770381858cb354fb592c49`;
  `probe.tsv` SHA-256
  `b07dfd927f6a8dd3a44f811fbb9fc1b10d84dcdd939beb185f96ee034682e361`.

Next: establish whether the Poco answers a bounded Beacon Request under any
mode/channel combination, then choose a safe target-quality source or an
explicitly opt-in single-neighbor advisory fallback. Do not claim automatic
assisted roaming from this manual result.
