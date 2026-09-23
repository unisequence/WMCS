# ADR 0006: Authenticated single-WLAN control transaction

Status: accepted for native v0 lab validation

## Context

ADR 0005 establishes a durable relationship key and stable cryptographic peer
identity, but it deliberately authorizes no OpenWrt configuration change. The
wired MVP needs the smallest useful mutation that can prove the Freenetic/WMCS
boundary: copy one existing controller WLAN to one paired agent without making
WMCS a data-plane component, changing radio policy, or overwriting foreign UCI
objects.

The first transaction must remain usable on small OpenWrt targets, expose no
WLAN credential over ubus, reject replay, and recover conservatively after a
daemon or router restart. The protocol is a native WMCS mechanism; it does not
claim compatibility with a vendor mesh protocol.

## Decision

Native v0 uses a fixed 256-byte UDP control frame on port `45125`, accepted
only during an explicit 5..300 second window bound to the configured LAN
interface. A paired relationship key is expanded with direction-specific
HKDF-SHA256 domains. Request and result payloads are protected by AES-256-GCM;
the complete 60-byte header is authenticated as additional data.

The authenticated header contains the protocol version, closed message type,
sender and recipient identity IDs, a durable per-peer sequence, and a random
96-bit nonce. The agent accepts exactly `stored_generation + 1`. A valid
request consumes that sequence even when validation rejects the profile or an
apply rolls back, so the same mutation cannot be replayed.

The first request schema carries exactly one 5 GHz `sae-mixed` WLAN profile:
SSID, key, and a `dry_run` flag. The controller reads the configured source
`wifi-iface` directly through libuci; callers provide neither the secret nor a
UCI selector. The agent targets one configured 5 GHz radio and creates or
updates only `wireless.wmcs_home_5g`. That section must either be absent or
carry both `wmcs_managed=1` and the matching controller peer ID in
`wmcs_owner`.

For apply, the agent:

1. validates the authenticated peer, next sequence, profile, target radio and
   ownership;
2. atomically writes a complete protected backup of `/etc/config/wireless` to
   `/etc/wmcs/wireless.pending`;
3. commits only the owned AP section and asks native OpenWrt services to
   reload/reconfigure the radio;
4. verifies all owned UCI fields and resolves the actual runtime interface
   through `network.wireless status` before requiring its `hostapd.<ifname>`
   ubus object;
5. advances the durable peer generation and removes the pending backup only
   after successful verification;
6. restores the exact backup and reloads wireless if apply or verification
   fails.

At startup, a pending backup whose sequence is newer than the durable peer
generation is rolled back. If the peer generation already includes that
sequence, the applied configuration is retained and the stale backup is
removed.

Package installation, service enablement, and mutation permission remain
separate controls. Both `enabled` and `mutation_enabled` default to `0`.

## Alternatives considered

- Send WLAN credentials through ubus from Freenetic: rejected because it
  expands secret exposure and moves protocol policy into the UI.
- Copy the complete wireless UCI package from the controller: rejected because
  radio capabilities, channels, device names, and foreign configuration are
  device-local.
- Rewrite a factory/default AP section: rejected because absence or a familiar
  name is not ownership proof.
- Begin with a general configuration language: rejected because it creates a
  much larger privileged parser and ownership surface before one narrow path is
  proven.
- Adopt 802.11s or wireless backhaul first: rejected because the initial
  Ethernet-backed AP coordination problem does not require WMCS in the data
  path.
- Use a long-lived TLS session: deferred. The fixed authenticated datagram is
  smaller and sufficient for the single bounded transaction, but it is not a
  general session protocol.

## Consequences

The resulting AP continues forwarding if `wmcsd` or the controller is down.
Radio channel, width, country, power, bridge, DHCP, firewall, and unrelated
wireless sections remain local. Two APs can therefore advertise the same WLAN
over one L2 backhaul without pretending that WMCS already supplies roaming
assistance or wireless mesh routing.

ADR 0008 adds a protected durable result record before generation advancement.
After an agent or controller restart, an authenticated retry of the consumed
sequence now returns its original outcome without repeating the mutation.
ADR 0009 adds an ownership-safe release transaction and a separate guarded
local forget operation. ADR 0011 preserves the controller's exact request and
authenticated response across restart. Multi-entry controller history,
coordinated key-erasure acknowledgement, key rotation, and multiple WLANs
remain future work.

The relationship key provides no per-message forward secrecy. The protocol has
no negotiation or downgrade path in v0: unknown versions, types, algorithms,
reserved bytes, lengths, identities, and states fail closed.

## Validation

- Host tests cover exact frame and payload encoding, reserved bytes, length
  limits, direction-specific encryption, tamper rejection, and malformed
  profiles.
- aarch64 and mipsel OpenWrt cross-builds pass with warnings as errors.
- Crypto spikes execute on both current hardware targets.
- A two-router test performs dry-run, forced verification failure with exact
  rollback, successful apply, both router reboots, and a post-reboot dry-run.
- UCI hashes prove that only the intended wireless file changes; runtime ubus
  and bridge observations prove that the created AP exists on LAN.

The first hardware evidence is WMCS-EXP-0005.

## Revisit when

Durable controller job history, more than one WLAN, multiple active
controllers, 802.11k/v, optional 802.11r, wireless backhaul, or a negotiated
general control protocol enters scope. Release/forget is specified by ADR 0009.
