# WMCS Security Policy

Status: pairing, one-WLAN control, and ownership-safe release pilot

WMCS coordinates privileged network configuration and processes untrusted LAN
traffic. Security properties are release gates, not optional hardening work.

## Threat model

The design must account for:

- unauthorized adoption and hostile candidates;
- controller or member impersonation;
- replay, downgrade, reflection, and confused-deputy attacks;
- malicious protocol fields, identifiers, SSIDs, and capability reports;
- cloned or stolen durable identity;
- partial apply, link loss, process crash, reboot, and power loss;
- credential leakage through logs, ubus, diagnostics, captures, or crash data;
- lateral access from guest and untrusted segments;
- unsafe release, reset, package installation, and firmware update behavior;
- resource exhaustion through discovery, parsing, jobs, logs, and state growth.

## Mandatory properties

- Pairing is explicit, local, time limited, and visible to the administrator.
- MAC addresses and IP addresses are attributes, never cryptographic identities.
- Peer identity is key based and verified before configuration mutation.
- Control sessions authenticate messages, protect replay-sensitive state, and
  have bounded lifetimes.
- Version and algorithm downgrade cannot occur silently.
- Every untrusted input has type, length, character-set, state, and resource
  limits before use.
- Browser and protocol input never becomes shell source. Privileged helpers use
  fixed operations and fixed argv layouts.
- No reusable secret is returned by status, topology, diagnostics, or normal
  error responses.
- A complete and tested rollback path exists before the first remote mutation.
- Release and uninstall preserve unrelated working network configuration.
- Package installation and updates are signed and fail closed.

## Secrets and durable state

Identity keys, trust anchors, and session bootstrap material live under
`/etc/wmcs/` with a private directory mode and least-privilege file modes.
Administrator settings belong in `/etc/config/wmcs`; transient observations and
jobs belong under `/tmp/wmcs/`.

Logs use reason codes and redacted identifiers. Code review must reject logging
of WLAN keys, private keys, reusable pairing secrets, bearer tokens, complete
session material, or raw untrusted frames.

Test fixtures use synthetic credentials. Raw captures and device backups remain
outside Git and are handled according to the research policy in
[CONTRIBUTING.md](CONTRIBUTING.md).

## Implemented pairing pilot

The native v0 pilot uses lazy persistent P-256 identities, randomized
ECDSA/SHA-256 signatures, ephemeral P-256 ECDH, and HKDF-SHA256. The request
signature proves possession of the controller identity. The response signature
covers the nonce and both identity and ephemeral public keys. Both devices
derive a 48-bit display-only SAS from that full transcript and store a peer
relationship only after local SAS confirmation.

Identity and peer records are atomically written under `/etc/wmcs/` with mode
`0700` for directories and `0600` for files. Private keys, ECDH output and the
derived relationship key are never returned through ubus. Pending ephemeral
state is erased on stop, timeout and successful confirmation. The store is
bounded to 16 peers, and an agent permits only one active controller.
A separate fixed-size `generation.state` preserves the monotonic local revision
without coupling unrelated per-peer anti-replay sequences.

Pairing still lacks coordinated two-sided confirmation, identity rotation and
automatic recovery from a half-confirmed relationship. The agent enforces one
active controller; released records retain no mutation authority. Pairing
refuses to overwrite an existing peer fingerprint, so relationship replacement
requires guarded local forgetting rather than implicit key rotation.

## Implemented control and transaction pilot

Fixed-size WLAN and release requests/results are protected with AES-256-GCM
under operation- and direction-specific HKDF-SHA256 keys derived from the
paired relationship key.
The authenticated header binds protocol version, message type, sender and
recipient identity IDs, a persistent monotonic sequence and a random 96-bit
nonce. The agent accepts only the next sequence from a peer stored with the
controller role; the controller accepts only the matching response from its
stored agent peer. Neither WLAN credentials nor reusable control keys appear in
ubus responses or logs.

The first platform transaction creates or updates only the explicitly owned
`wireless.wmcs_home_5g` section. It refuses a section lacking both the
`wmcs_managed=1` marker and matching cryptographic owner ID. Before UCI commit,
the complete wireless file is copied to a mode-`0600` pending record. Runtime
verification requires both the owned UCI fields and the real hostapd ubus
object. Failure restores the original file and reloads wireless. At daemon
restart, the pending record is committed or rolled back by comparing its
sequence with the durable peer generation and authenticated result record.
The exact encrypted result is atomically written to mode-`0600`
`control.result` before generation advances. A repeated authenticated sequence
receives that original result; it cannot repeat the side effect or replace the
outcome with changed desired state.

