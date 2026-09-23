# Experiment WMCS-EXP-0013

Status: strict source gate calibrated; no BTM sent and no roam occurred

## Question

Does a POCO F6 walking from the Globitel toward the Cudy remain at or below
-68 dBm on the source AP for five consecutive one-second samples, so that the
existing conservative laboratory BTM gate can open without operator judgment?

## Provenance

- Researcher: local WMCS development session
- Date and timezone: 2026-09-20, Europe/Moscow
- Interface source: public OpenWrt hostapd, nl80211, and ICMP interfaces
- Clean-room role: threshold calibration
- Data handling classification: sanitized local evidence

No roamd artifact or behavior trace was used.

## Initial state and controlled action

The hardware, shared ESS, runtime k/v state, 14 dBm AP transmit power, and POCO
F6/Android 16 client matched
[EXP-0012](WMCS-EXP-0012-passive-roundtrip-continuity.md). The phone began on
the Globitel. A 180-second passive association/ICMP capture was started along
with a one-shot gate requiring five consecutive source samples at or below
-68 dBm. A valid gate would have emitted one advisory BTM with no imminent
disassociation and a zero disassociation timer.

## Observation

- The gate timed out normally and the BTM helper exited with status 0.
- Globitel BTM counters remained at two requests and one response. No request
  was sent by this run.
- All 142 association samples retained the recent live path on the Globitel;
  no recent Cudy association appeared.
- Source signal ranged from -41 to -73 dBm, but readings at or below -68 dBm
  were not sustained for five consecutive samples.
- The operator terminated the still-running 180-second capture when starting
  the separately classified follow-up. Ping and observer therefore exited 143
  after approximately 174 seconds. This partial capture is not handoff-quality
  evidence.
- The parsed probe series contained one isolated missing sequence.

This is the desired fail-closed behavior: isolated weak readings did not cause
a steering request.

## Artifacts

External evidence directory:
`/home/uni/quarantine/wmcs-exp0013-poco-f6-a16-assisted-g2c-01`

| Artifact | SHA-256 |
|---|---|
| `associations.tsv` | `699ccba3671f16de129f7b120ea871c88cea4387c9444b4ab01877e1c0489fd4` |
| `btm.tsv` | `68c9182c18a2a76cc065821da6bacf615d04fe9bfae6d5e01da6f44341f38a3b` |
| `btm-exit.txt` | `d88a3d391bbf76f367f7823fb9e5e2da0571a97b36199119359915024f3ab33a` |
| `probe.raw` | `2e27812f2011241f4588f4d7170f30d25de70b37444e1ee394c405677fb8253d` |
| `probe.tsv` | `793e3f9297afbbb1ff2149f528c83c0ba26d28a4c10cfb8e42b00f2788d7f588` |
| `summary.txt` | `3dab6313b2678e8c9f16f6a2a07ffd7fbaf173afcfde9fc33ae198301fa27187` |

## Interpretation

The -68 dBm/five-sample condition is too strict to exercise steering reliably
in this particular indoor path at 14 dBm. That does not justify weakening the
production policy: target quality is still unavailable, and a source-only
threshold cannot prove improvement. A -60 dBm gate may be used only in a
clearly operator-directed compatibility run where the operator confirms that
the phone is beside the target AP.

Compatibility remains unchanged. This record is calibration and negative
safety evidence, not a roaming result.
