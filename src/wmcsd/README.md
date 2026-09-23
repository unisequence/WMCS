# wmcsd discovery, pairing, one-WLAN, and release pilot

This is the first target-side WMCS process. Its local API exposes:

- `ubus call wmcs status`;
- `ubus call wmcs capabilities`;
- `ubus call wmcs nodes`;
- `ubus call wmcs identity`;
- `ubus call wmcs peers`;
- `ubus call wmcs pairing_status`;
- `ubus call wmcs wlan_sync_status`;
- `ubus call wmcs release_status`.

Controller and agent roles additionally expose bounded discovery:

- `ubus call wmcs discovery_start '{"duration_seconds":30}'`;
- `ubus call wmcs discovery_stop`.

They also expose explicit 5..300 second authenticated pairing windows:

- agent: `ubus call wmcs pairing_start '{"duration_seconds":30}'`;
- controller: `ubus call wmcs pairing_start '{"duration_seconds":30,"candidate_id":"..."}'`;
- both: `ubus call wmcs pairing_confirm '{"sas":"xxxx-xxxx-xxxx"}'`;
- both: `ubus call wmcs pairing_stop`.

The controller accepts only a candidate ID retained by bounded discovery. Both
sides must display and confirm the same SAS. Pairing lazily creates a persistent
P-256 identity and stores the peer relationship under the protected state
directory (default `/etc/wmcs`). A wrong SAS, stop, or timeout leaves no peer
record and erases the pending ephemeral exchange.

Paired controller and agent roles expose bounded encrypted control:

- agent: `ubus call wmcs control_listen '{"duration_seconds":30}'`;
- controller dry-run: `ubus call wmcs wlan_sync_start '{"candidate_id":"...","peer_id":"...","dry_run":true}'`;
- controller apply uses the same method with `dry_run:false`;
- controller release dry-run: `ubus call wmcs release_start '{"candidate_id":"...","peer_id":"...","dry_run":true}'`;
- controller live release uses the same method with `dry_run:false`;
- both: `ubus call wmcs wlan_sync_stop`.

The controller reads its configured source AP internally. The WLAN secret is
encrypted before leaving the process and is never returned by ubus. The agent
creates only `wireless.wmcs_home_5g`, marked with its cryptographic owner, and
uses a protected backup plus runtime hostapd verification and rollback. It does
not overwrite the disabled vendor/default AP section or radio/channel options.
Before consuming the peer sequence, the agent atomically stores the exact
encrypted result in protected state. A repeated authenticated sequence after
either daemon restarts returns that result without repeating the mutation.
Before its first transmission, the controller similarly stores the exact
encrypted request. It stores the authenticated response before advancing local
peer state. After a timeout or restart, rediscovery plus the same start call
resumes that exact request; startup alone never transmits it. Completed status
also survives restart.

Release removes the fixed section only when its managed marker, cryptographic
owner, section type, and target radio all match. It uses the same backup,
runtime verification, durable result, and rollback boundary. Commit marks the
relationship non-authorizing while preserving enough key state to reconcile a
lost result. After reconciliation, each node can run
`ubus call wmcs forget_orphan '{"peer_id":"..."}'`; this refuses while owned or
pending WLAN state remains and never edits UCI. Cleanup is protected by a
generation-bound `forget.pending` intent and resumes before ubus registration if
the daemon stops between deleting its records.

On a controller, `wlan_sync_stop` and `release_stop` suspend retries but retain
an unresolved request as `recovery_pending`. For an ordinary completed
operation they acknowledge the latest durable status. This distinction avoids
claiming that a sent UDP mutation was cancelled.

Mutation requires both the package config gate and an authenticated paired
request. UDP sockets exist only during explicit bounded discovery, pairing, or
control windows and are bound to the configured LAN interface. The OpenWrt
service and mutation gate are disabled by default.

Cross-build both supported ABI families with:

```sh
make check-wmcsd-cross
```

Build and validate the installable OpenWrt package with:

```sh
make check-openwrt-package OPENWRT_DIR=/path/to/openwrt
```

The first live-router check is `tools/lab/read-only-smoke.sh`. It launches the
daemon directly, verifies the read API, and proves that relevant UCI exports
have the same hash before and after the run.

This C layer remains useful as an OpenWrt adapter even if the protocol/state
core later moves to Rust.
