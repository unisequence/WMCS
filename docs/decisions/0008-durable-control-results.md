# ADR 0008: Durable authenticated control results

Status: accepted for native v0 implementation; hardware fault validation pending

## Context

ADR 0006 made the agent-side WLAN mutation transactional, but its encrypted
result existed only in process memory. A restart after the agent committed its
peer generation and before the controller received the UDP result left the
controller one generation behind. Retrying was safe from duplicate mutation,
but returned only `replay_rejected`; it could not distinguish a commit from a
rollback or rejection.

This ambiguity also blocks a coordinated release protocol. An agent must not
delete its relationship key until the controller has learned the exact outcome,
and the controller needs a bounded way to recover after either daemon restarts.

## Decision

The agent atomically stores the exact authenticated control result packet in the
private state tree as `control.result` before advancing the peer generation.
The fixed record has a closed magic/version/schema, a duplicated sequence, and
one 256-byte encrypted control frame. It is owned by the daemon user, has no
group or other permissions, and contains no plaintext WLAN profile or key.

The ordering for every next-sequence request is now:

1. authenticate and validate the request;
2. execute dry-run, apply, or rollback and choose the bounded result;
3. encrypt and atomically persist the result;
4. advance the durable peer generation;
5. retire the WLAN backup when commit cleanup is complete;
6. transmit the result.

If result persistence fails, the sequence is not consumed. A successfully
applied WLAN is restored before the agent accepts a retry. If generation update
fails after result persistence, the cache remains authoritative and recovery
finishes the generation transition before any result is sent.

At startup the agent validates the cached frame against its local identity, the
stored controller relationship key, the closed result codec, and the peer
generation. A cache exactly one generation ahead completes that generation. A
matching committed result then retires the pending backup; a cached
`rollback_failed` result retries rollback and fails startup rather than
discarding a still-needed backup.

After authenticating a repeated request for an already consumed sequence, the
agent returns the cached result. The request need not be byte-identical for
agent-side safety, but ADR 0011 now preserves the exact controller request so a
restart cannot accidentally change operation type or desired state at the same
sequence. New desired state requires the following sequence.

Native v0 still has one bounded latest-result slot because the first milestone
has one controller/agent relationship and one in-flight operation. The slot is
replaced atomically by the next result. Cache lookup scopes the record to its
recipient peer ID before comparing sequence or operation type, so a released
controller's tombstone cannot block the first request from a different paired
controller. A same-peer, same-sequence operation mismatch still fails closed.
A general concurrent multi-peer implementation must use per-peer bounded
records.

## Alternatives considered

- Infer success from the current UCI section: rejected because observed state
  cannot prove which outcome consumed a sequence, and inference would weaken
  ownership and rollback guarantees.
- Advance generation before persisting the result: rejected because the
  original crash window remains.
- Persist the plaintext request and result: rejected because the request
  contains the WLAN credential and is unnecessary for reconciliation.
- Treat every retry as a fresh transaction: rejected because the same sequence
  must identify exactly one outcome.
- Add a general job database immediately: deferred. A fixed single-result
  record closes the current safety gap without widening the privileged parser
  or state model.

## Consequences

Controller retry after rediscovery can now reconcile a lost result even when
either daemon restarted. The agent never repeats the side effect for that
sequence, and a changed controller source WLAN cannot retroactively change the
recorded outcome.

The durable file contains ciphertext and public routing metadata. Its integrity
is checked by both the fixed record codec and the existing AEAD relationship
key. Corruption, wrong ownership, wrong permissions, unknown peers, impossible
generation gaps, and failed strict rollback recovery fail closed.

ADR 0011 adds a bounded durable controller operation record and preserves the
authenticated outcome through restart. It is still a single latest-operation
state rather than general job history. ADR 0009 applies the same
result-before-generation ordering to release and retains a non-authorizing peer
tombstone until explicit local forgetting.

## Validation

- Host tests cover atomic save/load/remove, closed record fields, corruption,
  and exact encrypted-frame round trips.
- Existing control-wire, simulator, redaction, transaction, and repository
  checks remain green.
- Full `wmcsd` cross-builds pass with warnings as errors for aarch64/Filogic and
  mipsel/MT7621.
- Hardware validation must interrupt the agent before result transmission,
  restart each side independently, retry the same generation, and prove one
  mutation, the original outcome, unchanged foreign UCI, and cleanup of both
  pending records.

## Revisit when

Revisit for multiple active controllers, concurrent operations, durable
controller job history, coordinated key-erasure acknowledgement,
relationship-key rotation, or a negotiated protocol version.
