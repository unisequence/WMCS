# WMCS ubus API v0

Status: implemented experimental API; not a compatibility promise

This is the local boundary intended for Freenetic, a future LuCI client, and
lab tooling. Clients must not parse daemon logs, inspect protected state files,
or reproduce the native wire protocol.

## Object and common rules

The object is `wmcs`. Successful replies include `api_version: 0` and the
current durable local `generation`. This is a revision token, not a per-peer
control sequence; gaps are valid. Version 0 can change before the first release.

Rules:

- unknown, duplicate, incorrectly typed, or oversized request fields fail;
- callers cannot supply IP addresses, filesystem paths, shell commands, UCI
  selectors, service names, package filenames, or trust-store locations;
- candidate IDs are untrusted observations, while peer IDs refer to paired
  cryptographic identities;
- WLAN credentials are read internally by the controller and never enter an
  ubus request or response;
- active discovery, pairing, and control windows are mutually exclusive;
- an unresolved controller operation permits discovery for a fresh address but
  blocks pairing and a different control operation;
- service enablement and `mutation_enabled` are separate package settings and
  both default off;
- methods return bounded state directly in v0; there is no job object yet.

## Implemented method classes

| Class | Methods | Intended ACL |
|---|---|---|
| Observe | `status`, `roaming_status`, `capabilities`, `nodes`, `identity`, `peers`, `pairing_status`, `wlan_sync_status`, `release_status` | Authenticated read |
| Discover | `discovery_start`, `discovery_stop` | Network administrator |
| Pair | `pairing_start`, `pairing_confirm`, `pairing_stop` | Network administrator plus explicit local confirmation |
| Control | `control_listen`, `wlan_sync_start`, `wlan_sync_stop`, `release_start`, `release_stop` | Network administrator; live mutation additionally requires the daemon mutation gate |
| Trust cleanup | `forget_orphan` | Local network administrator plus daemon mutation gate |

The package ships an exact `wmcs` rpcd ACL containing only the methods in this
table and no file, shell, UCI, or foreign-ubus grant. Freenetic or another local
client must explicitly request its read or write scope. The daemon still
validates role, peer, FSM state, field schema, and mutation gate after local
transport authorization.

## Read methods

### `status {}`

Returns:

- daemon/API version, configured role, and uptime;
- `state`, `observe_and_pair`, `transactional`, or `degraded`;
- `mutation_enabled` and the effective `mutation_available` gate;
- `neighbor_sync_enabled`, the separate opt-in for paired Neighbor Report
  exchange and runtime hostapd 802.11k/v recovery;
- `ubus_connected`, plus `degraded` and a bounded `degraded_reason` when the
  daemon is waiting for ubus or startup recovery;
- discovery, pairing, and control activity plus pairing/control state and
  operation names;
- `control_reconciliation_pending`, which tells a client to rediscover and
  resume or explicitly forget the affected peer;
- whether persistent identity exists, peer count, active-controller count, and
  durable generation.

It never returns a key, SAS outside a pending pairing state, WLAN profile, raw
packet, or protected path.

When `degraded` is true, the observe methods remain available but discovery,
pairing, control, release, stop, and forget methods are read-only blocked with
`UBUS_STATUS_PERMISSION_DENIED`. A normal ubus reconnect clears a transient
`ubus_disconnected` state. A startup recovery failure remains visible and
requires a controlled daemon restart after the underlying state is repaired;
the daemon never performs a hidden WLAN mutation from a reconnect callback.

### `capabilities {}`

For controller and agent roles, implemented feature booleans are true for
`read_only_inventory`, `native_protocol`, `discovery`,
`persistent_identity`, `pairing`, `authenticated_control`, `transactions`,
`one_wlan_sync`, `ownership_safe_release`, and `local_forget`. Controllers also
report `durable_controller_operation`. `degraded_status` and `ubus_reconnect`
describe the lifecycle boundary. `roaming` and `wireless_backhaul` remain
false. Platform booleans report the availability of ubus, UCI, hostapd, and iw.

These are daemon capabilities, not a claim that every radio profile or device
pair is supported.

### `roaming_status {}`

