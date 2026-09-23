# ADR 0011: Durable controller operation reconciliation

Status: accepted for native v0 implementation; hardware fault validation pending

## Context

ADR 0008 made the agent's authenticated result durable, but the controller's
outgoing request and received outcome still existed only in process memory. A
controller restart could therefore forget which operation occupied the next
per-peer sequence. Re-reading a changed source WLAN or issuing release at that
same sequence could collide with the agent's cached result type and leave the
relationship impossible to reconcile automatically.

Automatically transmitting a live mutation immediately after boot is also
unsafe. The old destination address may be stale and the administrator's
bounded operation window has ended.

## Decision

The controller keeps one fixed-size protected `control.operation` record. It
contains a closed header plus the exact 256-byte encrypted request and, after a
result arrives, the exact encrypted response. It contains no plaintext WLAN
profile. The file is atomically replaced, owned by the daemon user, and has no
group or other permissions.

The durable ordering is:

1. validate the local request and encrypt its fixed payload;
2. open the bounded socket without transmitting;
3. atomically persist the pending request;
4. transmit and retry that exact frame;
5. authenticate and decode the matching result;
6. atomically replace the record with request plus result;
7. advance the local peer sequence and release state;
8. expose the completed bounded outcome.

A restart with a pending record validates its local identity, remote peer,
request AEAD, payload schema, peer state, and exact next sequence. It exposes
`recovery_pending` but does not transmit. The administrator or UI must
rediscover the peer and repeat the same start call with the same peer,
operation, and `dry_run` value. WMCS then sends the preserved frame to the
freshly observed address. Changed source WLAN state is not read until the
pending sequence is resolved.

A restart with a completed record validates both AEAD frames and finishes a
missing local peer transition before exposing `complete`. The completed result
therefore survives a restart between receipt and UI polling. Starting the next
operation atomically replaces an ordinary completed record; calling the stop
method acknowledges and removes it.

Timeout and stop suspend network retries but retain a pending record. They do
not claim to cancel a request that may already have reached the agent. Daemon
shutdown also preserves both pending and completed records.

An authenticated `replay_rejected` result is non-consuming because the agent
does not advance for an out-of-sequence request. It remains a reconciliation
condition rather than allowing a different operation to reuse that sequence.
Explicit guarded peer forgetting can retire the record together with an orphan
relationship.

## Crash boundaries

| Interruption point | Durable state | Recovery |
|---|---|---|
| Before pending write | Previous completed record or none | No packet was sent |
| After pending write, before first send | Pending exact request | Explicit rediscovery and resume |
| After send, before agent result | Pending exact request | Resume; agent executes once or returns cache |
| After completed write, before peer advance | Authenticated request and result | Startup advances peer once |
| After peer advance, before status update | Completed record plus advanced peer | Startup verifies matching state and exposes result |
| After completed-record acknowledgement | No operation record | Peer transition remains authoritative |

## Alternatives considered

- Recreate a request from current source WLAN after restart: rejected because
  one sequence must identify one immutable operation and result type.
- Automatically resend on daemon startup: rejected because a reboot must not
  silently extend an expired mutation window or trust a stale address.
- Store plaintext desired state: rejected because only the already encrypted
  wire frame is needed for reconciliation.
- Persist only the request: rejected because a crash after local peer advance
  would lose the authenticated outcome presented to the UI.
- Add an unbounded job database: deferred. Native v0 permits one in-flight
  control operation, so a closed single-slot record matches the protocol
  boundary and keeps privileged parsing bounded.

## Consequences

Controller and agent restarts no longer erase the identity of an in-flight
operation. A resumed operation preserves its original nonce, ciphertext,
operation type, sequence, and intended `dry_run` mode while using only a fresh
untrusted discovery observation for routing.

The protected ciphertext is not a substitute for full at-rest encryption: the
same protected state tree also contains the relationship key. Filesystem
ownership, mode checks, a closed codec, and AEAD validation provide corruption
and privilege-boundary protection; a local root compromise remains outside the
threat model.

The v0 record is latest-operation state, not general history. Multiple
concurrent agents, multi-entry retention, explicit remote result
acknowledgement, and coordinated key erasure remain later protocol work.

## Validation

- Host tests cover pending and completed round trips, operation/sequence
  mismatch, forbidden pending response bytes, unsafe permissions, corruption,
  atomic replacement, and removal.
- A source contract checks pending-before-send,
  completed-before-peer-advance, startup recovery, and shutdown preservation.
- Both target ABIs compile with warnings as errors; GCC static analysis and
  ASan/UBSan cover the new fixed-record codec.
- EXP-0017 must still exercise controller and agent process kills and power
  loss at every ordering boundary on the two-router lab.

## Revisit when

Revisit for multiple concurrent operations, a bounded historical job API,
remote result acknowledgement, relationship-key rotation, or controller
replacement without local orphan cleanup.
