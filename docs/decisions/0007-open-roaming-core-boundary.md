# ADR-0007: Standards-based roaming core at the WMCS boundary

Status: proposed

## Context

WMCS currently coordinates native OpenWrt state while leaving radio operation,
association, packet forwarding, and the L2 backhaul to native services. The
lab has demonstrated a working WDS backhaul, but its roaming-quality
requirements are still unimplemented. Existing open projects show that client
steering is a separate layer from mesh transport:

- hostapd/wpad provide the 802.11k/v/r mechanisms;
- usteer and DAWN provide policy, measurements, peer information, and
  steering;
- WDS, 802.11s, batman-adv, and mesh11sd solve different backhaul problems.

roamd is a useful external behavioral benchmark, but its mesh controller,
backhaul, configuration ownership, update path, provenance, and security model
do not fit the WMCS boundary. Running two mesh managers on the same nodes would
also create competing owners of wireless, bridge, STP, and membership state.

Freenetic and WMCS additionally require an independently explainable public
implementation. The implementation must not become a source-code or
architecture import from roamd or from reverse-engineered vendor materials.

## Decision

WMCS will implement a small, standards-based roaming-core behind the existing
platform adapter. The core will:

1. observe BSSes, STAs, native capability bits, measurements, and transitions;
2. validate ESS/profile and security compatibility;
3. maintain fresh candidate observations and a hysteretic policy;
4. publish or synchronize Neighbor Reports;
5. request Beacon Measurements when supported;
6. send bounded, non-disruptive 802.11v BSS Transition requests;
7. classify the observed association result and enforce cooldown/rate limits;
8. expose redacted reason-coded diagnostics through the versioned WMCS API.

WMCS will not implement a new radio frame format, a private L2 backhaul, or a
second network stack. The current WDS/STP backhaul remains a separate
capability. 802.11r/FT is an opt-in authentication optimization and is not a
prerequisite for the policy engine.

Forced kicks, association denial, and disassociation-imminent actions remain
disabled by default. A client that rejects or ignores BTM is not claimed to be
seamlessly movable.

The first implementation will be derived from public OpenWrt/hostapd
interfaces, public standards, this specification, and WMCS-owned experiments.
roamd, DAWN, and usteer may be used as external comparison fixtures, but their
source code, constants, identifiers, comments, and state-machine structure are
not to be copied.

## Alternatives considered

### Install roamd as the WMCS roaming layer

Rejected for the native path. It introduces a second owner for mesh/backhaul,
wireless configuration, membership, updates, and recovery. Its current
security/provenance findings also exceed the first WMCS release gate.

### Depend on usteer or DAWN

Rejected for the core. Both are valuable open references, but a dependency
would add another policy owner, another configuration surface, and a version-
specific wpad/hostapd contract. Their behavior can be compared in a lab
without making Freenetic depend on them.

### Replace WDS with 802.11s or batman-adv first

Rejected for roaming work. Backhaul transport and client steering are
independent. The current two-node WDS path already provides the required L2
reachability; a new backhaul would multiply failure variables.

### Only enable static 802.11k/v/r and let clients decide

Kept as the safe baseline and fallback, but insufficient for the product goal:
the coordinator must publish accurate neighbors, measure candidates where
possible, and provide reasoned BTM suggestions.

## Consequences

Positive:

- WMCS has one owner for membership, transport, policy, and transactions.
- Native hostapd/wpad remains the executor of standards-defined operations.
- The roaming feature can be tested independently of WDS or 802.11s.
- The implementation remains small enough for the current aarch64 and mipsel
  targets.
- External implementations become differential-test references rather than
  runtime dependencies.

Costs and risks:

- Client behavior remains variable and cannot be forced into a universal
  seamless guarantee.
- The platform adapter must normalize hostapd/wpad differences.
- Thresholds require a measured client/AP matrix instead of copied defaults.
- A future authenticated observation/advice message needs a versioned WMCS
  protocol extension.
- Strict clean-room implementation would require a separate implementation
  role after the research specification is frozen; this ADR does not claim
  that separation retroactively.

## Validation

Before moving this ADR to `accepted`:

- the current phone and at least one second client are captured on both APs;
- hostapd/wpad capability and BTM response parsing is verified on both target
  firmware families;
- unit tests cover candidate eligibility, stale observations, hysteresis,
  cooldown, rejection, timeout, and ping-pong prevention;
- a simulator covers controller loss, agent loss, target loss, duplicate
  events, and delayed association;
- hardware runs cover both directions, band changes, same-band transitions,
  BTM-capable clients, and legacy clients;
- no WDS/STP/foreign UCI object changes occur during a roaming decision;
- the compatibility matrix records exact firmware, client OS, security mode,
  and traffic continuity results.

## Revisit when

Revisit this decision when a second member, multi-hop wireless backhaul,
EasyMesh interoperability, FT key distribution, or a strict separate
implementation team enters scope.