Returns aggregate local source-gate state only: `enabled`, `active`, `state`,
`mode`, the configured `source_trigger_dbm`, `improvement_margin_db`,
`force_after_timeout`, and `force_trigger_dbm`, client
and Neighbor Report counts, `neighbor_list_truncated`, sample/gate/request and
802.11k measurement counters, `fallback_btm_sent`, `force_disconnects`,
`force_failures`, fresh last source/target signal observations,
`last_target_margin_db`, and `last_target_age_ms`, plus a bounded `last_reason`.
Station addresses, candidate BSSIDs, credentials,
and raw hostapd payloads are never returned. The experimental
`source_gate_advisory` policy requires five consecutive weak-source samples
and minimum dwell before sending one 802.11v suggestion. Measurements are
requested serially and bounded when supported; because the hostapd request
method does not return its measurement dialog token, a report is matched to
the sole outstanding request by station, BSSID, operating class, and channel.
A fresh report must show the configured signal-improvement margin. If no
usable report arrives, one same-SSID, locally published Neighbor Report
candidate may be suggested without claiming a measured target signal.
Multiple unmeasured candidates, an explicitly measured weak target, a changed
neighbor, or an oversized neighbor list fail closed. Local Neighbor Reports
are not authenticated evidence of compatible security or reachable target APs.
The station normally makes the final candidate choice. An accepted BTM
response is not counted as a completed roam.
When hostapd notifications are available, `btm_response_monitor_active`,
`btm_responses`, `btm_accepted`, `btm_rejected`, `btm_response_timeouts`, and
`last_btm_status_code` report responses matched by station address and dialog
token. `beacon_measurement_pending`, `beacon_requests_sent`,
`beacon_request_failures`, `beacon_reports_received`, and
`beacon_report_timeouts` expose the bounded measurement path without exposing
the raw per-station report.
`force_after_timeout` is disabled by default. If enabled, an unanswered BTM
may be followed by a 10-second wait and one hostapd disassociation only while
the client remains authorized on the same source, the sole target is unchanged,
and the source is at or below the separately configured, stricter signal
threshold. Explicit BTM responses cancel it. A five-second source ban and
five-minute per-station cooldown limit repeated actions; this can interrupt
traffic and does not prove a successful roam. It is not a deauthentication.

The opt-in `neighbor_sync_enabled`, `neighbor_sync_ready`,
`neighbor_sync_state`, `authenticated_neighbor_count`, `neighbor_queries_sent`,
`neighbor_replies_accepted`, and `neighbor_apply_failures` expose the separate
authenticated AP-neighbor reconciliation. No peer BSSID or SSID is returned.
When enabled, steering is gated on its ready state and an exact fresh-list
match. See [neighbor sync](NEIGHBOR_SYNC_V0.md) for ownership and rollback
semantics. This option does not enable `force_after_timeout`.

### `nodes {}`

Returns at most 16 retained discovery observations. Each contains an ephemeral
candidate ID, observed address, age, role, transport, state, reason, and
`identity_trust: "ephemeral_untrusted"`. Neither the ID nor address authorizes
pairing or mutation.

### `identity {}`

Returns `ready`, `algorithm`, `paired_peer_count`, `active_controller_count`,
generation, and—only after identity creation—the public-key fingerprint. A
released reconciliation tombstone remains paired but not active. The method
never returns public/private key data or a stored peer record.

### `peers {}`

Returns a stable peer-ID-sorted array of at most 16 protected-store summaries.
Each item contains only `peer_id`, remote `role` (`controller` or `agent`),
`state` (`active` or `released`), and the per-peer generation. It exposes no
public key, relationship key, address, WLAN state, or protected path. This is
the authoritative input for presenting guarded `forget_orphan` actions after a
restart.

### `pairing_status {}`

Returns `active` and `state`: `idle`, `exchanging`, or
`awaiting_confirmation`. A pending exchange additionally returns the
display-only `sas` and `pending_peer_id`. The local public fingerprint is
included when identity exists.

### `wlan_sync_status {}`

Returns `active`, `operation` (`none`, `wlan_sync`, or `release`), and `state`:
`idle`, `listening`, `waiting_result`,
`processing`, `complete`, `recovery_pending`, `failed`, or `timed_out`. When
available, it includes the public peer ID, sequence, and `dry_run` mode.
`reconciliation_pending` is true for a durable request awaiting explicit resume
and for a non-consuming replay rejection. A complete exchange includes one
bounded `outcome` and `reason`; it never includes the WLAN profile or key.

`release_status {}` is an alias over the same single-operation control state.

## Discovery methods

### `discovery_start { "duration_seconds": 60 }`

Starts one bounded window for a controller or agent. Duration is optional and
must be 5..300 seconds. Returns active state, selected duration, and bound
interface. Candidates are read separately through `nodes`.

### `discovery_stop {}`

Stops the local discovery window. It does not mutate paired membership or
native network configuration.

## Pairing methods

### `pairing_start`

Agent request:

```json
{ "duration_seconds": 120 }
```

Controller request:

```json
{ "duration_seconds": 120, "candidate_id": "32-lowercase-hex" }
```

Duration defaults to 120 and must be 5..300 seconds. The controller resolves
the retained candidate address internally. Starting pairing may lazily create
the protected local identity.

### `pairing_confirm { "sas": "xxxx-xxxx-xxxx" }`