On the controller, the exact encrypted request is atomically written to
mode-`0600` `control.operation` before first transmission. The matching
authenticated response replaces it before local peer state advances. Startup
validates both frames with the relationship key. Pending work is reported for
explicit rediscovery and is never transmitted merely because the daemon
started. Stop and timeout suspend retries without pretending to revoke a UDP
request that may already have reached the agent.

Release deletes the fixed WLAN section only when its section type, managed
marker, cryptographic owner ID, and target radio all match. It uses the same
backup, native reload, runtime verification, durable-result, and rollback
ordering as apply. Commit marks the relationship `released` atomically with its
sequence, so the retained reconciliation key can return only the cached release
result and cannot authorize a new mutation. Local `forget_orphan` refuses to
erase a peer while UCI ownership or a pending backup still names it, and never
modifies UCI itself. It first persists a generation-bound `forget.pending`
intent, removes only records addressed to that peer, unlinks the peer, and
removes the intent last. Startup retries that intent before other recovery;
malformed or generation-mismatched cleanup fails closed.

Current limitations include no per-message forward secrecy after pairing, no
key rotation, no acknowledged simultaneous key erasure, one latest durable
agent result, and one latest durable controller operation. The bounded
transaction also assumes no concurrent manual edits to the wireless file during
its apply/verify window. Freenetic must call only the documented ubus methods
through an explicit least-privilege ACL; it must never receive the WLAN key from
WMCS status or topology methods.

## Experimental paired Neighbor Report channel

The opt-in neighbor channel is separate from WLAN/release control. It is bound
to the configured LAN interface and TTL-one multicast, accepts only an active
paired opposite-role identity, and uses new query/reply HKDF domains over the
same authenticated WMCX envelope. A fresh random challenge, a 15-second reply
window, and one accepted response per peer/challenge prevent stale response
replay without advancing the durable WLAN operation sequence. The payload
contains only bounded SSID and own Neighbor Report data, never a WLAN key or
station identifier. Hostapd reports from the peer are still assertions by a
paired device, not independent proof of RF reachability or security parity.

The controller BSS requires explicit opt-in; the agent additionally verifies
that its BSS carries the matching WMCS owner marker. A private, atomically
written `neighbor-sync.state` records only the exact hostapd list WMCS last
applied and binds it to the local BSSID/SSID. WMCS refuses to overwrite a
nonempty foreign list. A reply older than 30 seconds is removed from desired
state; advisory steering stops whenever the current list is not the exact
authenticated fresh set. This channel does not change wireless UCI or enable
forced disassociation. The runtime hostapd enable API is one-way; disabling
WMCS stops further writes but does not remove those flags until hostapd
restarts. Live radio-restart and hostile-LAN fault tests remain a release gate.

## Privileged boundary

The local ubus method surface separates read operations from explicit control
and mutation operations. The package rpcd ACL grants only the documented
methods on the `wmcs` object and grants no file execution, file access, UCI, or
foreign ubus object. Freenetic still decides which authenticated session
receives read or write scope. Every mutation checks role, paired identity,
cryptographic sequence, local mutation gate, ownership and FSM state inside the
daemon. Filesystem paths are generated internally from validated identifiers;
callers cannot supply arbitrary paths, commands, package filenames, service
names, or UCI selectors.

Longer term, network-facing decoding should be separated from privileged apply.
Until then, fuzzing, bounded allocations, parser timeouts, and simulator-driven
fault tests are required for each exposed decoder.

## Supply chain

- CI actions and external build inputs are pinned to immutable digests.
- Toolchains and SDK archives have recorded cryptographic hashes.
- Artifacts are built from one clean commit and are immutable after publication.
- Build and signing are separate trust steps.
- Packages carry provenance and are verified before installation.
- WMCS has no embedded self-update or alternate trust path.

The current two-router lab has installed locally built APKs with
`--allow-untrusted` after checking exact SHA-256 hashes and package contents.
That is a development-only exception, not a signed release or a supply-chain
assurance claim.

## Reporting a vulnerability

Until a public hosting location and private security contact exist, do not place
live credentials, captures, exploit payloads, or undisclosed vulnerabilities in
an issue or commit. Record only a redacted reference and coordinate disclosure
through the repository owner. A concrete private reporting channel is required
before the first public release.
