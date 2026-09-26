# WMCS Master Plan

Status: working draft
Project name: **WMCS — Wi-Fi Mesh Coordination System**
Initial platform: OpenWrt 24.10 and 25.12
Initial interoperability target: native WMCS between OpenWrt devices

## 1. Mission

WMCS is an open-source coordination layer for managed Wi-Fi systems. It owns
discovery, secure adoption, node membership, configuration orchestration,
topology and health, while leaving packet forwarding and radio operation to
native platform services.

The first concrete objective is deliberately narrow:

> An OpenWrt router running WMCS discovers, securely adopts, configures and
> monitors one OpenWrt agent over Ethernet backhaul, survives reboot on both
> sides, and can release the agent without damaging unrelated
> configuration.

This objective establishes the native WMCS protocol and safe OpenWrt platform
boundary. Keenetic MWS interoperability remains a separate later adapter and
must not be claimed until tested against identified real Keenetic hardware.

## 2. Scope

### 2.1 Initial MVP

- OpenWrt/WMCS acts as controller.
- One OpenWrt/WMCS device acts as agent/member.
- Ethernet backhaul only.
- Explicit, time-limited adoption initiated by the administrator.
- Cryptographic node identity and an authenticated control session.
- Synchronization of one home WLAN: SSID, security mode and key.
- Read-only node status, capabilities and topology.
- Transactional configuration with verification and rollback.
- Persistence across controller and agent reboot.
- Explicit release/forget operation.
- No changes to unrelated or unowned OpenWrt configuration.

### 2.2 Later scope

- Multiple agents and topology changes.
- Guest and additional network segments with VLAN propagation.
- Wireless backhaul and parent selection.
- Multi-hop topology.
- 802.11k/v-assisted roaming, followed separately by 802.11r.
- Channel coordination, load awareness and steering policy.
- Keenetic MWS controller/member interoperability through a separate adapter.
- Optional EasyMesh backend or integration.
- Coordinated firmware/component updates only after the core protocol is safe.

### 2.3 Non-goals

- WMCS is not a replacement network stack.
- WMCS is not in the data path.
- WMCS does not require a cloud service for local operation.
- WMCS does not silently normalize or delete foreign UCI configuration.
- WMCS does not promise interoperability based only on matching SSIDs or
  implementing a generic 802.11s mesh.
- The first alpha does not target every OpenWrt radio driver, target family, or
  third-party mesh protocol.
- Proprietary firmware, libraries, decompiled code or copied vendor resources
  must not enter this repository.

## 3. Architectural principles

1. **Native source of truth.** Interfaces, radios, stations, bridges, VLANs,
   DHCP and firewall state come from UCI, ubus, netifd, hostapd,
   wpa_supplicant, nl80211 and the kernel.
2. **Separate control state.** WMCS stores only protocol-specific state such as
   identity keys, trust anchors, membership, protocol version and transaction
   generations.
3. **Not in the data path.** An established network continues forwarding when
   `wmcsd`, LuCI or the controller UI is unavailable.
4. **Capability-driven behavior.** The controller never assumes identical
   radios, bands, drivers or firmware versions.
5. **Explicit ownership.** Every OpenWrt object created by WMCS carries an
   ownership marker such as `wmcs_managed=1`.
6. **Transactional mutation.** Remote and local changes follow
   `prepare -> validate -> apply -> verify -> commit`, with rollback on failure.
7. **Secure by construction.** Pairing, trust, replay protection, secret
   handling and ACLs are part of the first protocol implementation.
8. **Versioned boundaries.** The daemon API, stored state and protocol adapters
   have explicit versions and migration rules.
9. **Observable decisions.** Every state transition and reconciliation action
   has a reason code suitable for logs and UI, without logging secrets.
10. **Interoperability is empirical.** Compatibility claims require a recorded
    test against identified real hardware and firmware.

## 4. High-level architecture

```text
Freenetic UI / generic LuCI / wmcsctl
                  |
             versioned ubus API
                  |
                wmcsd
     +------------+-------------+
     | controller/agent FSM     |
     | discovery and adoption   |
     | trust and sessions       |
     | desired/observed model   |
     | transaction coordinator  |
     | topology and health      |
     +------+-------------+-----+
            |             |
     protocol adapter   platform adapter
     native / future     OpenWrt UCI, ubus,
     MWS / EasyMesh      netifd, hostapd,
                        wpa_supplicant, nl80211
            |             |
            +------+------+
                   |
       wired/wireless backhaul and APs
```

