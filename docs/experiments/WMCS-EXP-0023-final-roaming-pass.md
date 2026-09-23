# WMCS-EXP-0023 — final passive roaming pass

Status: observed negative handoff result; continuity passed, repeatable roaming
is not closed

## Question

Does the current Globitel BT-RB300 / Cudy TR3000 pair naturally move the Poco
F6 between the shared 5 GHz APs during one ordinary walk from the controller
to the Cudy and back, without disrupting an active connection?

## Setup

- Controller AP: Globitel BT-RB300, `hostapd.phy1-ap0`, channel 149
- Agent AP: Cudy TR3000, `hostapd.phy1-ap0`, channel 36
- Client: Poco F6, discovered from the local DHCP lease as `192.168.1.166`
- Capture: passive association observer plus 100 ms ICMP continuity probe
- Duration: 120 seconds
- Station identifiers and WLAN credentials were not collected in the evidence.

## Result

- 100 association samples covered approximately 119 seconds.
- The controller retained one associated client for the entire observation.
- Controller RSSI ranged from `-35` to `-76 dBm`, average approximately
  `-56.6 dBm`.
- The Cudy reported zero associated clients for the entire observation.
- No controller → Cudy or Cudy → controller handoff was observed.
- While the client was visible on the controller, it advertised BTM and
  Neighbor Report capability (`1/1`) in the hostapd observation.
- ICMP continuity: 1193 replies from 1193 probes, 0% loss, maximum internal
  missing interval `0 ms`; RTT min/avg/max was `1.835/11.818/118.578 ms`.

## Interpretation

This pass proves that the route stayed connected while the source AP signal
fell below the WMCS `-68 dBm` trigger value, but it does not prove a roaming
transition. The phone remained sticky to the controller even at approximately
`-76 dBm`, and the Cudy never became the active serving AP.

The result is useful rather than inconclusive: the current passive path is
stable, but natural roaming is not repeatably demonstrated in this physical
layout/client policy. The earlier positive handoff observations remain valid
as isolated evidence, not as a production guarantee.

## Artifact and next decision

Sanitized capture artifacts are outside Git at
`/home/uni/wmcs-roaming-final-20260923/` (`summary.txt`, `associations.tsv`,
`probe.tsv`, and metadata). No WMCS UCI or daemon state was changed by this
pass.

Before claiming seamless roaming, run one controlled assisted-BTM pass with
runtime Neighbor Reports enabled, then decide whether the policy belongs in
Freenetic or remains an optional advisory feature. The WMCS core should not
force a client that ignores BTM.
