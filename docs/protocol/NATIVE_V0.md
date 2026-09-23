# Native WMCS protocol v0

Status: implemented experimental discovery, pairing, single-WLAN control, and
ownership-safe release

Version 0 is not a compatibility promise. It freezes the safety and state
machine invariants exercised by the simulator, daemon, host tests, and current
two-router lab. It is a native WMCS protocol, not an implementation of Keenetic
MWS, EasyMesh, or 802.11s.

## Roles and boundaries

- **Controller:** opens bounded discovery/pairing activity, authorizes a peer,
  reads one local source WLAN, and initiates a control transaction.
- **Agent:** advertises only during bounded discovery, proves its identity,
  validates the controller, and applies only its explicitly owned WLAN object.

A device may be built with either role, but a live exchange has one controller
and one agent. Packet forwarding, bridging, DHCP, firewall, radio operation,
and client roaming remain native OpenWrt responsibilities.

All v0 network sockets are IPv4 UDP, bound to one configured interface, and
exist only for an explicit 5..300 second operation. Discovery uses port
`45123`, pairing `45124`, and authenticated control `45125`.

## Discovery

Discovery is deliberately untrusted and carries no reusable secret or mutation
authority:

- IPv4 multicast group `239.255.77.67`, port `45123`, TTL 1;
- one explicitly configured interface;
- fixed 40-byte datagrams and at most 16 retained candidates;
- controller nonce echoed by an equal-size agent response;
- random per-boot candidate IDs marked `ephemeral_untrusted`;
- candidate observations expire after the bounded retention period.

A retained candidate selects a destination address for pairing or control. It
never becomes a peer identity or authorization decision.

## Identity pairing

1. The administrator runs bounded discovery and selects one observed candidate
   on the controller.
2. Separate pairing windows are opened on agent and controller.
3. Each device lazily loads or creates a persistent P-256 identity.
4. The controller sends its identity and ephemeral P-256 key with a fresh
   128-bit nonce and an ECDSA/SHA-256 signature.
5. The agent verifies it and returns its identity and ephemeral key. Its
   signature binds the nonce and both identity and ephemeral keys.
6. Both derive a relationship key with P-256 ECDH and HKDF-SHA256 and display
   the same 48-bit SAS as `xxxx-xxxx-xxxx`.
7. Both local administrators confirm that exact SAS. Only then is the peer
   identity and relationship key stored.

The fixed pairing packet is 224 bytes:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | Magic `WMCP` |
| 4 | 1 | Version `0` |
| 5 | 1 | Closed message type: request or response |
| 6 | 2 | Big-endian packet size, `224` |
| 8 | 16 | Controller nonce |
| 24 | 65 | Uncompressed P-256 identity public key |
| 89 | 65 | Uncompressed ephemeral P-256 public key |
| 154 | 64 | PSA raw ECDSA signature (`r || s`) |
| 218 | 6 | Required zero padding |

The durable peer ID is the lowercase hexadecimal encoding of the first 16
bytes of SHA-256 over its public identity key. Identity private keys, ephemeral
private keys, ECDH output, and the relationship key never cross ubus or the
wire. Stop, timeout, and failed confirmation erase pending secret state.
The protected store accepts at most 16 peer records. An agent permits only one
of those records to be an active controller; released controller records are
non-authorizing reconciliation tombstones. Pairing never overwrites an existing
peer fingerprint or rotates its relationship key implicitly. The same identity
must be explicitly forgotten after ownership checks before it can be paired
again; a different controller identity may pair once no active controller
remains.

Pairing v0 still has no coordinated confirmation message. If only one side is
confirmed, a half-paired record can remain there; later authenticated traffic
fails unless both devices possess the same relationship.

## Authenticated control frame

Control uses a fixed 256-byte `WMCX` frame:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | Magic `WMCX` |
| 4 | 1 | Version `0` |
| 5 | 1 | Type: `1` WLAN request, `2` WLAN result, `3` release request, `4` release result |
| 6 | 2 | Big-endian packet size, `256` |
| 8 | 16 | Sender cryptographic identity ID |
| 24 | 16 | Recipient cryptographic identity ID |
| 40 | 8 | Big-endian nonzero per-peer sequence |
| 48 | 12 | Random nonzero AES-GCM nonce |
| 60 | 180 | Encrypted fixed-size payload |
| 240 | 16 | AES-GCM authentication tag |

