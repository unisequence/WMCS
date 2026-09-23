# Experiment WMCS-EXP-0006

Status: prepared; capability preflight complete; awaiting a controlled client walk

## Question

How does one real phone move in both directions between the Globitel and Cudy
5 GHz APs before any WMCS 802.11k/v/r assistance is enabled, and what is the
resulting user-visible packet interruption?

## Provenance

- Researcher: local WMCS development session
- Date and timezone: 2026-09-20, Europe/Moscow
- Public references: WMCS roaming quality requirements, EXP-0005
- Clean-room role: implementation and validation
- Data handling classification: sanitized local evidence

## Topology

```text
development host
  enp9s0  192.168.64.142/24 -> normal Internet gateway 192.168.64.1
  enp10s0 192.168.1.182/24  -> isolated WMCS lab, no gateway or DNS
                                  |
                +-----------------+-----------------+
                |                                   |
        Globitel controller                  Cudy agent
        192.168.1.1       2.4 GHz WDS        192.168.1.2
        channel 149       L2 backhaul        channel 36
```

Both APs bridge the same WLAN to the same Ethernet/L2 LAN. The development
host's NetworkManager profile `wmcs-lab-globitel` has static IPv4, no gateway,
no DNS, ignored automatic routes, `never-default`, and disabled IPv6. External
host traffic continues through `enp9s0`; the lab router cannot become the
computer's Internet gateway.

## Devices

| Role | Vendor and model | Firmware | Runtime role | Notes |
|---|---|---|---|---|
| AP A | Globitel BT-RB300 | OpenWrt SNAPSHOT `r0+36056-d019f0b2e3` | WMCS controller | 5 GHz source AP |
| AP B | Cudy TR3000 v1 | OpenWrt 25.12.5 `r33051-f5dae5ece4` | WMCS agent | owned `wmcs_home_5g` AP |
| Client | to be recorded | to be recorded | station | model and OS build required before classification |
| Probe host | Linux workstation | local | observer/probe | direct wired lab link |

## Initial state

- Both `wmcsd 0.2.0-r1` services are paired at generation 5.
- The owned Cudy AP is active as `hostapd.phy1-ap0` in `br-lan`.
- No 802.11k/v UCI options are enabled on either tested AP.
- Both hostapd builds expose RRM neighbor-report/beacon-request ubus
  primitives; neither currently exposes a BSS-transition operation. The exact
  package/API baseline is recorded in [EXP-0007](WMCS-EXP-0007-hostapd-roaming-capabilities.md).
- No separate `usteer`, `dawn`, or `roamd` package was observed.
- The original privacy-preserving preflight had a compact-JSON counting defect
  and cannot be used as evidence. After correction, a five-second sample
  observed one phone on the Cudy at -64 to -67 dBm and zero clients on the
  Globitel. A controlled bidirectional walk and continuity capture remain
  pending.

## Controlled action

1. Disconnect other clients from the test ESS.
2. Record the phone model, exact OS build, and its LAN IPv4 address. Do not
   record or publish its MAC address.
3. Connect the phone to the shared 5 GHz WLAN near the Globitel and require the
   observer to show one station on the controller AP.
4. Start `capture-roaming-baseline.sh` for 120 seconds. It runs a 100 ms ICMP
   continuity probe and polls both APs for aggregate count, signal and
   advertised BTM/neighbor-report support.
5. Walk steadily to the Cudy, wait at least 60 seconds after a transition, then
   return to the Globitel.
6. Repeat until at least 20 transitions in each direction are captured for a
   compatibility claim. Individual development runs may contain fewer, but
   cannot be marked supported.

Example command:

```sh
tools/lab/capture-roaming-baseline.sh \
  /home/uni/quarantine/wmcs-roaming-phone-a-run-01 PHONE_IPV4 120
```

## Expected observation

The association timeline changes from controller `1`, agent `0` to controller
`0`, agent `1`, and back without simultaneous long-lived dual association. The
phone retains its IP and existing L2 path. Probe sequence gaps quantify the
interruption; no result is called seamless before it meets the class-specific
gate in `docs/requirements/ROAMING.md`.

## Captures and artifacts

| Artifact | Storage reference | SHA-256 | Sanitized fixture committed? |
|---|---|---|---|
| Association timeline | pending under `/home/uni/quarantine/` | pending | no |
| 100 ms probe timeline | pending under `/home/uni/quarantine/` | pending | no |
| Capture metadata/summary | pending under `/home/uni/quarantine/` | pending | no |
| Wireless packet capture | not planned for first pass | — | no |

The prepared tools intentionally collect no station MAC address and no WLAN
credential. The supplied private LAN IP is retained only in the external local
experiment directory.

## Observed behavior

Pending client association and physical walk.

## State diff

- Controller before/after: expected unchanged.
- Agent before/after: expected unchanged.
- Unrelated state preserved: required.
- Services restarted or interrupted: none expected.

## Failure and recovery

- Failure injected: none in the baseline.
- Expected rollback: not applicable; this is passive observation.
- Management connectivity preserved: required.
- Manual recovery required: none expected.

## Interpretation

- Conclusion: pending.
- Confidence: pending.
- Alternative explanations: client scan policy, power saving, signal geometry,
  band preference, background state, and ICMP deprioritization must be
  considered.
- Follow-up experiment: enable owned 802.11k/v assistance through the normal
  WMCS transaction boundary and repeat the same run; evaluate 802.11r
  separately only after k/v evidence.

## Compatibility impact

None until a real client run is complete. AP availability alone is not a
roaming compatibility claim.