### 4.1 Core components

- **`wmcsd`** — long-running daemon and owner of coordination state machines.
- **Protocol core** — validated wire types, messages, versions and error codes.
- **Native WMCS adapter** — the first versioned controller-agent transport and
  message contract.
- **Future MWS adapter** — isolated compatibility logic based on separately
  governed public and black-box research.
- **OpenWrt adapter** — reads native state and applies narrowly scoped changes.
- **Reconciler** — compares desired and observed state without overwriting
  unknown fields.
- **Transaction manager** — stages, verifies, commits and rolls back changes.
- **State store** — small atomically written state; no database in the MVP.
- **ubus service** — stable local API for UI, CLI and tests.
- **`wmcsctl`** — diagnostics and administrator CLI; it uses the same ubus API.
- **Simulator** — fake controller/member used for deterministic CI.
- **Capture/decoder tools** — research tooling kept separate from runtime code.

### 4.2 Proposed node lifecycle

```text
unknown -> discovered -> candidate -> pairing -> adopting -> configuring
                                                         |
                                                         v
released <- releasing <- offline <- degraded <- online <- verifying
                              \                    /
                               +------ failed -----+
```

Every transition must define its trigger, timeout, retry policy, persistent
effects and rollback behavior.

## 5. Repository layout

The precise language layout will be selected after the toolchain spike, but
the logical ownership should be:

