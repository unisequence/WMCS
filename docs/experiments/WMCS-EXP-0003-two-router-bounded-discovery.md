# Experiment WMCS-EXP-0003

Status: completed

## Question

Can a Globitel BT-RB300 controller and a Cudy TR3000 agent discover each other
over one Ethernet LAN in either window-start order, close their discovery
sockets on command or timeout, and leave native OpenWrt configuration
unchanged?

## Provenance

- Researcher: local WMCS development session
- Date and timezone: 2026-09-20, Europe/Moscow
- Public references: ADR 0004, Native WMCS protocol v0, WMCS ubus API v0
- Clean-room role: implementation and validation
- Data handling classification: sanitized

## Topology

```text
development host (192.168.1.182/24)
                 |
       Globitel BT-RB300 LAN (controller, router-a, 192.168.1.1)
                 |
          Cudy TR3000 LAN (agent, router-b, 192.168.1.2)
```

Both router links used LAN bridge `br-lan`; the Cudy WAN port was unused. The
Globitel remained the existing LAN gateway. The Cudy DHCPv4, DHCPv6 and RA
servers were disabled before this experiment, so only one DHCP authority was
present. SSH over the same wired LAN was the management and recovery path.

## Devices

| Role | Vendor and model | Hardware revision | Firmware / OpenWrt build | MAC alias | Notes |
|---|---|---|---|---|---|
| Controller | Globitel BT-RB300 | unknown | OpenWrt SNAPSHOT `r0+36056-d019f0b2e3`, kernel 6.18.44, mediatek/filogic | router-a | native snapshot ABI |
| Candidate/member | Cudy TR3000 | v1, U-Boot layout | OpenWrt 25.12.5 `r33051-f5dae5ece4`, kernel 6.12.94, mediatek/filogic | router-b | private WMCS library aliases for the tested 24.10 binary |
| Development host | Linux workstation | n/a | local WMCS build environment | host-a | wired management and test orchestration |

## Initial state

- Factory-clean or prior state: Cudy was reset before lab preparation; only its
  LAN management address, gateway/DNS, and DHCP/RA disablement were changed.
  Globitel retained its existing lab configuration.
- Cudy clean snapshot manifest SHA-256:
  `5f92f9cc33cec51a2e496cd5e893abc8e1741391d21dc0220cfc318b79c7577e`
- Package inventory SHA-256: not collected.
- Relevant service and interface state: both persistent `wmcsd 0.1.0-r1`
  services were read-only; router-a had role `controller`, router-b role
  `agent`, and `mutation_enabled=false` on both.
- Recovery image/configuration: clean Cudy snapshot and matching Globitel
  buildroot available outside Git.
- Independent management path verified: wired SSH remained reachable throughout.

The following direct file digests were recorded before and after both runs:

| Device | UCI file | SHA-256 |
|---|---|---|
| router-a | `/etc/config/network` | `fe7612ebc5c2d32f4d83f86003fb05c999bb1ec6431501c7d59a570ba3daf0f6` |
| router-a | `/etc/config/wireless` | `eb69217a5bd093e6dbff251ddc177ad71c689c5d1b91a564f6e74b0393fd05a8` |
| router-a | `/etc/config/firewall` | `4b204bfed25cdabd0566e2a2390cbd5b5fffadaeb74c2c5345bbd396c14679df` |
| router-a | `/etc/config/dhcp` | `27e76d9be33448dab114a4ec0ab629b715648ab02e89a8a6a13d1fc1675b329e` |
| router-b | `/etc/config/network` | `86746c56235c485b35f09acd73d8fa3ee752d8caed1f37a1a7ccfe7040ccad50` |
| router-b | `/etc/config/wireless` | `53a550a609c1a19a0fd3998b2448ec9958aca28bd4858cc8b95406d19c734255` |
| router-b | `/etc/config/firewall` | `7ec1d9cd5cf448cf26e1360b9fb23f59f60c40cd40a65da6a7d8a162b61af2c5` |
| router-b | `/etc/config/dhcp` | `3692e8591d44b5a5da12562b3ccc6247a65d9d8dce88e4add490f94b14ed0921` |

## Controlled action

Stop the persistent services temporarily and run development binaries from
`/tmp`, bound explicitly to `br-lan`:

1. Open a 20-second agent window, then a 20-second controller window. Observe
   the candidate and close both windows explicitly.