The relationship key is expanded by HKDF-SHA256 using separate WLAN
`WMCS-CONTROL-C2A-V0`/`WMCS-CONTROL-A2C-V0` and release
`WMCS-RELEASE-C2A-V0`/`WMCS-RELEASE-A2C-V0` domains plus ordered sender and
recipient IDs. AES-256-GCM authenticates the complete 60-byte header as
additional data and protects the 180-byte payload. Unknown versions, message
types, sizes, algorithms, identities, reserved bytes, and out-of-state packets
are rejected before privileged work. A cached result must match both sequence
and operation-specific result type.

The controller atomically stores the exact encrypted request before its first
transmission, then retries that frame once per second until a result or
operation deadline. An authenticated duplicate for the latest consumed
sequence receives the durable cached result without repeating the side effect.
After controller restart, WMCS requires explicit rediscovery and a matching
start call before retransmitting the preserved frame to the fresh observed
address.

## Single-WLAN request, release request, and results

The 180-byte request payload is intentionally narrow:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | Payload version `0` |
| 1 | 1 | Profile schema `1` |
| 2 | 1 | `dry_run`, `0` or `1` |
| 3 | 1 | Encryption enum `1` = `sae-mixed` |
| 4 | 1 | SSID length, 1..32 |
| 5 | 1 | Key length, 8..63 |
| 6 | 1 | Band `5` = 5 GHz |
| 7 | 1 | Required zero |
| 8 | 32 | SSID bytes followed by zero padding |
| 40 | 64 | Key bytes followed by zero padding |
| 104 | 76 | Required zero padding |

Embedded NUL bytes are rejected. The result payload uses version `0`, schema
`1`, a closed outcome enum and a closed reason enum in its first four bytes;
all remaining bytes are zero. Outcomes are `dry_run_ready`, `committed`,
`rolled_back`, `rejected`, and `rollback_failed`. Public reason strings include
`invalid_profile`, `ownership_conflict`, `platform_failure`, `verify_failure`,
`replay_rejected`, `mutation_disabled`, and `internal_error`.

The release request payload uses version `0`, schema `1`, and a `dry_run` bit
in its first three bytes; all remaining bytes are zero. It carries no selector,
section name, credential, or arbitrary desired state. Its separate authenticated
message type fixes the only eligible object and operation.

The WLAN credential is read internally from the controller's configured
source section. It is never accepted from or returned to ubus.

## Sequence and transaction behavior

Each durable peer record stores the last consumed per-peer sequence. The first
request after pairing is sequence 2 because every new relationship starts at
sequence 1. The agent
accepts only the next value and consumes it after authenticating and processing
the request, including validated rejection or successful rollback. The
controller advances only after authenticating the matching result.

A dry-run validates source schema, target radio, ownership, and peer sequence
but does not require the mutation gate and performs no UCI mutation. Apply is
one bounded agent-side transaction:

```text
controller                              agent
    |                                     |
    | persist encrypted request            |
    |-- encrypted WLAN request ---------->|
    |                            authenticate/validate
    |                            write durable backup
    |                            apply owned UCI object
    |                            reload native services
    |                            verify UCI + hostapd
    |                            persist encrypted result
    |                            commit generation
    |<--------- encrypted result ---------|
    | persist result, commit local peer    |
```

The target section is fixed as `wireless.wmcs_home_5g`. It can be created when
absent or changed only when both `wmcs_managed=1` and `wmcs_owner=<controller
peer ID>` match. Names, SSIDs, MAC addresses, and absence from desired state
are not ownership proof.

Before apply, the complete wireless file, its mode, request sequence, owner,
size, and SHA-256 digest are written atomically to protected
`wireless.pending`. Verification requires all intended owned UCI fields and the
real `hostapd.<ifname>` object resolved from `network.wireless status`. Failure
restores the complete backup and reloads wireless. On daemon restart, a pending
sequence with no matching durable result is rolled back. A validated result one
generation ahead completes the generation first. A committed result then
retires its stale backup, while `rollback_failed` retries restoration and fails
closed if the backup still cannot be applied.

The exact encrypted result is atomically stored as protected
`control.result` before peer generation advances. A controller that timed out
may rediscover the agent and call `wlan_sync_start` again. If its local peer
generation is still behind, the authenticated repeated sequence receives the
original result and reconciles without a second mutation. Native v0 retains
only the latest result for its single-active-controller/single-operation
boundary. The agent refuses pairing a second active controller; released
relationships are non-authorizing tombstones and do not count toward that
limit.

