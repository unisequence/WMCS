# ADR 0009: Ownership-safe release and local relationship forgetting

Status: accepted for native v0 implementation; hardware fault validation pending

## Context

The single-WLAN transaction can create and update one explicitly owned AP, but
previously had no native path to remove it. Deleting a section by name, SSID,
or desired-state absence would violate WMCS ownership rules. Deleting the
relationship key as soon as the agent removes the AP would also make a lost UDP
result unrecoverable: the controller could no longer authenticate a retry and
would not know whether release committed or rolled back.

A release must revoke the old controller's mutation authority even while enough
cryptographic state remains to replay the durable result. Local administrators
also need a conservative way to remove a released or half-paired relationship
without deleting unrelated OpenWrt configuration.

## Decision

Native v0 adds separate authenticated `RELEASE_REQUEST` and `RELEASE_RESULT`
control types with release-specific HKDF-SHA256 domains. The request payload
contains only a closed version/schema and `dry_run` bit. The result reuses the
bounded transaction outcome and reason codec. Separate message types prevent a
cached WLAN result from being interpreted as a release result, or vice versa.

The agent authorizes release only for an active stored controller and the exact
next per-peer sequence. Dry-run verifies ownership without mutation. A live
release may delete only the fixed `wireless.wmcs_home_5g` section when all of
these properties match:

- section type `wifi-iface`;
- `wmcs_managed=1`;
- `wmcs_owner=<authenticated controller peer ID>`;
- `device=<configured target radio>`.

An absent section is an idempotent success. A present section with any missing
or conflicting marker is an ownership conflict and remains untouched.

When the owned section exists, release uses the same protected transaction
boundary as WLAN apply: save the complete wireless file, delete through
libuci, reload native OpenWrt services, verify UCI absence, and verify that
`network.wireless status` no longer reports the section. Failure restores the
backup. The encrypted release result is persisted before generation commit, so
a daemon restart or lost response cannot repeat deletion or change its outcome.

On a committed release, each side atomically advances the peer generation and
marks that peer `released`. Peer-record byte 6 stores this state; existing v0
records use zero and therefore remain active. A released relationship retains
its key only as a reconciliation tombstone: the agent may return the matching
durable release result, but it rejects every new control transaction. Reusing
the same identity for WLAN synchronization requires local guarded forgetting on
both sides followed by a new SAS-confirmed pairing; pairing never overwrites an
existing fingerprint in place. The agent permits only one active controller
relationship at a time.

`forget_orphan {peer_id}` is a separate local administrative operation. It is
available only with the local mutation gate enabled and with discovery,
pairing, and control inactive. It refuses deletion while the peer ID appears in
the managed WLAN owner field or a pending wireless backup. It removes a
matching durable result tombstone and then unlinks the peer record.
Before that cleanup it writes a protected `forget.pending` intent containing
the exact peer ID and generation; the intent is removed only after all matching
records are gone. Startup replays an unfinished intent before ordinary
recovery, and a stale generation or malformed intent fails closed. It never
changes UCI itself. Administrators run it independently on both sides after a
reconciled release, or to remove a confirmed half-pair with no owned or
pending state.

## Alternatives considered

- Delete by section name or SSID: rejected because neither proves ownership.
- Delete all generated-looking fields: rejected because partial markers and
  familiar naming are insufficient authority.
- Delete both relationship keys immediately after release: rejected because a
  lost result would be permanently ambiguous.
- Keep the relationship active until manual forgetting: rejected because the
  old controller would retain mutation authority after an apparent release.
- Add an unbounded distributed acknowledgement protocol now: deferred. A
  non-authorizing reconciliation tombstone plus explicit local forgetting is
  deterministic and fits the current bounded v0 state model.
- Automatically forget on the controller only: rejected because it creates an
  asymmetric orphan and removes the controller's ability to reconcile a lost
  result.

## Consequences

Release now removes only state that the authenticated controller demonstrably
owns, preserves foreign configuration, rolls back on failed verification, and
survives the same crash windows as WLAN apply. A released key cannot authorize
new mutation even before it is physically erased.

Trust erasure is deliberately local rather than pretending that two UDP peers
can prove simultaneous deletion. A completed release can therefore leave
bounded released peer records until each administrator invokes
`forget_orphan`. Its durable intent makes multi-file local cleanup recoverable
across process and power interruption without broadening the deletion scope.
`paired_peer_count` includes these tombstones;
`active_controller_count` reports only controllers that still have authority.

The complete-file rollback inherited from the v0 WLAN transaction still
assumes no concurrent administrator edits during its bounded apply window.
General field-level journaling and transaction locking remain future work.

## Validation

- Host codec tests cover both release message types, closed release payloads,
  reserved-byte rejection, and durable release-result storage.
- Existing simulator release tests prove owned-only deletion and rollback.
- Full daemon cross-builds pass with warnings as errors for aarch64/Filogic and
  mipsel/MT7621.
- Hardware validation must cover dry-run, successful release, foreign-marker
  refusal, verification rollback, daemon and power interruption at each durable
  boundary, result retry after both-side restart, rejection of post-release
  WLAN sync, and local forgetting on both nodes.

## Revisit when

Revisit for multiple active controllers, per-peer result journals, coordinated
key-erasure acknowledgement, field-level rollback under concurrent UCI edits,
controller replacement policy, or a negotiated protocol version.
