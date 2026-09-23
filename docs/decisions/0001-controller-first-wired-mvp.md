# ADR-0001: Native OpenWrt controller-agent Ethernet MVP

Status: accepted

## Context

WMCS ultimately may support wireless backhaul, multiple agents, and several
protocol adapters. Implementing those dimensions at once would make failures
harder to isolate.

The fastest useful milestone is one OpenWrt controller coordinating one OpenWrt
agent through an Ethernet backhaul using a native, publicly specified WMCS
protocol. It exercises discovery, identity, trust, adoption, one WLAN profile,
persistence, topology, and release without coupling the foundation to an opaque
vendor protocol or wireless parent selection.

## Decision

The first alpha will support:

- one OpenWrt controller;
- one OpenWrt agent from the supported Freenetic target matrix;
- a native versioned WMCS controller-agent protocol;
- Ethernet backhaul only;
- explicit, time-limited adoption;
- one home WLAN;
- read-only topology and health;
- transactional mutation, reboot persistence, and release.

Multiple agents, guest networks, VLAN propagation, wireless backhaul,
802.11k/v/r, Keenetic MWS interoperability, and updates remain outside this
milestone.

## Alternatives considered

- **Keenetic MWS first:** would prove vendor interoperability earlier, but makes
  the first working system depend on slower black-box protocol research.
- **Wireless backhaul in the first milestone:** increases driver, radio, and
  recovery variables before identity and transaction behavior are stable.
- **Read-only tooling only:** useful as a research phase, but insufficient as
  the first product milestone.

## Consequences

The initial protocol work and test lab remain narrow. The design must retain
capability negotiation and adapter boundaries so that the narrow MVP does not
become a hidden assumption in later phases.

## Validation

Ten consecutive factory-clean adoption, reboot, and release cycles between two
OpenWrt devices must finish without manual recovery or changes to unrelated
configuration. Forced failures must exercise rollback. A 24-hour soak must
preserve authenticated membership and bounded resource use.

## Revisit when

- Ethernet-only operation cannot exercise the required identity and profile
  lifecycle;
- resource measurements invalidate the selected OpenWrt target.
