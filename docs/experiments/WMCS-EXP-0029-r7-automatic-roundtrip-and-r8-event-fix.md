# WMCS-EXP-0029: automatic r7 roundtrip and BTM response-width fix

Date: 2026-09-26. Status: two controlled automatic transitions observed on
one phone and one AP pair; not a repeatability or seamless-roaming claim.

## Topology and gate

- BT-RB300: OpenWrt SNAPSHOT `d019f0b2e3`, full
  `wpad-mbedtls 2026.08.07~831364bf-r2`, WMCS `0.2.0-r7` controller.
- Cudy TR3000 v1: OpenWrt 25.12.5 `r33051-f5dae5ece4`, full
  `wpad-mbedtls 2025.08.26~ca266cc2-r2`, WMCS `0.2.0-r7` agent.
- POCO F6, Android 16, on the shared `OpenWrt_5G` ESS, reachable at
  `192.168.1.166` from the independent wired lab interface.
- Public hostapd runtime k/v flags were enabled and each AP held exactly one
  Neighbor Report for the other AP. After reconnecting, the POCO advertised
  BTM and Neighbor Report support on the BT association. The runtime helper
  verified that managed UCI exports were unchanged.
- Only the current source AP's WMCS roaming policy was enabled, with a
  `-68 dBm` source threshold. Its window was bounded by an operator-launched
  watchdog that restored the previous policy. `force_after_timeout=0` on both
  runs. There was no `del_client`, deauthentication, wireless reload, or
  802.11r.

## BT-RB300 to Cudy

The user walked from BT-RB300 to Cudy and back during a 180-second
association/100 ms ICMP capture. BT saw source signal around `-77 dBm` and
waited through the minimum dwell. WMCS received one Beacon Report, measured
the Cudy candidate around `-42 dBm` with a `36 dB` margin, sent one advisory
BTM, and recorded one accepted response (`status_code=0`). Hostapd's request
and response counters each increased once. The observer saw Cudy become the
recent active AP; BT's old authorized station entry later became inactive.

The probe received 1785 of 1789 replies (0.224% total loss). Its maximum
internal gap was one 100 ms probe. These are whole-run figures, not a claimed
application-level handoff duration. On returning beside BT, the POCO remained
active on the weaker Cudy AP; Cudy's policy was still off at that point.

## Cudy to BT-RB300

With the user stationary beside BT, Cudy's policy was enabled for a second
bounded run. It observed source signal around `-74` to `-78 dBm`, received one
Beacon Report, measured BT around `-53 dBm` with a `23 dB` margin, and sent
one advisory BTM. Cudy hostapd logged a `status_code=0` response naming BT
as target, and its BTM request/response counters each increased once. The
observer saw BT become the recent active AP and Cudy's old entry become
inactive. No forced disassociation occurred.

The probe received 1188 of 1193 replies (0.419% total loss), with a maximum
internal gap of three 100 ms probes near the transition. Whole-run losses
must not all be attributed to the transition. Critically, r7 WMCS recorded
`btm_responses=0` and `btm_response_timeouts=1` despite hostapd's accepted
response. The air transition succeeded; the daemon's response accounting did
not.

## Root cause and r8 correction

The OpenWrt 25.12 hostapd source emits BTM `dialog-token` and `status-code`
notification attributes as blobmsg `INT8`; the newer BT-RB300 snapshot emits
them as `INT32`. The r7 WMCS subscriber policy accepted only `INT32`, so it
discarded Cudy's valid event before correlating the token and station. This
is a public-source platform-version difference, not evidence of a phone
rejection. It also means the optional force-after-timeout feature must remain
off until response handling is verified live on Cudy.

The r8 parser accepts exactly `INT8` or `INT32`, validates the octet range
and nonzero dialog token, and retains station/token matching. A host libubox
test covers both widths and invalid values. `make check`, the parser test,
aarch64/mipsel cross-builds, and exact BT-RB300 and 25.12.5 SDK APK checks
passed. Exact r8 packages were installed after the walk tests:

- BT-RB300 APK SHA-256:
  `26bfe867b68fdcba829da817cb488a6c2ce787f852ebeda29a7bba436add629d`.
- Cudy APK SHA-256:
  `9c1ed5a12de699da965031da8832e5fbe237f17e0d305427a3e5e90fbba4f6d3`.

Both daemons report `0.2.0-r8`, generation 84, their existing paired peer,
no degraded state, and roaming/force policies off. Both 5 GHz APs remain
`ENABLED`; the phone is active on BT. Package payload hashes matched the
installed binaries, and exact r7 downgrade plans were simulated on both
routers. The Cudy WMCS config SHA-256 remained
`7550ee188ad902744dc24c157e546418835570b94d52cd94cadaa5941a39f5ca`;
the BT config remained
`cea89952def3a7ccf3408a0419243e557f7f53a7c20c45ccd93bb0e0a3922219`.
Root-only `/tmp/wmcs-r8.*` backups and r7 APKs remain on each router for
short-term rollback. The workstation's Internet route remained via
`192.168.64.1`, not either lab router.

The r8 response fix has **not** yet been exercised by a new live BTM response.
The runtime k/v flags and Neighbor Reports are volatile and will disappear on
wpad restart. Persistent owned configuration, a live r8 response check,
repeatable two-direction quality, and reboot/rollback tests remain before a
supported roaming claim.

## Sanitized evidence

- BT to Cudy: `/home/uni/quarantine/wmcs-r7-bt-cudy-20260926T1622Z/`;
  `associations.tsv` SHA-256
  `83dd7bf26acaeb8b941c129119f940ee5a3220b2a2e9a59e3c1caa400a3b4a57`,
  `probe.tsv` SHA-256
  `df4a12471af1b5f03acba61925e5b8142a8e3c10e2420349e39b1d4993d9a72d`.
- Cudy to BT: `/home/uni/quarantine/wmcs-r7-cudy-bt-20260926T1626Z/`;
  `associations.tsv` SHA-256
  `a6ec44212fdc94a5dc9cb27c0005b3b9ca8b9c9c0fecb4e0daf5fec6092efbf5`,
  `probe.tsv` SHA-256
  `82a35224c4c46bb5b18363fed1fada74e7630b7ecda1264eba7c6845ac95ff9b`.

No station MAC address or WLAN credential was stored in these capture files.
