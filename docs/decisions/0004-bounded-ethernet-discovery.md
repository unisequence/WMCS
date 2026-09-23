# ADR 0004: Bounded native discovery over scoped UDP multicast

Status: accepted for read-only lab validation

## Context

The first two-router phase needs to prove that an OpenWrt controller can observe
an OpenWrt agent on one Ethernet LAN before identity enrollment, authenticated
control, or configuration mutation exists. Continuous advertisement, generic
service discovery, and a listener on every interface would enlarge the attack
surface and conflict with the explicit-window model.

## Decision

Use an experimental IPv4 UDP multicast transport on administratively scoped
group `239.255.77.67`, port `45123`, with TTL 1 and an explicitly configured
interface (`br-lan` by default).

Both sides require a local `discovery_start` call with a duration from 5 through
300 seconds. The socket does not exist outside that window. A controller sends
a fixed-size probe containing a fresh nonce; an agent returns an equal-size
unicast announcement that echoes the nonce. The controller accepts responses
only for its current nonce and retains at most 16 observations for at most 300
seconds.

The v0 datagram is exactly 40 bytes. It has fixed magic, version, type, size,
nonce, 128-bit boot-instance identifier, role, and zeroed reserved bytes. There
are no variable-length fields, credentials, reusable identifiers, model names,
or configuration values.

The instance identifier is random on every daemon start. It is explicitly
reported as `ephemeral_untrusted` and cannot authorize pairing or mutation.

## Alternatives considered

- mDNS/DNS-SD: useful later, but adds a separate daemon/API dependency and does
  not itself provide the nonce-bound bounded window.
- LLDP: good topology input, but inappropriate as an adoption identity or
  challenge/response mechanism.
- Broadcast JSON: easy to inspect but creates variable-length hostile parsing
  before a schema and authentication layer exist.
- Continuous agent announcements: operationally convenient but violates the
  explicit local discovery window.

## Consequences

This proves only same-LAN reachability and bounded candidate observation. It is
not authentication, identity, pairing, adoption, topology proof, or roaming.
The fixed packet can later be replaced without changing the ubus boundary.

The early daemon still parses LAN input as root. The strict fixed-length codec,
interface binding, equal-size response, node cap, TTL 1, short lifetime, and
host-side malformed-input tests are mandatory compensating controls.

## Validation

- Completed: codec round-trip and malformed-packet tests.
- Completed: aarch64 and mipsel builds with warnings as errors.
- Completed: two-router HIL observation in both start orders; see
  [WMCS-EXP-0003](../experiments/WMCS-EXP-0003-two-router-bounded-discovery.md).
- Partial: explicit stop and timeout close the discovery window; 300-second
  candidate eviction still needs direct hardware validation.
- Completed for the tested pair: all measured UCI hashes remained unchanged.

## Revisit when

Authenticated identity enrollment is designed, IPv6 discovery is required, or
multi-interface/VLAN discovery enters scope.
