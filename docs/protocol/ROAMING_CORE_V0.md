# WMCS roaming-core v0

Status: proposed research specification; not a compatibility promise

This document defines the boundary and observable behavior of the future WMCS
roaming coordinator. It does not define a new radio protocol and does not
replace hostapd, wpa_supplicant, netifd, nl80211, WDS, or 802.11s.

The initial executable policy model lives in
`simulator/wmcs_sim/roaming.py`; it has no router-side effects.

The implementation must be independently explainable from public 802.11/OpenWrt
interfaces and WMCS lab measurements. Existing implementations are external
behavioral references only. No source, constants, identifiers, comments, or
state-machine structure are imported from roamd, DAWN, or usteer.

## 1. Terminology and scope

- **BSS** — one AP radio identified by a BSSID.
- **ESS profile** — the authenticated WLAN identity that a client may use on
  multiple BSSes. A matching SSID alone is not sufficient.
- **STA** — a client station.
- **source BSS** — the BSS to which a STA is currently associated.
- **target BSS** — a candidate BSS that may provide a better connection.
- **steering** — a standards-based suggestion to the STA, normally an
  802.11v BSS Transition Management request.
- **roaming** — the observed association change, which remains a client-side
  decision.

The core covers observation, candidate selection, Neighbor Report management,
optional Beacon Measurement requests, BSS Transition requests, result
classification, and rate limiting. It does not cover wireless backhaul,
packet forwarding, DHCP, firewalling, or a vendor mesh protocol.

## 2. Ownership boundary

| Layer | Owner | WMCS responsibility |
|---|---|---|
| Radio and association | OpenWrt native services | Observe; request bounded native operations |
| 802.11k/v/r frame handling | hostapd/wpad | Supply capability and ubus/native results |
| Client/AP observations | local WMCS platform adapter | Normalize and timestamp bounded facts |
| Roaming policy | WMCS roaming-core | Select candidates and decide whether to suggest |
| Backhaul and L2 | current WMCS/WDS + native bridge/STP | Keep available; never alter during a roam decision |
| UI/CLI | Freenetic or generic client | Display redacted decisions and results |

The core is not in the data path. If `wmcsd` or the controller stops, existing
associations and native forwarding continue; new steering decisions simply
stop after their bounded freshness window expires.

The current two-node lab uses a managed WDS L2 backhaul with STP. That is a
valid transport for coordination and is intentionally not reclassified as an
802.11s mesh. An eventual 802.11s or batman-adv adapter must remain a separate
backhaul capability.

## 3. Normalized observations

The platform adapter produces bounded, timestamped records. These are internal
facts, not a public API promise.

### 3.1 BSS observation

Each record contains:

- controller-known node identity and local interface scope;
- BSSID, ESS profile identifier, band, frequency/channel, and enabled state;
- native security/PMF/FT compatibility facts needed to compare BSSes;
- local Neighbor Report element, when hostapd can provide one;
- health and backhaul reachability state;
- optional channel utilization and associated-STA count;
- `observed_at` and a bounded freshness class.

The profile identifier is WMCS state. It is not inferred from an SSID, BSSID,
MAC prefix, or section name.

### 3.2 STA observation

Each associated STA record contains:

- a validated internal station key and current BSSID/profile;
- signal/RSSI and, when available, RCPI/RSNI or link measurement data;
- association time and last observation time;
- 802.11k/RRM capability bits;
- 802.11v/BTM/WNM capability bits;
- MBO and 802.11r/FT compatibility facts when exposed by native services;
- last BSS Transition response and result classification;
- steering attempt count, cooldown, and minimum-dwell timestamps.

Raw station identifiers must not appear in normal UI output or logs. Diagnostic
mode may use a stable redacted identifier that cannot be mistaken for a trust
identity.

### 3.3 Events

The adapter should normalize association, disassociation, Beacon Report,
BSS-Transition response, and native error events. Polling remains a fallback;
event timestamps must take precedence over a later coarse poll when both are
available.

## 4. ESS and candidate eligibility

A target BSS is eligible only when all of the following hold:

1. It belongs to the same WMCS ESS profile, not merely the same visible SSID.
2. Security, PMF, network/bridge scope, and any required FT parameters are
   compatible for the STA and source profile.
3. The target is enabled, healthy, reachable through the current control and
   backhaul topology, and not administratively excluded.
4. Its observations and Neighbor Report data are fresh enough for the current
   policy.
5. The target BSSID is present in the trusted native inventory; a client or
   remote report cannot inject an arbitrary destination BSSID.
6. The target is not the current BSS and does not violate the STA's cooldown,
   minimum dwell, or retry budget.

When eligibility cannot be proved, the candidate is rejected with a reason
code. The core must not guess that two APs are interchangeable from matching
SSID/password values alone.

## 5. Measurement and decision policy

The policy is deliberately expressed in terms of validated facts rather than
copied numeric constants.

### 5.1 Measurement order

1. Use the current source-BSS observation as the baseline.
2. If the STA advertises suitable RRM capability, request a bounded Beacon
   Measurement Report for eligible targets or use a fresh report already in
   the cache.
3. Otherwise use fresh native observations from the target APs, clearly marked
   as indirect measurements.
4. Discard measurements older than the profile's freshness limit.

The controller must distinguish “target not measured” from “target measured
and weak”. Missing data is not evidence that a target is good.

### 5.2 Candidate scoring

The initial policy may use:

- source and target signal/link quality;
- a required improvement margin (hysteresis);
- channel utilization and AP load when reliable;
- band preference as a policy input, never as an unconditional override;
- capability and security compatibility;
- recency and measurement confidence.