The controller's protected `control.operation` stores the exact encrypted
request before send and atomically adds the authenticated response before local
peer generation advances. Pending records survive timeout, stop, process
restart, and reboot, but are never transmitted automatically at startup. A
matching operation, peer, and `dry_run` value plus a fresh discovery observation
resume the original sequence. Completed outcomes survive restart until a new
operation replaces them or stop acknowledges them. A non-consuming
`replay_rejected` result remains a reconciliation condition and cannot be
silently replaced by another operation at that sequence.

Local relationship forgetting has its own protected `forget.pending` intent.
The intent records one peer ID and the expected peer generation before any
matching operation/result record or peer file is removed. Recovery processes it
before ordinary controller or agent recovery, checks that no owned WLAN or
pending backup names the peer, removes only matching records, unlinks the peer,
and removes the intent last. A crash leaves the intent for retry; it never
causes UCI changes or automatic network transmission.

## Ownership-safe release and forgetting

Release accepts only an active paired controller and its exact next sequence.
Dry-run checks the same preconditions without changing state. The fixed
`wireless.wmcs_home_5g` section is absent-or-owned only when its type is
`wifi-iface`, `wmcs_managed=1`, `wmcs_owner` equals the authenticated controller
ID, and `device` equals the configured target radio. Any conflict is preserved
and reported.

For an owned section, the agent writes `wireless.pending`, deletes only that
section through libuci, reloads native services, verifies UCI absence, and
requires `network.wireless status` to stop reporting the runtime section.
Failure restores the complete backup. An already absent section commits
idempotently without inventing ownership over any other object.

The release result follows the same result-before-generation order as WLAN
sync. A committed result atomically advances the peer and changes its durable
state from `active` to `released` on both sides. A released relationship can
authenticate only retrieval of its matching cached release result; it cannot
authorize WLAN sync or a new release sequence. New authority requires a fresh
SAS-confirmed pairing.

`forget_orphan` is intentionally local. With mutation enabled and no active
discovery, pairing, or control window, it removes a peer only when neither the
managed WLAN owner field nor `wireless.pending` names that peer. A matching
result tombstone is removed first. It never edits UCI. Both devices perform
local forgetting after release reconciliation; this avoids claiming impossible
simultaneous key deletion over lossy UDP.

## Secrets and observability

WLAN credentials and reusable pairing material never enter normal status,
candidate lists, results, reason text, or logs. Identity and peer files and the
pending transaction backup live in a private state tree. All normal APIs expose
bounded enums and public identity fingerprints rather than secret material.
The local `peers` method exposes at most 16 sorted summaries containing only
peer ID, role, active/released state, and generation so a client can safely
offer orphan cleanup after restart.
The reply-level `generation` is a separate protected local revision persisted
in `generation.state`; it advances for peer create, update, and deletion and
does not regress when the final peer is forgotten.

## Implemented v0 capabilities

- bounded Ethernet discovery;
- persistent P-256 identity and SAS-confirmed pairing;
- encrypted, authenticated, replay-protected one-request control;
- one owned 5 GHz `sae-mixed` WLAN profile;
- dry-run, apply, runtime verification, rollback, and restart recovery;
- durable agent-result and controller-operation reconciliation across daemon
  restart;
- ownership-safe dry-run/release, non-authorizing released peers, and guarded
  local forgetting;
- read-only daemon and platform capability reporting.

Not implied: seamless roaming, 802.11k/v/r, client steering, wireless
backhaul, VLAN propagation, multiple active controllers, automatic two-sided
key erasure, coordinated updates, or vendor-protocol interoperability.

## Open decisions

- bounded multi-entry controller history and remote result acknowledgement;
- relationship-key rotation and state migration;
- coordinated pairing confirmation and half-paired recovery;
- controller replacement and acknowledged two-sided key erasure;
- a negotiated general control protocol and version migration;
- multiple agents, profiles, and concurrent transactions;
- Freenetic session wiring and final rpcd ACL name/version policy;
- roaming assistance and measurement gates.

Each addition requires an ADR, malformed-input tests, both target cross-builds,
and hardware evidence before the compatibility matrix can claim support.
