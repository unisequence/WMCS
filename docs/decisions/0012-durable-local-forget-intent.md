# ADR 0012: Durable local forget intent

Status: accepted for native v0 implementation; hardware fault validation pending

## Context

`forget_orphan` removes several independent protected records: a controller
operation, an agent result, and a peer relationship. The filesystem does not
commit those paths as one transaction. A process or power interruption between
two removals could therefore leave a peer without its journal, or leave a
journal that points at a peer already removed. The operation must also remain
conservative around an owned WLAN and its pending rollback backup.

## Decision

WMCS writes one fixed-size private `forget.pending` record before changing any
forgettable state. The v1 record contains the exact lowercase peer ID and its
expected peer generation, plus a version, schema, size, and zeroed reserved
bytes. The record is written atomically and its directory is synchronised.

The cleanup order is fixed:

1. confirm that the peer has no WMCS-owned WLAN section and no matching
   `wireless.pending` backup;
2. load the peer and require the recorded generation, when the peer still
   exists;
3. remove only a matching `control.operation` recipient;
4. remove only a matching `control.result` recipient;
5. unlink the exact peer record;
6. remove and synchronise `forget.pending`.

The marker is loaded and completed before normal result, controller-operation,
or WLAN recovery during daemon startup. It is never transmitted and never
changes UCI. If any step returns a storage, generation, or ownership result,
the marker remains for a later explicit retry or the next restart. A malformed
marker fails closed before ubus registration. A missing peer after a prior
successful unlink is treated as idempotent, so the final marker removal can
complete safely.

The same path serves both controller and agent roles. Ubus is only a guarded
front end; it cannot bypass the journal with direct record deletion. Pairing
cannot observe a half-cleaned relationship because recovery runs before ubus
registration and an existing marker prevents a new forget intent from being
replaced.

## Consequences

Crash windows become recoverable without pretending that several filesystem
files are one atomic transaction. The peer generation prevents a stale marker
from deleting a later relationship incarnation. Matching by recipient ID
prevents cleanup for one peer from consuming another peer's single-slot
journal. The price is a small persistent marker and fail-closed startup when
its integrity or the ownership precondition cannot be established.

## Validation

- Source contract checks enforce marker-first ordering, peer-generation
  validation, marker-last removal, startup recovery, and the ubus boundary.
- Host simulator and codec checks remain green.
- AArch64 and mipsel daemon cross-builds and GCC static analysis remain clean.
- Hardware validation must interrupt forget at each durable boundary and prove
  that restart completes only the named peer cleanup while preserving UCI.
