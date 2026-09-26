# WMCS Architecture

Status: implemented one-WLAN and release foundation; broader lifecycle remains
a draft

## System boundary

WMCS is a coordination service, not a replacement network stack. It observes
native OpenWrt state and applies narrowly scoped, reversible changes through a
platform adapter.

```text
Freenetic UI / generic LuCI / wmcsctl
                 |
            versioned ubus API
                 |
               wmcsd
       +---------+----------+
       | protocol and FSM   |
       | trust and sessions |
       | transaction engine |
       | topology and health|
       +----+-----------+---+
            |           |
     protocol adapter   OpenWrt adapter
     native / future    UCI, ubus, netifd,
     MWS                hostapd, nl80211
```

The daemon may stop without interrupting established packet forwarding. Native
services remain responsible for bridges, VLANs, DHCP, firewalling, AP operation,
and station forwarding.

## Components

- **Protocol core:** bounded wire types, state transitions, versions, errors,
  replay rules, and capability negotiation.
- **Controller and agent FSMs:** lifecycle orchestration without platform- or
  vendor-specific parsing.
- **Protocol adapters:** translate a specific protocol into neutral core events.
- **OpenWrt adapter:** reads native state and performs approved operations with
  fixed argument and ownership boundaries.
- **Reconciler:** compares desired and observed state at field level. Absence
  from desired state is not permission to delete a foreign object.
- **Transaction manager:** stages, validates, applies, verifies, commits, or
  restores every mutation.
- **State store:** atomically written protocol state and schema migrations.
- **ubus service:** the only supported local integration API.
- **Simulator:** deterministic peers, failures, and clocks for CI.

Network-facing parsing and privileged application should ultimately run across
separate privilege boundaries. If an early MVP runs as root, the parser surface
must remain minimal and this temporary exception must be recorded.

## State ownership

| State | Source of truth | WMCS access |
|---|---|---|
| Radios, BSSes and stations | hostapd, nl80211, ubus | Observe; request bounded native operations |
| Interfaces and bridges | UCI, netifd, kernel | Observe; mutate only explicitly owned fields |
| VLAN, DHCP and firewall | UCI and native services | No implicit normalization or deletion |
| Node identity and trust | WMCS protected state | Own |
| Membership and protocol version | WMCS protected state | Own |
| Jobs and transaction generations | WMCS state and journal | Own |
| Topology and health | Derived observation | Cache with bounded lifetime |
| WLAN credentials | Native configuration | Read only when required; never return via status APIs |
| Paired Neighbor Reports | Current hostapd BSS plus authenticated active peer reports | Opt-in runtime reconciliation; exact last-applied list journaled privately; foreign lists preserved |

Generated OpenWrt sections must carry both `wmcs_managed=1` and a stable scope
identifier. Automatic cleanup may remove only objects carrying the expected
ownership and scope markers. Field-level ownership must be defined before an
adapter can modify a pre-existing section.

The experimental neighbor adapter never edits a pre-existing UCI wireless
section. On an explicitly selected controller BSS it changes only hostapd
runtime k/v flags and an ownership-checked Neighbor Report list. On an agent
it additionally requires the exact managed BSS and cryptographic owner. The
protected last-applied list lets restart recovery distinguish WMCS output
from foreign runtime entries; a conflict pauses steering instead of guessing.
See [neighbor sync](docs/protocol/NEIGHBOR_SYNC_V0.md).

## Desired and observed state

The desired model contains only fields WMCS is authorized to coordinate. The
observed model is rebuilt from native services and peer reports. Reconciliation
must classify every difference as one of:

- owned and safely reconcilable;
- foreign and preserved;
- conflicting and requiring administrator action;
- unsupported by current capabilities;
- temporarily unavailable.

No reconciliation loop may infer ownership from a section name, SSID, MAC
address, or the fact that an object is absent from the desired model.

## Transaction protocol

Every mutating operation follows this lifecycle:

```text
inspect -> plan -> prepare -> validate -> confirm -> apply -> verify
                                                      |         |
                                                      v         v
                                                   rollback   commit
```

Before `apply`, the journal contains:

- operation and generation identifiers;
- the exact owned fields to change;
- preconditions and capability results;
- snapshots or compensating operations;
- service states and package preconditions;
- verification and rollback deadlines.

Commit is allowed only after connectivity and state verification. Recovery on
startup resumes verification or rollback from the durable journal. A successful
ping alone is not proof that the whole operation succeeded.

The v0 one-WLAN and release pilots implement this boundary as one authenticated
request whose agent-side path is `validate -> backup -> apply/delete -> verify
-> commit`, with rollback on failure and startup recovery from the pending
backup. The latest operation-typed encrypted result is persisted before
generation commit so a repeated sequence can reconcile across daemon restart.
The controller separately persists its exact encrypted request before sending,
then stores the authenticated response before advancing local peer state. It
does not automatically resume mutation after boot; a fresh discovery
observation and matching explicit start call are required. A committed release
atomically changes peer authority to `released`; its key is retained only as a
non-authorizing result-reconciliation tombstone until guarded local forgetting.
Forget is a separate generation-bound journal: `forget.pending` is durable
before matching operation/result records or the peer file are removed, and is
removed only after cleanup. Startup completes that intent before ordinary
transaction recovery. It never edits UCI and never broadens cleanup beyond the
recorded peer.
Multi-entry controller history and bounded per-peer result history remain later
work.

## Node lifecycle

```text
unknown -> discovered -> candidate -> pairing -> adopting -> configuring
                                                         |
                                                         v
released <- releasing <- offline <- degraded <- online <- verifying
                              \                    /
                               +------ failed -----+
```

Each transition defines its trigger, authorization, timeout, retry policy,
persistent effects, and compensation. `failed`, `offline`, `orphaned`, and
`rollback_failed` are distinct states and must not be reported as success.

## API boundary

UI and CLI clients consume a versioned ubus contract. They never parse daemon
logs, access persistent state files, construct remote commands, or contain wire
protocol logic. The experimental v0 returns bounded control state and reason
codes directly. One durable latest-operation slot survives restart; durable job
identifiers and multi-entry progress history are a later API.

The current local API proposal is in
[docs/protocol/UBUS_API_V0.md](docs/protocol/UBUS_API_V0.md). Native
controller/agent semantics are specified separately in
[docs/protocol/NATIVE_V0.md](docs/protocol/NATIVE_V0.md).

## Packaging boundary

Runtime packages are target-ABI-specific where required. Freenetic UI packages
remain architecture independent and depend only on a compatible ubus contract.
WMCS does not implement a private package feed or self-updater; installation and
updates use the platform package manager and the signed release pipeline.
