# Roaming quality requirements

Status: initial product requirement

The implementation boundary is specified in
[ROAMING_CORE_V0.md](../protocol/ROAMING_CORE_V0.md). The requirements here
remain the acceptance criteria; the protocol document remains proposed until
the lab gates below have hardware evidence.

WMCS aims to make movement between supported access points unnoticeable during
ordinary interactive traffic. "Seamless" is a measured compatibility claim,
not a universal promise: the client ultimately decides when and where to roam.

## Responsibilities

WMCS can improve roaming by:

- keeping participating APs in one correctly bridged ESS;
- publishing accurate 802.11k neighbor information;
- suggesting a better BSS through 802.11v BSS Transition Management;
- enabling 802.11r only for compatible security modes, APs, and clients;
- making steering decisions from fresh observations with hysteresis;
- preserving the client IP address and established L2 path;
- detecting refusal, failed transitions, loops, and client-specific capability
  differences.

WMCS cannot force every phone to roam well. It must not hide client behavior by
calling a disconnect/reconnect cycle seamless.

## Safety rules

- Passive observation and standards-based hints are preferred over forced
  deauthentication.
- Forced disconnect is disabled by default and requires an explicit policy.
- A client that rejects or ignores a transition request enters a cooldown; it
  is not hammered with repeated requests.
- Steering uses hysteresis and a minimum dwell time to prevent ping-pong.
- Decisions include a reason code, source observations, target BSS, and result.
- Thresholds are policy inputs validated on the supported hardware/client
  matrix; they are not copied from another implementation.
- Roaming configuration changes only owned fields and uses the normal WMCS
  transaction path.

## Provisional quality gates

The numeric budgets are initial targets to validate on hardware. Results are
reported per client model, OS build, security mode, band pair, and AP pair.

| Class | Transition disruption p95 | Packet-loss budget | Required behavior |
|---|---:|---:|---|
| 802.11r-capable supported client | <= 150 ms | At most 1 consecutive lost 100 ms probe | Same IP; established sessions survive |
| 802.11k/v client without usable FT | <= 800 ms | At most 8 consecutive lost 100 ms probes | Same IP; TCP session survives |
| Legacy client | Measured, no seamless claim | Reported, not hidden | No steering loop or forced-kick storm |

For each supported client/AP/security combination:

- at least 20 controlled transitions in each direction;
- at least 95% reach the intended compatible BSS without manual intervention;
- no DHCP renewal caused by the transition;
- no address change or topology loop;
- no return to the previous BSS within 60 seconds unless the target link fails;
- continuous voice or equivalent real-time traffic remains usable for the
  supported seamless classes.

## Measurements

Record both protocol and user-visible timing:

- hostapd association, authentication, FT, BTM, and disconnect events;
- packet capture timestamps at both APs;
- 100 ms ICMP/UDP probes and a long-lived TCP flow;
- client IP, ARP/neighbor and bridge forwarding state;
- RSSI, band, channel, BSSID, requested target, and actual target;
- authentication algorithm and whether FT was actually used;
- transition reason, retries, cooldown, and failure classification.

A result is not classified as FT merely because `ieee80211r` was configured.
The observed authentication exchange must confirm it.

## Initial scenarios

1. Controller AP to wired agent AP and back.
2. 2.4 GHz to 5 GHz on different APs and back.
3. Same-band transition with controlled attenuation.
4. Client ignores BTM request.
5. Target rejects association or becomes unreachable.
6. Agent link fails during transition.
7. Repeated movement near the decision boundary.
8. Mixed WPA2, WPA3-SAE, and transition modes where supported.
9. Sleep/wake and background-phone behavior.
10. Multiple active clients with different capabilities.

## Relationship to roamd

Published reports about roamd provide a useful external performance benchmark
and a catalogue of test situations. WMCS does not reuse its source, constants,
thresholds, or state-machine implementation. WMCS performance claims come from
its own repeatable measurements against this document.
