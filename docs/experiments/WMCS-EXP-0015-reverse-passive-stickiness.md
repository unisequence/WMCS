# Experiment WMCS-EXP-0015

Status: complete reverse walk captured; no natural transition occurred

## Question

After the POCO F6 has moved to the Cudy, will it roam naturally back to the
Globitel during the return walk, and what continuity is visible if it remains
on the source?

## Provenance

- Researcher: local WMCS development session
- Date and timezone: 2026-09-20, Europe/Moscow
- Clean-room role: client behavior and continuity experiment
- Data handling classification: sanitized local evidence

No roamd artifact or behavior trace was used.

## Controlled action

Immediately after the natural Globitel-to-Cudy transition in
[EXP-0014](WMCS-EXP-0014-natural-handoff-continuity.md), a fresh 120-second
passive capture began with the phone beside the Cudy. The operator walked back
to the Globitel and remained there. No BTM, measurement request, disconnect,
configuration mutation, or radio reload was issued.

## Observation

- All 97 association samples retained the live path on the Cudy.
- The Globitel had no hostapd or recent kernel station entry during the entire
  capture.
- Cudy signal ranged from -38 to -72 dBm. Multiple weak samples still did not
  cause the client to roam naturally.
- All 1193 ICMP probes returned: zero packet loss.
- RTT min/average/max/mdev was 2.709/13.369/147.815/15.114 ms.
- Because no AP transition occurred, this is movement-path continuity and
  sticky-client evidence, not handoff quality evidence.

## Artifacts

External evidence directory:
`/home/uni/quarantine/wmcs-exp0015-poco-f6-a16-natural-c2g-01`

| Artifact | SHA-256 |
|---|---|
| `associations.tsv` | `9d7b9e683f83d631c053249f2ee8cc1b19a4a0844356c1afa557e8e6141743e0` |
| `probe.raw` | `d37ba29fbd4912cd412ea3549a340d4cdcedc2d9d358e52f793e722610f80f0c` |
| `probe.tsv` | `7e0c910bee3fcc0a2569fe443fd6ce5a21a11933250074d57e7941ecc9ce631f` |
| `summary.txt` | `2fdd3c9558c1954a975ccd6aa6cf8e96022bbbd9649260f282fffbcce86a4456` |
| `metadata.txt` | `a27cf17ec3b35a944321edb198def3387eafc6d100c2eff974a095a7f4be2406` |
| `capture.stdout` | `06dec2e244375108a8ea17e3176a68046fed86622a39f4278b7f1e7abe1afda8` |

Both stderr artifacts were empty.

## Interpretation

Natural roaming is asymmetric and nondeterministic on this client/AP geometry.
The client eventually left the Globitel in EXP-0014, but did not leave the Cudy
on the reverse walk despite a -72 dBm source sample. This is direct evidence
that a shared SSID and static Neighbor Reports cannot provide a predictable
handoff deadline.

The next experiment is an operator-directed advisory BTM from the Cudy to the
known-live Globitel under the same 100 ms continuity probe.

## Compatibility impact

No status change. The result strengthens the rationale for conservative
standards-based steering but is not itself an assisted-roaming claim.
