# ADR 0010: Durable local revision and bounded peer inventory

Status: accepted for native v0 implementation; restart validation pending

## Context

The original identity store exposed `generation` as the maximum per-peer
control sequence found on disk. That value could fail to change when a lagging
peer advanced, and could regress to zero after the last peer was forgotten and
the daemon restarted. It also made a newly paired peer's initial sequence
depend on unrelated relationships; two devices with different peer histories
could initialize the same new relationship to different sequences.

Release tombstones make peer discovery after restart necessary. A client that
sees only `paired_peer_count` cannot identify which bounded record is safe to
pass to `forget_orphan`.

## Decision

Local state revision and per-peer anti-replay sequence are separate concepts:

- `generation.state` is a protected fixed 16-byte local revision record;
- every peer create/replace, sequence/state advance, and delete persists the
  next local revision before changing the peer file;
- gaps are allowed if the following peer write fails, but the revision never
  regresses after restart or deletion of the last peer;
- every newly SAS-paired relationship starts at per-peer sequence `1`, so its
  first control request is `2` on both devices regardless of unrelated local
  history;
- existing peer records retain their current sequences, and a missing
  `generation.state` is migrated on the next peer mutation after startup scans
  the legacy records.

The private store is bounded to 16 peer records. The local read-only `peers {}`
method returns a peer-ID-sorted array containing only ID, remote role,
active/released state, and per-peer sequence. It never returns key material,
addresses, WLAN data, or filesystem paths.

The revision record is mode `0600`, owner checked, fixed-size, closed-versioned,
atomically replaced, and directory-fsynced. It is written before a peer change;
therefore a power loss may consume an unused local revision but cannot expose a
new peer state under an older revision.

## Alternatives considered

- Continue deriving the value from live peer files: rejected because deletion
  regresses it and independent peer histories can diverge.
- Use one global value as every peer's anti-replay sequence: rejected because
  both sides need an identical relationship-local starting point.
- Return protected peer files or relationship public keys to the UI: rejected
  because cleanup needs only a bounded public identifier and state.
- Keep an unbounded in-memory peer cache: rejected because durable state remains
  authoritative and resource limits must survive restart.

## Consequences

Freenetic can treat reply `generation` as a durable local revision token and
can rebuild its bounded peer view after restart. Per-peer replay protection is
independent and remains visible only as each peer summary's generation.

Local revision gaps are normal and have no protocol meaning. Version 0 has one
durable controller operation slot but no multi-entry job history or per-peer
result journal.

## Validation

- Repository and rpcd ACL contracts include the read-only `peers` method.
- Both target cross-builds pass with warnings as errors and GCC analyzer is
  clean.
- Hardware/target validation must create and forget the final peer, restart,
  prove the local revision does not regress, forget then re-pair devices with
  unequal local histories, and prove the first control request is sequence 2 on
  both sides.

## Revisit when

Revisit for schema migration tooling, more than 16 peers, durable job/event
pagination, or a database-backed multi-controller implementation.
