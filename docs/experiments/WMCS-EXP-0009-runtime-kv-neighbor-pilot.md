# Experiment WMCS-EXP-0009

Status: runtime k/v, two-node Neighbor Reports, and client capability observed

## Question

With exact full wpad running on both APs, can public hostapd operations enable
802.11k/v at runtime and exchange a bounded two-node Neighbor Report set
without changing UCI or sending a transition request?

## Provenance

- Researcher: local WMCS development session
- Date and timezone: 2026-09-20, Europe/Moscow
- Interface source: public OpenWrt hostapd ubus documentation
- Clean-room role: platform capability experiment

No roamd binary, source, configuration, trace, or constant was used.

## Topology and versions

| Role | Device | wpad package |
|---|---|---|
| Controller AP | Globitel BT-RB300, `192.168.1.1` | `wpad-mbedtls 2026.08.07~831364bf-r2` |
| Agent AP | Cudy TR3000 v1, `192.168.1.2` | `wpad-mbedtls 2025.08.26~ca266cc2-r2` |

Cudy's package transaction had previously interrupted its wireless-only
management path. A normal power cycle restored WDS and proved that full wpad
was committed, the live executable matched the installed file, the AP was
enabled, and the managed-UCI digest was unchanged. APK then reported the
current world as solved and simulated the exact basic-wpad `r2` rollback.

## Initial state

- Both `hostapd.phy1-ap0` objects exposed `bss_mgmt_enable`,
  `rrm_nr_get_own`, `rrm_nr_list`, `rrm_nr_set`, and
  `bss_transition_request`.
- Neither AP had a remote Neighbor Report entry after its latest wpad start.
- No client was associated during the pre-action five-second passive sample.
- No BTM request was pending or sent.

Managed-UCI digests before the runtime action:

| Node | SHA-256 |
|---|---|
| Controller | `5ab679cf28e05b5792ed99cc6974bc69c78ec105cfa8ee2f61231b73300de690` |
| Agent | `5b108c340af411cf591a6fc49106544e4f8a4e98261606c4d4f007174e340223` |

## Controlled action

On each AP, invoke `bss_mgmt_enable` with only these public booleans enabled:

- Neighbor Report;
- Beacon Report;
- link measurement;
- BSS Transition.

The workstation then read each AP's own Neighbor Report, validated its
three-field structure, and supplied that one record to the other AP through
`rrm_nr_set`. The workstation retained neither WLAN credentials nor a capture
of the BSSIDs.

The repeatable implementation is
[`tools/lab/roaming-runtime.sh`](../../tools/lab/roaming-runtime.sh). Its
failure path disables partially enabled runtime flags and clears the runtime
neighbor list.

## Observation

- Both runtime enable calls succeeded.
- Each AP reported exactly one remote Neighbor Report entry.
- Both managed-UCI digests remained byte-identical.
- A single phone associated to the Cudy AP and advertised both BSS Transition
  Management and Neighbor Report support. Its initial observed RSSI was
  -58 to -59 dBm.
- The same association advertised passive, active, and beacon-table
  measurement support. These bits were decoded from the public hostapd RRM
  capability array using the public IEEE definitions carried by hostapd.
- No station identifier was collected or transferred from either AP.
- No transition request, disconnect, deauthentication, radio reload, or UCI
  commit occurred.

The state is intentionally volatile and will be lost when wpad restarts.

## Interpretation

The public hostapd surface is sufficient for an independent WMCS platform
adapter to publish bounded neighbor data and expose non-disruptive BTM support.
The observed phone advertises the required 802.11k/v capabilities. This does
not yet prove that it consumes a Neighbor Report, follows a BTM request, or
roams seamlessly.

The next gate is a controlled walk followed by one non-disruptive BTM request
with `disassociation_imminent=false` and `disassociation_timer=0`. The request
must also pass a multi-sample source-signal gate; a single RSSI sample is not
sufficient evidence. Client probing and gate behavior are tracked separately
in [EXP-0010](WMCS-EXP-0010-client-kv-steering-pilot.md).

Persistent configuration remains deliberately out of scope for this
experiment. It requires a separately reviewed WMCS field-ownership and
rollback transaction for both the controller source BSS and the managed agent
BSS.
