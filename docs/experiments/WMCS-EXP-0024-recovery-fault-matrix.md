# WMCS-EXP-0024 — recoverable non-roaming fault matrix on the r6 pair

Status: partial. Host fault checks passed; one controller service restart has
final healthy-state evidence with an SSH sampling interruption. The agent
restart was skipped because its independent recovery path is unavailable, and
physical fault boundaries remain untested.

## Question and scope

Which storage and journal failure boundaries have direct host evidence, and
what process-restart behavior can be observed on the actual r6 Globitel
BT-RB300 controller and Cudy TR3000 agent without risking the working WLAN?

Run: 2026-09-26, 12:32 MSK (09:32 UTC), on repository `main@63495f6` with
the 19 pre-existing modified files preserved. No new pairing, control
transaction, WLAN/UCI edit, power interruption, or storage exhaustion was
performed. Earlier r4 normal lifecycle and process-restart evidence is in
EXP-0017 and EXP-0019..0022; it is not counted as a new r6 fault injection.

## Topology and recovery boundary

- Controller: Globitel BT-RB300, `192.168.1.1`, `wmcsd 0.2.0-r6`.
- Agent: Cudy TR3000, `192.168.1.2`, `wmcsd 0.2.0-r6`.
- Host route to both routers: `enp10s0`, source `192.168.1.182`.
  Internet default route remained via `192.168.64.1` on `enp9s0`.
- The user confirmed FTDI UART `/dev/ttyUSB0` attached to the controller.
  It was not exercised in this run. The Cudy has no UART and controlled power
  interruption is unavailable. Thus there is no verified recovery path for
  a deliberate agent flash or power fault.
- The default SSH configuration for the controller uses
  `UserKnownHostsFile=/dev/null`, so a strict-checking call in this run failed
  with no known ED25519 key for that invocation. Controller calls in this run
  used `StrictHostKeyChecking=no` on the isolated lab route; the Cudy used
  strict host-key checking. A separate run-directory `soak_known_hosts` has
  since pinned a controller key for read-only monitoring; it was not used for
  this experiment. The limitation applies to this run's controller SSH calls,
  not to every known-hosts file on the host.

## Commands and direct results

Host tests, run once from the repository root:

```sh
make check-atomic-file check-result-store check-operation-store check-controller-journal check-forget-journal
```

Exit 0. Output: `Atomic file tests: ok`, `Durable result store tests: ok`,
`Durable controller operation store tests: ok`, `Durable controller journal
contract: ok`, and `Durable forget journal contract: ok`. The atomic-file
test deliberately returned `-ENOSPC` at write, file fsync, rename, and
directory fsync stages in host temporary storage and checked the resulting
record and temporary-file state. Result/operation store tests covered valid
round trips and rejection of malformed or unsafe-permission records. The two
journal checks inspect source ordering and recovery guards; they are not
hardware crash tests.

Preflight commands:

```sh
ip route get 192.168.1.1
ip route get 192.168.1.2
ip route get 1.1.1.1
```

Both router routes selected `enp10s0`; the Internet route selected
`192.168.64.1` via `enp9s0`. The following shell arrays express the exact
SSH options used for read-only preflight and post-restart checks:

```bash
C=(ssh -o BatchMode=yes -o ConnectTimeout=5 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR root@192.168.1.1)
A=(ssh -o BatchMode=yes -o ConnectTimeout=5 -o StrictHostKeyChecking=yes -o LogLevel=ERROR root@192.168.1.2)

"${C[@]}" 'printf ssh_ok'
"${A[@]}" 'printf ssh_ok'
"${C[@]}" 'ubus -S call wmcs status' | jq -ce '{daemon_version,role,state,mutation_enabled,mutation_available,ubus_connected,degraded,discovery_active,pairing_active,pairing_state,control_active,control_state,control_operation,control_reconciliation_pending,identity_ready,paired_peer_count,active_controller_count,generation}'
"${A[@]}" 'ubus -S call wmcs status' | jq -ce '{daemon_version,role,state,mutation_enabled,mutation_available,ubus_connected,degraded,discovery_active,pairing_active,pairing_state,control_active,control_state,control_operation,control_reconciliation_pending,identity_ready,paired_peer_count,active_controller_count,generation}'
"${C[@]}" 'ubus -S call wmcs peers' | jq -ce '{peers:[.peers[]|{role,state,generation}]}'
"${A[@]}" 'ubus -S call wmcs peers' | jq -ce '{peers:[.peers[]|{role,state,generation}]}'
"${C[@]}" 'for c in network wireless firewall dhcp; do printf "%s " "$c"; uci export "$c" | sha256sum; done'
"${A[@]}" 'for c in network wireless firewall dhcp; do printf "%s " "$c"; uci export "$c" | sha256sum; done'
"${C[@]}" 'test -e /sys/class/net/phy1-ap0/operstate && ubus -S list hostapd.phy1-ap0 >/dev/null && cat /sys/class/net/phy1-ap0/operstate'
"${A[@]}" 'test -e /sys/class/net/phy1-ap0/operstate && ubus -S list hostapd.phy1-ap0 >/dev/null && cat /sys/class/net/phy1-ap0/operstate'
"${C[@]}" 'ls -A /etc/wmcs && printf "peer_file_count=" && find /etc/wmcs/peers -maxdepth 1 -type f | wc -l'
"${A[@]}" 'ls -A /etc/wmcs && printf "peer_file_count=" && find /etc/wmcs/peers -maxdepth 1 -type f | wc -l'
```