```text
wmcs/
|-- README.md
|-- PLAN.md
|-- ARCHITECTURE.md
|-- SECURITY.md
|-- CONTRIBUTING.md
|-- docs/
|   |-- protocol/
|   |-- experiments/
|   |-- compatibility/
|   `-- decisions/
|-- src/ or crates/
|   |-- core/
|   |-- discovery/
|   |-- transport/
|   |-- controller/
|   |-- agent/
|   |-- reconcile/
|   |-- protocol-mws/
|   |-- platform-openwrt/
|   `-- ubus-api/
|-- tools/
|   |-- capture/
|   |-- decode/
|   `-- replay/
|-- simulator/
|-- tests/
|   |-- fixtures/
|   |-- integration/
|   |-- namespaces/
|   `-- hardware/
`-- package/openwrt/
```

Freenetic-specific views should ultimately live in the Freenetic repository
and consume the public WMCS ubus contract. The WMCS repository may provide a
minimal generic LuCI application for development and independent use.

## 6. Clean-room research policy

The public implementation must be independently explainable from documented
external behavior and publicly available standards.

### 6.1 Acceptable inputs

- Traffic captured between devices owned and operated in the test lab.
- Documented inputs and outputs from normal user-visible interfaces.
- Before/after configuration and state observations.
- Published vendor documentation and public standards.
- Responses produced by controlled black-box experiments.
- Neutral protocol specifications written from observations.

### 6.2 Excluded inputs

- Decompiled or disassembled proprietary implementation code.
- Copied symbols, constants, command tables, schemas or strings from vendor
  binaries when they are not independently observable on the wire or through
  public behavior.
- Proprietary libraries, firmware images or extracted files committed to this
  repository.
- Captures containing real user credentials, private keys or unrelated user
  traffic.

### 6.3 Research records

Every experiment should record:

- experiment ID and date;
- topology diagram;
- device model, role and exact firmware version;
- initial configuration hash/snapshot;
- one controlled action;
- expected and observed behavior;
- capture filename and SHA-256;
- resulting configuration/state diff;
- interpretation, confidence and unanswered questions.

Raw sensitive captures live outside the Git repository. Only minimal,
sanitized fixtures required by tests may be committed.

If strict clean-room separation becomes necessary, a research role produces a
neutral protocol document and an implementation role works only from that
document and public standards.

## 7. Required lab

### 7.1 Hardware for the native MVP

- Two supported OpenWrt routers, initially one MediaTek Filogic/aarch64 and one
  MT7621/mipsel device.
- Managed Ethernet switch with reliable port mirroring.
- Linux capture workstation with sufficient storage.
- Wi-Fi adapter supporting monitor mode on the required bands.
- UART access for all development routers.
- Individually controllable power or smart plugs for reboot/failure tests.
- Spare recovery device and known-good firmware/configuration backups.

Keenetic controller/member hardware is required only when the separate MWS
adapter workstream begins.

### 7.2 Software tools

- `tcpdump`, `dumpcap`, Wireshark and `tshark`.
- A custom Wireshark dissector once framing is understood.
- `ubus monitor`, `logread`, `uci export`, `iw`, `iwinfo` and `ip monitor`.
- `hostapd_cli` and `wpa_cli` where available.
- `iperf3`, ping and controlled traffic generators.
- Linux network namespaces, veth pairs and bridges.
- `tc netem` for latency, loss, duplication and reordering.
- Automated serial-console and power-cycle control.
- A reproducible OpenWrt SDK/buildroot matrix.

### 7.3 Capture scenarios

Run each scenario first over Ethernet and later over wireless where relevant:

1. Idle controller with no candidates.
2. Candidate boot and discovery.
3. Pairing window start and expiry.
4. Successful acquire/adopt.
5. Rejected or incorrect pairing attempt.
6. Initial capability exchange.
7. Initial WLAN configuration.
8. Single-field SSID, key and security changes.
9. Controller reboot, member reboot and simultaneous reboot.
10. Link loss and restoration.
11. Release from controller and reset from member.
12. Addition of a second member.
13. Wired parent/topology change.
14. Version mismatch and unsupported capability.
15. Interrupted configuration transaction.
16. Controller replacement or factory reset.
17. Guest/segment synchronization.
18. Firmware update workflow, observation only at first.

Change exactly one variable per experiment whenever possible.

## 8. Platform and language decisions

### 8.1 Preferred implementation

Start with a short Rust toolchain and footprint spike. Rust is preferred for
untrusted protocol parsing, cryptographic state and asynchronous state
machines. Build target-specific, mostly self-contained musl artifacts.

The spike must prove:

- repeatable `aarch64` and `mipsel_24kc` cross-builds with OpenWrt toolchains;
- working ubus integration;
- acceptable stripped package size and startup time;
- no unsupported atomic or TLS assumptions on MT7621;
- reproducible CI builds.

If Rust cannot satisfy the MIPS/toolchain/resource requirements reliably, use
C with `libubox`, `libubus`, `libuci` and the platform TLS library. Do not
choose the language solely from a host-side prototype.

### 8.2 Provisional resource budgets

These are targets to validate, not release promises:

- compressed runtime package: at most 4 MiB;
- idle resident memory: at most 12 MiB on a 128 MiB router;
- negligible CPU while topology is stable;
- no high-frequency polling when event subscriptions are available;
- bounded log and persistent-state growth;
- bounded write frequency to flash.

### 8.3 Persistent state

For the MVP, use atomically replaced small files and/or a dedicated UCI config,
not SQLite. Separate:

- `/etc/config/wmcs` — administrator-owned settings;
- `/etc/wmcs/` — identity, trust and durable protocol state, mode `0700`;
- `/tmp/wmcs/` — runtime observations, sockets and transient jobs.

MAC addresses are attributes, not cryptographic identities.

## 9. Local API draft

The initial ubus object is `wmcs`. Read operations must be safe for polling;
mutations return a job ID and progress through explicit states.

Proposed calls:

- `wmcs status`
- `wmcs capabilities`
- `wmcs nodes`
- `wmcs topology`
- `wmcs discovery.start`
- `wmcs discovery.stop`
- `wmcs pairing.start`
- `wmcs pairing.stop`
- `wmcs adopt`
- `wmcs release`
- `wmcs reconcile`
- `wmcs job.status`
- `wmcs diagnostics`

Before implementation, define request/response schemas, authorization, stable
error codes, timeouts and which fields may contain sensitive data. Never expose
private keys or reusable pairing secrets through status calls.

## 10. Security work required before mutation

Create a threat model covering:

- unauthorized adoption and hostile candidates;
- controller impersonation;
- replay and downgrade attacks;
- stolen node state or cloned identity;
- malicious or compromised member responses;
- denial of service during discovery and pairing;
- injection through identifiers, SSIDs and protocol fields;
- partial apply, power loss and rollback failure;
- exposure of Wi-Fi keys through logs, ubus or captures;
- lateral access from guest or untrusted segments;
- unsafe release, reset and firmware update behavior.

Minimum security properties:

- pairing is explicit, local and time limited;
- node identity is key-based rather than MAC-based;
- control messages are authenticated and confidential where required;
- sessions have replay protection and bounded lifetimes;
- protocol versions and algorithms cannot silently downgrade;
- all input has length, type and state validation;
- local mutation crosses a narrow ACL and fixed operation boundary;
- secrets are redacted from logs and diagnostics by construction;
- release and uninstall preserve unrelated working network configuration;
- configuration has a verified rollback path before the first remote apply.

Longer term, split network-facing parsing from privileged configuration apply.
If the MVP runs as root, keep the exposed surface minimal and document the
temporary privilege boundary explicitly.

## 11. Execution phases and gates

Durations are rough single-developer estimates. Protocol opacity and hardware
behavior dominate the schedule.

### Phase 0 — Project foundation (2-4 days)

Deliverables:

- README, architecture, security policy and contribution rules;
- license decision;
- repository layout and CI skeleton;
- experiment template and compatibility-matrix template;
- explicit MVP capability matrix.

Exit gate: another engineer can state exactly what the first alpha will and
will not do.

### Phase 1 — Lab and observatory (3-7 days)

Deliverables:

- reproducible physical topology;
- safe capture procedure;
- automated state snapshots before and after an action;
- first native discovery/adoption test corpus;
- sanitized metadata index for captures.

Exit gate: candidate appearance and disappearance can be reproduced and
correlated with observable network events.

### Phase 2 — Native protocol specification (1-2 weeks)

Deliverables:

- versioned transport, framing and message schemas;
- discovery and identity model;
- adoption state machine;
- capability and message field catalogue;
- retry, timeout and error behavior;
- first decoder/replay fixtures.

Exit gate: protocol schemas and state transitions are reviewable, simulator
fixtures are deterministic, and every message has explicit bounds and errors.

### Phase 3 — Core daemon and simulator (2-4 weeks)

Deliverables:

- `wmcsd` event loop and lifecycle;
- versioned ubus read API;
- state store and migrations;
- controller/agent simulator;
- transaction and rollback framework;
- structured logs and reason codes.

Exit gate: simulated adoption, reboot, timeout and rollback pass in CI without
real hardware.

### Phase 4 — Read-only two-router integration (1-2 weeks)

Deliverables:

- real OpenWrt-agent discovery;
- capability and identity observation;
- node/topology model exposed over ubus;
- minimal generic diagnostic UI or CLI.

Exit gate: WMCS observes the second OpenWrt router for a long-running test without
modifying it or leaking resources.

### Phase 5 — Wired adoption MVP (2-4 weeks)

Deliverables:

- explicit secure adoption;
- one WLAN synchronized over Ethernet backhaul;
- apply verification and rollback;
- reboot persistence;
- release/forget flow;
- Freenetic status and adoption UI using only ubus.

Exit gate: ten consecutive factory-clean adoption/reboot/release cycles on
real hardware, including forced failures, without manual recovery.

### Phase 6 — Roaming quality on wired APs (3-6 weeks)

Deliverables:

- 802.11k neighbor information;
- conservative 802.11v transition policy;
- separate opt-in 802.11r support;
- client capability and cooldown model;
- repeatable transition timing and packet-loss measurements;
- supported client/AP/security compatibility matrix.

Exit gate: the supported seamless classes meet
`docs/requirements/ROAMING.md` in both movement directions without address
change, kick storms or ping-pong.

### Phase 7 — Multi-node configuration and resilience (4-8 weeks)

Deliverables:

- two or more agents;
- segment/VLAN propagation;
- topology changes and offline/degraded handling;
- reconciliation after local drift;
- controller restart and recovery;
- 24-72 hour soak tests.

Exit gate: no split ownership, uncontrolled config churn or loss of unrelated
network state during fault injection.

### Phase 8 — Wireless backhaul (6-12+ weeks)

Deliverables:

- wireless onboarding/backhaul;
- parent selection and reconnection;
- multihop if supported by observed protocol;
- roaming regression tests over the wireless transport;
- capability-specific driver behavior.

Exit gate: repeated movement/link-loss tests preserve client addressing and
recover automatically across the supported hardware matrix.

### Phase 9 — Additional protocol adapters (4-12+ weeks each)

Possible workstreams:

- Keenetic controller with a WMCS/OpenWrt agent;
- WMCS controller with a Keenetic member if independently implementable;
- EasyMesh adapter or prplMesh integration;
- coordinated updates and advanced steering.

Each workstream requires its own compatibility claim and exit criteria.

## 12. Testing strategy

### 12.1 Per-change tests

- unit tests for parsing, validation and state transitions;
- golden tests for sanitized protocol fixtures;
- property tests for codecs and transaction invariants;
- fuzzing for every network-facing decoder;
- integration tests against the simulator;
- namespace tests for link, address and service changes;
- package content, install, upgrade and uninstall tests;
- static checks preventing secrets and proprietary artifacts from entering Git.

### 12.2 Build matrix

- OpenWrt 24.10 with IPK/opkg.
- OpenWrt 25.12 with APK/apk.
- MediaTek Filogic/aarch64.
- MT7621/mipsel, subject to the language spike.
- Debug/sanitizer host builds for protocol tests.

### 12.3 Hardware-in-the-loop

Automate:

- clean configuration restore;
- package deployment;
- UART log capture;
- controller/member power cycles;
- link disconnect and restoration;
- packet capture boundaries;
- assertions over UCI, ubus, bridges, radio state and connectivity;
- artifact collection on failure.

Never run destructive or connectivity-risking tests on a production router or
through the only management path.

## 13. Packaging and Freenetic integration

Proposed packages:

- `wmcsd` — runtime daemon and protocol adapter(s);
- `wmcs-tools` — optional `wmcsctl`, capture and diagnostic tools;
- `luci-app-wmcs` — minimal generic interface, optional;
- `luci-app-freenetic-wmcs` — Freenetic presentation layer, likely maintained
  in the Freenetic repository.

The Freenetic integration must:

- depend only on the versioned ubus contract;
- never parse daemon log text as an API;
- expose capabilities rather than assuming features;
- show pending/failed/rollback states honestly;
- require confirmation for adopt, release, reset and topology-changing actions;
- remain usable when WMCS is not installed by hiding or disabling the feature;
- avoid direct protocol logic in browser JavaScript.

Release artifacts must be built per target ABI where necessary, while LuCI
packages can remain architecture-independent.

## 14. Roles and effort

The project can start with one developer, but it contains three specialties:

- black-box protocol research and packet analysis;
- embedded/network daemon and OpenWrt integration;
- UI and hardware-in-the-loop automation.

For a solo implementation, automate the lab early and keep the MVP narrow.
For parallel work, maintain a clean interface between the neutral behavioral
specification, daemon core and UI.

The native wired one-agent alpha is still a substantial engineering milestone,
but it avoids blocking the foundation on vendor-protocol research. Keenetic
interoperability, wireless backhaul and a real firmware matrix can reasonably
grow into a multi-month engineering program depending on protocol opacity and
driver behavior.

## 15. Principal risks

| Risk | Consequence | Mitigation |
|---|---|---|
| Encrypted or opaque future adapter protocol | Passive captures do not reveal semantics | Isolated black-box research, observable endpoint behavior, one-variable diffs |
| Protocol changes across firmware | False compatibility claims | Versioned fixtures and explicit device/firmware matrix |
| Driver and hostapd differences | Works on one router only | Capability probing and two-family HIL from the beginning |
| Partial remote apply | Member or network becomes unreachable | Prepare/verify/commit, watchdog and rollback |
| Ambiguous ownership | User configuration is overwritten | `wmcs_managed=1`, field-level ownership and conservative cleanup |
| Secret leakage | Compromised WLAN or node identity | Redaction tests, protected storage and sanitized captures |
| Clean-room contamination | Public implementation becomes indefensible | Hard repository boundary and documented provenance |
| MIPS toolchain/footprint | Target matrix collapses late | Language and package spike before core implementation |
| Scope expansion | No testable interoperability milestone | Keep the wired one-agent MVP as the first release gate |
| Loss of remote management | Expensive manual recovery | UART, independent management path, backups and controlled power |

## 16. Definition of done for the first alpha

The first alpha is done only when:

- packages install, start, upgrade and uninstall cleanly;
- one declared OpenWrt controller target/version pair is supported;
- one declared OpenWrt agent target/version pair is supported;
- discovery and explicit adoption work from a clean state;
- the authenticated relationship survives reboot on both devices;
- one WLAN is synchronized over Ethernet backhaul;
- topology and health are available through the documented ubus API;
- failed apply rolls back without destroying management connectivity;
- release removes only WMCS-owned state;
- no reusable secrets appear in normal logs or status output;
- simulator, parser, package and HIL tests pass;
- ten clean lifecycle repetitions and a 24-hour soak complete successfully;
- limitations and the exact compatibility matrix are published.

## 17. Immediate next actions

1. [x] Approve the native OpenWrt controller-agent, Ethernet-only MVP.
2. [x] Select Apache-2.0 and create `README.md`, `ARCHITECTURE.md`,
   `SECURITY.md` and `CONTRIBUTING.md` from this plan.
3. [ ] Finish the lab inventory. The Globitel BT-RB300 controller and Cudy
   TR3000 v1 agent and their firmware are recorded; an FTDI UART is attached
   to the controller but recovery is unverified, the Cudy has no UART, and
   controlled power interruption is unavailable. Verify switch mirroring and
   capture paths before treating the hardware fault lab as ready.
4. [x] Create the experiment template and compatibility matrix.
5. [x] Establish storage outside Git for sensitive captures and backups.
6. [x] Build an automated snapshot tool for UCI, ubus, link, neighbor, bridge,
   hostapd and process state.
7. [ ] Complete the idle-to-adoption and recovery corpus. Discovery,
   successful/wrong-SAS pairing, one-WLAN dry-run/apply/rollback and reboot
   are recorded in EXP-0003..0005; exact-package sync, release/forget/re-pair,
   ten normal lifecycle cycles, and process-restart reconciliation are in
   EXP-0019..0022. Pairing expiry and exact physical power, flash-full, and
   mid-UCI interruption boundaries remain untested.
8. [x] Run an aarch64/mipsel ubus/footprint spike and record the provisional
   C-versus-Rust decision gate.
9. [x] Draft ubus API v0 and the transport-independent simulator state machine.
10. [x] Implement bounded read-only candidate discovery and validate it in both
    start orders on the first two-router pair without configuration mutation.
11. [x] Define measurable roaming quality and safety requirements.
12. [x] Build IPK and APK packages and complete an inert read-only live smoke on
    the Globitel BT-RB300.
13. [x] Implement persistent P-256 identities and SAS-authenticated pairing;
    validate matching/wrong SAS, protected state, process restart recovery and
    unchanged UCI hashes on the Globitel/Cudy pair.
14. [x] Freeze the native v0 authenticated control envelope and implement the
    transactional one-WLAN validate/apply/verify/commit-or-rollback path;
    validate it on the Globitel/Cudy pair in ADR 0006 and EXP-0005.
15. [ ] Complete the lifecycle gate. Durable result/operation reconciliation
    and ownership-safe release/local forget are implemented; ten normal
    release/forget/re-pair/repair cycles passed (EXP-0021), and process
    restart/reconciliation passed on the r3 controller/r4 agent pair (EXP-0022).
    The r6 controller service restart ended healthy with two transient
    parallel SSH resets (EXP-0024); the r6 agent restart was skipped. A
    read-only 24-hour monitor started
    2026-09-26T09:37:58Z with a healthy first sample, but has no completed
    result. Physical power loss, router flash faults, and mid-UCI interruption
    remain untested.
16. [ ] Complete the wired-AP roaming gate. Baseline walks and accepted
    advisory BTM behavior are recorded, and daemon-side 802.11k target scoring
    plus event handling are now in source and cross-build successfully. In
    EXP-0026 the Poco stayed associated to Cudy while physically by Globitel:
    the automatic r6 policy timed out awaiting Beacon Reports and sent no BTM.
    One manual advisory BTM then moved it to Globitel with two lost 100 ms
    probes. The r7 policy adds a sole-neighbor advisory fallback and an
    opt-in, disabled-by-default forced-disassociation path. In EXP-0029, one
    automatic measured BTM in each direction moved the POCO to the intended
    AP; the 180-second and 120-second probes lost 4/1789 and 5/1193 replies,
    respectively. Cudy hostapd accepted the reverse request, but r7 WMCS
    discarded its INT8 response attributes and counted a timeout. The r8
    parser fix is host-tested, cross-built and deployed on both routers, but
    has not yet been live-response tested. Both advisory policies remain off.
    The opt-in paired Neighbor Report reconciler was exercised on both nodes
    in EXP-0030: encrypted reciprocal reports, runtime-list restoration,
    protected ownership state, daemon restart, peer expiry, and rejoin passed
    without changing wireless UCI. The r10 raw-list pre-BTM guard and exact
    package rollout are recorded in EXP-0031. A targeted BT 5 GHz radio
    down/up recreated its hostapd AP object and restored reciprocal reports
    without touching 2.4 GHz backhaul (EXP-0032). Cudy AP restart, global
    hostapd process restart/reboot recovery, live Cudy INT8 response handling,
    fallback safety and repeated quality trials remain. Evaluate 802.11r only
    as a separate opt-in step.

The first irreversible design commitment should be the local API contract, not
an assumed wire format. Protocol knowledge will evolve; Freenetic and other
clients should not have to evolve with every research discovery.
