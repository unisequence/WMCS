# Experiment WMCS-EXP-0001

Status: completed

## Question

Can the first aarch64 `wmcsd` binary run on a Globitel BT-RB300, expose only
the documented read API, and leave the relevant OpenWrt UCI configuration
unchanged?

## Provenance

- Researcher: local WMCS development session
- Date and timezone: 2026-09-20, Europe/Moscow
- Public references: WMCS ubus API v0 and repository source
- Clean-room role: implementation
- Data handling classification: sanitized

## Topology

The development host used a dedicated Ethernet management interface at
`192.168.1.182/24`. The router management address is recorded only as lab alias
`router-a`; no wireless or production data path was changed.

## Devices

| Role | Vendor and model | Hardware revision | Firmware / OpenWrt build | MAC alias | Notes |
|---|---|---|---|---|---|
| Target | Globitel BT-RB300 | unknown | OpenWrt SNAPSHOT `r0+36056-d019f0b2e3`, kernel 6.18.44, mediatek/filogic | router-a | ARMv8 |
| Development host | Linux workstation | n/a | local WMCS build environment | host-a | SSH over dedicated Ethernet subnet |

## Initial state

- Factory-clean or prior state: existing lab configuration; `wmcsd` absent
- Combined configuration SHA-256: `f6093d5ea37302ef96d1e11c1fca33bb83ba6c3d6d499b9b48ea83d1ff5596ae`
- Package inventory SHA-256: not collected; `wmcsd` confirmed absent
- Relevant service and interface state: no `wmcs` ubus object
- Recovery image/configuration: local matching OpenWrt buildroot available
- Independent management path verified: yes, dedicated wired interface

The configuration digest covers ordered exports of `network`, `wireless`,
`firewall`, and `dhcp`. Raw values were neither printed nor stored.

## Controlled action

Copy the stripped aarch64 binary to `/tmp`, run
`read-only-smoke.sh /tmp/wmcsd-read-only-test standalone`, call `status`,
`capabilities`, and `nodes`, then terminate the process and remove all temporary
files.

## Expected observation

The daemon registers `wmcs`, reports API version 0 and
`mutation_enabled=false`, returns an empty node list, and does not change the
combined UCI digest.

## Captures and artifacts

| Artifact | Storage reference | SHA-256 | Sanitized fixture committed? |
|---|---|---|---|
| Target binary | generated outside Git | `e1b5ab241cafabe80fbaa138c290b005a017c61cd2cd245b58e6ef3e030b7400` | no |
| Smoke script | `tools/lab/read-only-smoke.sh` | recorded by source control | yes |
| Packet capture | not collected | — | no |

## Observed behavior

- daemon version: `0.1.0-r1`;
- role/state: `standalone` / `read_only`;
- `mutation_enabled`: `false`;
- read-only inventory: `true`;
- native protocol, discovery, pairing, transactions, roaming, and wireless
  backhaul: all `false`;
- platform probes found ubus, UCI, hostapd, and iw;
- node list was empty;
- the daemon exited normally and all temporary files were removed.

## State diff

- Target before/after: combined UCI digest remained
  `f6093d5ea37302ef96d1e11c1fca33bb83ba6c3d6d499b9b48ea83d1ff5596ae`
- Unrelated state preserved: yes, for the four hashed UCI packages
- Services restarted or interrupted: none observed

## Failure and recovery

- Failure injected: none
- Expected rollback: not applicable; no mutation path exists
- Observed rollback: not applicable
- Management connectivity preserved: yes
- Manual recovery required: no

## Interpretation

- Conclusion: the read-only daemon is ABI-compatible with the tested target and
  performed no detected UCI mutation during the smoke test.
- Confidence: high for this bounded test
- Alternative explanations: the short run does not prove leak-free long-term
  operation or two-node behavior.
- Follow-up experiment: enabled read-only soak after node discovery exists.

## Compatibility impact

The exact Globitel target is `observed` for the read-only runtime only. This is
not a controller, agent, discovery, adoption, or roaming support claim.