The target with the highest valid score wins only if it exceeds the source by
the configured margin and passes all safety checks. A short-lived improvement
must not trigger a transition; the observation must remain valid across the
configured confirmation window.

No numeric threshold is release-ready until it has been measured on the
supported AP/client matrix. The values documented by other daemons are useful
comparison points, not WMCS defaults.

### 5.3 Hysteresis and cooldown

Every STA has:

- a minimum dwell time after association or a successful transition;
- a confirmation window for a candidate improvement;
- a bounded number of transition requests per window;
- a cooldown after a rejected, ignored, or failed request;
- a longer block after repeated return to the previous BSS.

The policy must prevent ping-pong between two BSSes even when their measured
signals alternate around the margin.

### 5.4 Action priority

The default action order is:

1. publish or refresh accurate Neighbor Reports;
2. request an optional Beacon Measurement when the STA supports it;
3. send a non-disruptive BSS Transition request with a bounded validity
   period and eligible Neighbor Report list;
4. observe the actual association result;
5. classify the result and enter success or cooldown.

Forced deauthentication, association denial, and disassociation-imminent
requests are disabled by default. They require an explicit administrator
policy, a reason code, a retry budget, and a measurable safety gate. A client
without usable BTM support receives no seamless-roaming claim.

802.11r/FT is an authentication optimization, not the candidate-selection
algorithm. It is enabled only when both APs, the security mode, and the client
path are compatible and the hardware test matrix has passed.

## 6. State machine

```text
associated
    | source/target facts stale or below evaluation gate
    v
evaluating -> measuring -> candidate_ready
                              |
                              v
                         btm_pending
                         /          \
              transition_seen     rejected/timeout
                    |                    |
                    v                    v
                 success              cooldown
                    |                    |
                    +---------> associated
```

Required properties:

- `measuring` has a deadline and cannot hold a client indefinitely;
- `btm_pending` accepts only a response or association event for the same
  decision generation;
- a stale response cannot authorize a new transition;
- success is based on observed target association, not on the request being
  accepted by hostapd;
- a failure never triggers an immediate retry without cooldown;
- controller loss cancels new decisions but does not disconnect a client.

Each decision records a bounded reason enum, source/target BSSID, observations,
capability flags, policy generation, action, response, actual target, and
elapsed time. Secrets and raw frames are excluded.

## 7. Controller/agent coordination

The controller may aggregate observations from multiple agents and choose a
target, but the source agent must revalidate locally before invoking hostapd.
The future authenticated control messages should carry only:

- profile and policy generations;
- redacted STA key or an authenticated internal reference;
- source and target BSSID from the trusted inventory;
- bounded measurements and expiry;
- requested action and decision reason.

An agent rejects stale generations, unknown BSSIDs, incompatible profiles,
expired observations, and actions outside its local policy. A report from a
peer is evidence, not permission to mutate local native state.

When the controller is unavailable, agents may continue passive observation but
must not invent cross-node steering decisions. Native client-directed roaming
continues normally.

## 8. Backhaul independence

The roaming-core depends only on an authenticated WMCS control path and a
bridged ESS. It does not require 802.11s, batman-adv, mesh11sd, or a private
wireless transport.

Backhaul selection and recovery remain WMCS responsibilities. A later wireless
backhaul implementation must expose stable reachability and health facts to
the roaming-core without changing its candidate policy.

## 9. Observability and verification

The lab recorder must correlate:

- hostapd association, FT, Beacon Report, BTM and disconnect events;
- packet-capture timestamps from both APs;
- 100 ms probes and a long-lived TCP/UDP flow;
- source/target signal, band, channel and BSSID;
- requested action, BTM response, actual target, retries and cooldown;
- client IP, ARP/neighbor state, and bridge forwarding state.

The existing requirements in `docs/requirements/ROAMING.md` define the initial
quality gates. No “seamless” claim is made from configuration alone; the
observed authentication exchange and traffic continuity must confirm it.

## 10. Provenance and external references

This specification is based on public standards/OpenWrt documentation and
WMCS-owned measurements. roamd may be used as a quarantined external benchmark
for behavior, but its source, thresholds, terminology, and internal design are
not implementation inputs.

Public reference set:

- [OpenWrt Wi-Fi roaming](https://openwrt.org/docs/guide-user/network/wifi/roaming)
- [OpenWrt usteer configuration](https://openwrt.org/docs/guide-user/network/wifi/usteer)
- [usteer source and policy](https://github.com/openwrt/usteer)
- [DAWN README](https://github.com/berlin-open-wireless-lab/DAWN/blob/master/README.md)
- [DAWN configuration](https://github.com/berlin-open-wireless-lab/DAWN/blob/master/CONFIGURE.md)
- [OpenWrt WDS](https://openwrt.org/docs/guide-user/network/wifi/wifiextenders/wds)
- [Linux 802.11s documentation](https://wireless.docs.kernel.org/en/latest/en/developers/documentation/ieee80211/802.11s.html)
- [Linux batman-adv documentation](https://www.kernel.org/doc/html/v4.20/networking/batman-adv.html)
- [mesh11sd documentation](https://github.com/openNDS/mesh11sd)

## 11. Open decisions

- Exact observation schema and its versioning.
- Controller-side versus source-agent-side scoring split.
- Capability normalization across hostapd/wpad versions.
- FT profile and security-mode compatibility matrix.
- Numeric thresholds, dwell times, and retry budgets.
- Durable decision history and bounded ubus diagnostics.
- Whether a later local-only policy is needed during controller outage.