The identity fingerprints and peer records were also compared across the two
routers without printing their values. Each active peer matched the opposite
router's identity, and the agent's `wireless.wmcs_home_5g.wmcs_owner` matched
the controller identity; `wmcs_managed=1`, `device=radio1`.

Before the restart, both SSH paths worked. Each daemon reported
`state=transactional`, `ubus_connected=true`, `degraded=false`,
`mutation_available=true`, one active peer, generation `84`, and no active
discovery, pairing, control, or reconciliation. Peer generation was `5` on
both sides. Both `hostapd.phy1-ap0` objects existed and both interface
operstates were `up`. The controller protected root held only
`generation.state`, `identity.key`, and `peers` with one peer file. The agent
also held the expected `control.result`; neither had a pending marker.

UCI export SHA-256 values, measured before the restart and unchanged at the
final sequential recheck:

| Export | Controller | Agent |
|---|---|---|
| `network` | `e701a13108b281b52606f6a0729045a0695a3cc7437cf54dd0f5e2aaeb28bb66` | `3e1140173cd6ad351da6439caa4d88623265736a5765719692a76106c5332f2a` |
| `wireless` | `b7e0a6fa6ad75cd6dfb91560671b590fe45d152a06774f1fb78a4f170b5a6b36` | `dcbc165ea344867f70ea550a0a5e1b62a750deca23c8cebb75269e3c1c7fb728` |
| `firewall` | `967fee4bb6bb3a43748418bb3cde26a52619a7143a12521492e2d1f97dfd3155` | `6e1874c23a318f406f388c07c1f4f39d5daee3e0b027da0cd8f0e6258a4fa708` |
| `dhcp` | `5efe1da45a61b241a1997d9854eb4781a990d29358a53aad42da5687a25d114f` | `00649cce7893dd5e389d2e69874caa730a7bfe87b34e4db6f5f2440d1373b113` |

## Controlled hardware action and stop condition

After the preflight passed, the only hardware action was:

```sh
ssh -o BatchMode=yes -o ConnectTimeout=5 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR root@192.168.1.1 'service wmcsd restart'
```

It returned exit 0 at about 12:32 MSK. The first post-restart parallel read
found both daemons r6, paired, idle, and at generation `84`; both four-export
hash sets matched the table; both SSH reachability probes passed. Two other
concurrent SSH sessions returned exit 255 with
`kex_exchange_identification: read: Connection reset by peer`: the controller
AP read and the agent protected-root read. The cause was not established.
The requested stop rule was applied: no agent restart or further mutation.

A sequential read-only repeat of every command in the preflight block then
passed on both routers: SSH `ssh_ok`, r6 transactional/healthy/idle status,
one active peer each at peer generation `5`, local generation `84`, every UCI
hash unchanged, both AP operstates `up`, and the same protected-root names and
one peer file each. A further read-only cross-identity and WLAN owner-marker
comparison also passed after the restart, without printing identifiers.
The orchestration root independently confirmed both wireless hashes and both
r6 statuses. These observations confirm the final state, while the two initial
SSH resets remain a management-path blemish in this run.

## Case classification

| Case | Result and boundary |
|---|---|
| Host atomic write `-ENOSPC` at four named stages | Passed in temporary host storage; no router flash fault inferred. |
| Host durable result and controller operation stores | Passed save/load and malformed-record checks; no live router interruption inferred. |
| Host controller and forget journal contract checks | Passed static ordering/guard checks; no live journal interruption inferred. |
| r6 controller idle service restart | Final state preserved on the exact pair; first parallel AP/protected-state sampling had two SSH resets, then all sequential checks passed. |
| r6 agent idle service restart | Skipped after the SSH resets, per stop rule; Cudy has no independent recovery path. |
| Physical power loss at a durable boundary | Untested: power interruption is unavailable; controller UART alone does not recover the agent. |
| Actual router flash ENOSPC/read-only behavior | Untested: no safe bounded injection and independent agent recovery path. No storage was filled. |
| Mid-UCI apply/reload interruption or rollback | Untested: no safe exact-boundary interruption and recovery capability. |

The healthy final r6 state is process-restart evidence only. It does not prove
rollback, replay safety under a physical cut, or behavior on exhausted flash.

## Future physical-fault runbook

Arrange controlled power and an independently tested recovery path for each
router, especially the Cudy, and verify the pinned controller SSH key through
a trusted path. Preserve recovery images and private configuration outside Git.
In a maintenance window, capture sanitized status, generation, peer state, UCI
hashes, AP state, management routes, and protected-state filenames. Arm one
fault with a verifiable durable boundary, interrupt only the named router,
and inspect recovery before any new operation. After each case, require the
expected journal disposition, exact configuration hashes, paired identity and
generation, AP state, and management reachability; stop on any mismatch.
Use a bounded fault hook or sacrificial image for ENOSPC rather than filling
live router storage. Do not combine fault injection with package changes or
normal lifecycle operations.
