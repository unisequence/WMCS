# Experiment WMCS-EXP-0007

Status: observed read-only capability baseline

## Question

Which public hostapd/wpad roaming primitives are actually available on the
current Globitel and Cudy lab images, and which 802.11k/v capabilities does the
currently associated phone advertise before any roaming configuration is
enabled?

## Provenance

- Researcher: local WMCS development session
- Date and timezone: 2026-09-20, Europe/Moscow
- Public references: OpenWrt Wi-Fi roaming and wpad package documentation;
  public hostapd `ieee802_11_defs.h` capability definitions
- Clean-room role: research and implementation
- Data handling classification: sanitized local evidence

No roamd binary or source was executed. The capability decoder uses the public
hostapd definitions `WLAN_EXT_CAPAB_BSS_TRANSITION` (bit 19) and
`WLAN_RRM_CAPS_NEIGHBOR_REPORT` (bit 1).

## Topology

```text
development host 192.168.1.182
             |
             | Ethernet management/L2
             v
Globitel controller 192.168.1.1
             |
             | hidden 2.4 GHz WDS backhaul, bridged L2
             v
Cudy agent 192.168.1.2

Client ESS: OpenWrt_5G
AP A: channel 149
AP B: channel 36
```

The development host route to `192.168.1.0/24` used only `enp10s0` with
source `192.168.1.182`. Its default Internet route remained on `enp9s0` via
`192.168.64.1`.

## Devices

| Role | Vendor and model | Firmware / OpenWrt build | Relevant package |
|---|---|---|---|
| AP A | Globitel BT-RB300 | SNAPSHOT `r0+36056-d019f0b2e3`, kernel 6.18.44 | `wpad-basic-mbedtls 2026.08.07~831364bf-r2` |
| AP B | Cudy TR3000 v1 | 25.12.5 `r33051-f5dae5ece4`, kernel 6.12.94 | `wpad-basic-mbedtls 2025.08.26~ca266cc2-r1` |
| Client | private phone | not recorded in this capability preflight | associated to AP B during the corrected sample |
| Observer | Linux workstation | local | SSH/ubus read-only collector |

No client MAC address or WLAN credential was recorded.

## Initial state

- Both 5 GHz client BSSes were enabled under `hostapd.phy1-ap0`.
- AP A reported channel 149; AP B reported channel 36.
- Both APs reported zero 802.11k Neighbor Report transmissions and zero
  802.11v query/request/response counters.
- No `ieee80211k`, `bss_transition`, `ieee80211r`, RRM-report, FT, or mobility
  domain option was present in the current wireless UCI configuration.
- Both Neighbor Report lists were empty.
- The routers remained reachable through the isolated lab route.

Read-only state fingerprints before the controlled calls:

| Device | Managed UCI SHA-256 | Package inventory SHA-256 |
|---|---|---|
| AP A | `e54ee6845a59d811ff03961bad0df14905819775431925591d71ff2997920c85` | `982f5bae778e56ac8f0bf13be6bfcca90ffd622a6dd9451758be02a3a0813bc6` |
| AP B | `f088f5bca023b537c2da8af3f4e1d7caa961a5a19c39cf8ce3e5244a84b77351` | `4923249eb3345f7e245be07631b9061955946ee4076400bcb3f06f641feb99e0` |

The managed UCI digest covers exports of `network`, `wireless`, `firewall`,
and `dhcp` without storing or printing those exports.

## Controlled action

On each AP, over authenticated SSH:

1. list the method signature of `hostapd.phy1-ap0`;
2. call the read-only `get_status`, `get_clients`, `get_features`,
   `rrm_nr_get_own`, and `rrm_nr_list` methods;
3. aggregate client count, signal, RRM octets, and Extended Capabilities on the
   router before returning sanitized values over SSH;
4. recalculate the managed UCI digest.

No enable, set, reload, transition, disconnect, or package operation was
invoked.

## Expected observation

The calls should reveal the compiled hostapd interface without changing UCI.
Because the installed package is the basic wpad variant and no roaming options
are enabled, active BTM steering may be unavailable and client capability
advertisement may be incomplete.

## Captures and artifacts

| Artifact | Storage reference | SHA-256 | Sanitized fixture committed? |
|---|---|---|---|
| Method/capability summary | this experiment record | repository-managed later | yes |
| Corrected 5-second observer output | transient terminal output | not retained | no |
| Raw `get_clients` response | processed only on each router | not retained | no |
| Packet capture | not taken | — | no |

## Observed behavior

### AP method matrix

| Public hostapd operation | AP A | AP B |
|---|---:|---:|
| Read own Neighbor Report | yes | yes |
| Read/set Neighbor Report list | yes | yes |
| Request Beacon Measurement | yes | yes |
| Request link measurement | yes | yes |
| Send BSS Transition request | **no** | **no** |

`rrm_nr_get_own` returned a well-formed local Neighbor Report element on each
AP. `rrm_nr_list` returned an empty list on both. The exact BSSIDs and encoded
elements are deliberately omitted from the public record.

### Corrected client observation

The previous observer counted MAC-key lines in pretty-printed JSON, while
`ubus -S` returns compact one-line JSON. It also queried synthetic `btm` and
`rrm_neighbor_report` fields that these builds do not expose. The observer was
changed to parse the hostapd JSON locally on each AP and decode the published
capability bits without exporting station identifiers.

A corrected five-second sample observed:

- AP A: zero associated clients;
- AP B: one associated client, RSSI between -64 and -67 dBm;
- RRM Neighbor Report capability bit: `0`;
- Extended Capabilities BSS Transition bit: `0`.

These zero bits describe only the current association while the AP has no k/v
configuration. They do **not** prove that the phone hardware or OS lacks k/v
support.

## State diff

- AP A managed UCI before/after: identical
  (`e54ee6845a59d811ff03961bad0df14905819775431925591d71ff2997920c85`).
- AP B managed UCI before/after: identical
  (`f088f5bca023b537c2da8af3f4e1d7caa961a5a19c39cf8ce3e5244a84b77351`).
- Unrelated state preserved: yes, by digest.
- Services restarted or interrupted: none.

## Failure and recovery

- Failure injected: none.
- Expected rollback: not applicable; no mutation was authorized.
- Management connectivity preserved: yes.
- Manual recovery required: no.

## Interpretation

- Conclusion: both current basic wpad builds expose useful 802.11k/RRM read and
  measurement primitives, but neither exposes the active 802.11v
  `bss_transition_request` operation required by the proposed steering core.
- Confidence: high for the method/package baseline; low for intrinsic phone
  capability until the AP advertises k/v and the client reassociates.
- Alternative explanations: the missing BTM method is a package/build feature
  boundary rather than a radio hardware limitation.
- Unknown fields or transitions: client capability advertisement after full
  wpad installation and owned k/v enablement.
- Follow-up experiment: transactionally replace the basic wpad package with
  the matching full variant on a recovery-controlled lab node, verify the BTM
  method appears, enable only WMCS-owned k/v fields, reassociate the phone, and
  repeat the same passive capture before sending any transition request.

## Compatibility impact

The `802.11k/v roaming assistance` capability remains `planned`. This
experiment establishes a package and API release gate; it is not a seamless
roaming or client-compatibility claim.
