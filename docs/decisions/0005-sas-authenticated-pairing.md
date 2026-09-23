# ADR 0005: SAS-authenticated P-256 identity pairing

Status: accepted for native v0 lab validation

## Context

Bounded discovery in ADR 0004 proves only that an untrusted candidate address
is reachable. The wired MVP needs stable key-based identity and explicit local
administrator consent before any authenticated control session or network
configuration can exist. MAC and IP addresses cannot be identities, and a
shared default credential or unattended first-contact rule would violate the
project's local, explicit-pairing invariant.

Both current OpenWrt targets already provide Mbed TLS 3.6.x with the PSA Crypto
API, P-256, ECDSA, ECDH, SHA-256 and a system random source. Reusing that
platform library avoids adding a second cryptographic runtime to the package.

## Decision

Each device lazily creates a persistent P-256 identity on its first pairing
attempt. Pairing is a separate 5..300 second, interface-bound UDP window on port
`45124`; it is never enabled continuously.

The controller signs a request containing a fresh 128-bit nonce, its identity
public key and a fresh ephemeral P-256 public key. The agent verifies that
signature and signs a response transcript containing the nonce and both sides'
identity and ephemeral keys. Both sides derive a 256-bit relationship key from
P-256 ECDH with HKDF-SHA256 and derive a 48-bit display-only SAS from the full
response transcript.

Both devices must receive an exact local SAS confirmation before storing the
peer. A discovery candidate ID selects the controller's destination address but
does not become an identity or trust anchor. The durable peer ID is derived
from the public identity key.

Identity and peer records are fixed-size, versioned and atomically replaced
under a private state directory. Stop, timeout and confirmation wipe pending
ephemeral private state and derived secrets. No identity private key,
relationship key, ECDH result or reusable bootstrap material is exposed over
ubus.

## Alternatives considered

- Trust on first use without comparison: simpler, but permits a same-LAN
  attacker to interpose during the administrator's pairing window.
- MAC/IP allowlists: unstable attributes with no proof of key possession.
- A printed or factory-wide password: unavailable on existing generic OpenWrt
  devices and unsafe if reused.
- TLS certificates and a local CA at first contact: useful for the later
  control session, but does not remove the need for an authenticated local
  enrollment ceremony.
- 802.11 DPP: valuable for wireless onboarding, but not required for this
  Ethernet-only generic OpenWrt pilot and not uniformly available on the
  current targets.

## Consequences

The relationship is independent of address changes and bootstraps the bounded
authenticated control transaction selected later by ADR 0006. Pairing itself
writes only WMCS-owned protected state; it never writes UCI.

The v0 pilot requires confirmation on both devices and has no confirmation
message between them. One side can therefore retain a peer if the other side
is stopped after only one confirmation. Authenticated control fails closed when
the other side lacks the relationship, but a safe retry/forget path is still
required. Encrypted control and replay behavior are defined by ADR 0006;
coordinated confirmation, identity/relationship-key rotation, and device-theft
recovery remain outside this decision.

The process currently handles untrusted UDP as root. Fixed-size parsing,
interface binding, bounded windows, strict state transitions and malformed
codec tests are mandatory compensating controls.

## Validation

- Host tests cover the 224-byte codec, message types, reserved bytes and common
  malformed lengths/fields.
- aarch64 and mipsel OpenWrt cross-builds must pass with warnings as errors.
- The native identity/ECDSA/ECDH/HKDF spike must run on both hardware targets.
- A two-router run must reach the same SAS, reject a wrong SAS without state
  mutation, store a peer only after the correct SAS, recover identity and peer
  metadata after daemon restart, and leave measured UCI hashes unchanged.

The first hardware evidence is WMCS-EXP-0004.

## Revisit when

Coordinated pairing commit is added, a second crypto provider is required,
identity rotation is implemented, or wireless onboarding enters scope.