Requires the exact SAS displayed independently on both devices. A mismatch is
permission denied and leaves the exchange pending. A match atomically stores
the peer relationship, advances generation, and returns the public `peer_id`.
Both devices are confirmed separately in v0.
An agent refuses a second active controller relationship; a released
non-authorizing tombstone does not block a different newly SAS-confirmed
controller. Pairing does not overwrite an existing peer fingerprint. Re-pairing
the same identity requires successful local `forget_orphan` first, preventing
implicit relationship-key rotation and stale-result ambiguity.

### `pairing_stop {}`

Closes the pairing window and erases unused ephemeral secret state. It does not
delete an already paired relationship.

## Authenticated control methods

### `control_listen { "duration_seconds": 30 }`

Agent-only. Opens the encrypted control receiver for 5..300 seconds; duration
defaults to 30. The agent still accepts only the exact next sequence from a
stored controller peer. Opening a window does not itself authorize mutation.

### `wlan_sync_start`

Controller-only request:

```json
{
  "candidate_id": "32-lowercase-hex",
  "peer_id": "32-lowercase-hex",
  "dry_run": true,
  "duration_seconds": 30
}
```

`candidate_id` and `peer_id` are required. `dry_run` defaults to `true`, and
duration defaults to 30 with a 5..300 second range. The candidate supplies only
the current destination address; the encrypted recipient identity and paired
relationship authenticate the agent.

The daemon reads the configured source AP internally. With `dry_run:true`, the
agent validates the peer, sequence, 5 GHz target and ownership without UCI
mutation. With `dry_run:false`, both controller and agent must have local
mutation enabled; the agent then performs protected backup, owned apply,
runtime verification, commit or rollback.

The start reply means only that the controller is waiting for a result. Poll
`wlan_sync_status` for `complete` and inspect its outcome.

Before first transmission, the controller stores the exact encrypted request.
If it times out or restarts before receiving a result, rediscover the same agent
and repeat `wlan_sync_start` with the same peer, operation, and `dry_run` value.
WMCS sends the preserved request to the freshly observed address; it does not
reread a changed source profile at the unresolved sequence. The agent executes
it once or returns its protected durable result. The controller stores that
authenticated result before advancing its local peer state, so `complete` also
survives restart. Changed source state is considered only by the following
sequence.

### `wlan_sync_stop {}`

Stops network retries. A pending durable request becomes `recovery_pending`
because stop cannot revoke a UDP request that may already have reached the
agent. For an ordinary completed operation, stop acknowledges and removes the
latest controller record. It cannot cancel or undo remote work.

### `release_start`

Controller-only. The request schema is identical to `wlan_sync_start`:

```json
{
  "candidate_id": "32-lowercase-hex",
  "peer_id": "32-lowercase-hex",
  "dry_run": true,
  "duration_seconds": 30
}
```

No section name or selector is accepted. Dry-run proves that the fixed managed
WLAN is absent or has the matching owner and target radio. With
`dry_run:false`, both daemons require mutation enabled. The agent backs up the
wireless file, deletes only the exactly owned section, reloads wireless, and
verifies both UCI and runtime absence. An absent section commits idempotently;
a conflicting marker is preserved.

Poll `release_status` for the operation and bounded outcome. A committed
release changes the peer to a durable non-authorizing `released` state on each
side. If the controller times out, rediscover and repeat `release_start` with
the same peer and `dry_run` value; its durable exact request emits the same
sequence and the agent returns the durable release result without repeating
deletion.

### `release_stop {}`

Alias of the local control stop operation. Pending release reconciliation is
retained; an ordinary completed record is acknowledged. It cannot undo a remote
transaction.

### `forget_orphan { "peer_id": "32-lowercase-hex" }`

Local-only trust cleanup. It requires mutation enabled and no active discovery,
pairing, or control operation. The method refuses while the peer owns the fixed
managed WLAN or appears in `wireless.pending`. It removes a matching durable
agent result or controller operation plus the peer record through a durable
`forget.pending` intent, returns `forgotten:true`, and never edits UCI. If the
daemon stops during cleanup, startup retries the named intent before exposing
ubus. Run it independently on both devices only after release reconciliation,
or for a confirmed half-pair that owns no state.

## Errors

Version 0 maps validation and state failures onto standard ubus statuses:

- invalid argument for unknown fields, invalid schema, conflicting activity,
  duplicate start, duration errors, or a peer that still owns/pends state;
- not supported for the wrong role;
- not found for missing candidate, peer, or identity;
- permission denied for a wrong SAS, rejected peer role, or disabled mutation;
- unknown error for internal platform/crypto/storage failure.

Stable transaction reason strings are returned by the control status methods
after an authenticated result. Error replies never contain credentials, raw
peer payloads, command output, private state, or local file paths.

## Planned methods

- `topology` and bounded health observations;
- multi-entry controller-side `job_status` and bounded result history;
- acknowledged two-sided key erasure and controller replacement policy;
- diagnostics with explicit redacted scopes.

Before v1, method/object versioning, final ACL naming, pagination, stable error codes,
confirmation policy, and Freenetic compatibility policy must be frozen.