2. Restart both development daemons to obtain new boot-instance IDs. Open a
   10-second controller window, wait two seconds, then open a 7-second agent
   window. Observe the candidate and let both windows expire automatically.
3. Compare all eight UCI file digests, remove temporary binaries and logs, and
   restore the persistent services.
4. After edge-case review, repeat a 7-second agent-first run with the final
   package-built binaries. Also call `status` and `discovery_start` with one
   unknown field and require both requests to fail before opening the valid
   window.

No pairing, adoption, transaction, wireless, DHCP, firewall, or network method
was invoked.

## Expected observation

The controller records exactly one bounded `ephemeral_untrusted` agent at the
Cudy management address in either order. Both windows become inactive after
stop/timeout. No UCI digest changes and no temporary process or file remains.

## Captures and artifacts

| Artifact | Storage reference | SHA-256 | Sanitized fixture committed? |
|---|---|---|---|
| Initial snapshot-ABI controller binary | generated outside Git | `7f6af94cb5dc5e86c30a5fba5618f6d4d4e0342b423432b66d3d5dbfd2f63d4a` | no |
| Initial 24.10-ABI agent binary | generated outside Git | `f823d0474236fab724382c1f922cfbc5b0816b64f9cb3057eb0809032ce3fbef` | no |
| Final snapshot-ABI packaged controller binary | generated outside Git | `d000bc502e75360c71868dccb4aaf7706380d08e187a7fe14e0c44e90dee3de4` | no |
| Final 24.10-ABI packaged agent binary | generated outside Git | `8fa1c5d682c4bdf10128dabd3298079d14cd0688f3ebaca31be1628e4a548d7e` | no |
| Packet capture | not collected | — | no |
| Temporary daemon logs | inspected only on failure; not retained | — | no |

## Observed behavior

- Both binaries registered the `wmcs` ubus object and reported API version 0,
  `state=read_only`, `mutation_enabled=false`, and their configured roles.
- With agent-first order, router-a reported one node at `192.168.1.2` with
  role `agent`, state `discovered`, transport `native_udp_v0`, reason
  `native_v0_announcement`, and trust `ephemeral_untrusted`.
- With controller-first order and a two-second lead, router-a reported the same
  semantic node record with a different random boot-instance ID.
- Explicit `discovery_stop` made both first-run windows inactive.
- The 10-second and 7-second second-run windows both changed to inactive
  without a stop call.
- The final package-built binaries reproduced agent-first discovery in a
  7-second window and then became inactive automatically.
- Requests containing an unknown `bogus` field were rejected for both the
  no-argument `status` method and `discovery_start`; the rejected start did not
  open a window.
- The source address was observational metadata only; it was not treated as a
  stable identity or authority.
- Candidate retention expiry after 300 seconds was not exercised.

## State diff

- Controller before/after: all four UCI file digests matched.
- Member before/after: all four UCI file digests matched.
- Unrelated state preserved: yes, within the hashed UCI scope.
- Services restarted or interrupted: both persistent WMCS services were stopped
  briefly for each test-binary run and restored afterward. Native
  forwarding, DHCP authority and management connectivity remained available.
- Final state: persistent `wmcsd 0.1.0-r1` active as controller on router-a and
  agent on router-b; temporary HIL binaries and logs absent.

## Failure and recovery

- Failure injected: none.
- Expected rollback: not applicable; the development daemon has no mutation
  path and all discovery state is volatile.
- Observed rollback: explicit stop and timeout both removed the active window.
- Management connectivity preserved: yes.
- Manual recovery required: no.

## Interpretation

- Conclusion: bounded, same-LAN, read-only discovery works on this exact
  two-router pair in both start orders, and its window lifecycle does not alter
  the measured OpenWrt configuration.
- Confidence: high for candidate appearance and window closure on this topology;
  low for packet-level claims because no capture was collected.
- Alternative explanations: the test does not exercise routed or VLAN-separated
  networks, packet loss, duplicate traffic, hostile datagrams, reboot, or more
  than one agent.
- Unknown fields or transitions: 300-second candidate eviction and resource
  behavior under repeated windows remain unverified on hardware.
- Follow-up experiment: verify candidate eviction and malformed/replayed packet
  handling, then design cryptographic identity enrollment separately.

## Compatibility impact

The exact Globitel controller and Cudy agent become `observed` for bounded
read-only Ethernet discovery. This is not a pairing, adoption, mutation,
roaming, or supported-compatibility claim.
