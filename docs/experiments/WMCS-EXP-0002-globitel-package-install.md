# Experiment WMCS-EXP-0002

Status: completed

## Question

Does the OpenWrt APK install cleanly on the matching Globitel snapshot while
leaving the daemon inactive and network configuration unchanged by default?

## Provenance

- Researcher: local WMCS development session
- Date and timezone: 2026-09-20, Europe/Moscow
- Public references: OpenWrt package infrastructure and WMCS package source
- Clean-room role: implementation
- Data handling classification: sanitized

## Topology and device

The topology and target are identical to WMCS-EXP-0001: Globitel BT-RB300 on
OpenWrt SNAPSHOT `r0+36056-d019f0b2e3`, managed over dedicated Ethernet.

## Initial state

- `wmcsd` package, process, and ubus object: absent
- Combined UCI SHA-256: `f6093d5ea37302ef96d1e11c1fca33bb83ba6c3d6d499b9b48ea83d1ff5596ae`
- Matching local buildroot and SSH recovery path: available

## Controlled action

Verify the copied package digest and APK signature, install it with
`apk add --no-network`, run the installed binary through the read-only smoke
test, and remove temporary package/test files. Do not set `wmcs.core.enabled`.

## Expected observation

The package installs three functional files, retains `enabled=0` and
`mutation_enabled=0`, and leaves no daemon process or `wmcs` ubus object after
the test.

## Captures and artifacts

| Artifact | Storage reference | SHA-256 | Sanitized fixture committed? |
|---|---|---|---|
| `wmcsd-0.1.0-r1.apk` | generated outside Git | `dc6cb8696e5bfdbf75599480ebd888b761094a56e685144b2d934afbda0d0231` | no |
| Installed binary | `/usr/sbin/wmcsd` | `e1b5ab241cafabe80fbaa138c290b005a017c61cd2cd245b58e6ef3e030b7400` | no |

## Observed behavior

- `apk verify` returned OK and installation completed successfully;
- package database reports `wmcsd-0.1.0-r1`;
- config contains role `standalone`, `enabled=0`, and `mutation_enabled=0`;
- OpenWrt created normal `S95wmcsd` and `K10wmcsd` init links, but the config
  gate prevented a persistent process from starting;
- installed-binary smoke test passed with the same read-only API response as
  WMCS-EXP-0001;
- final process and ubus object were absent;
- temporary files were absent.

## State diff

- Intended changes: package database plus `/usr/sbin/wmcsd`,
  `/etc/init.d/wmcsd`, `/etc/config/wmcs`, and package metadata
- Network configuration: combined digest unchanged
- Services restarted or interrupted: none observed
- Management connectivity preserved: yes

## Failure and recovery

- Failure injected: none
- Package removal path: `apk del wmcsd`
- Manual recovery required: no

## Interpretation

- Conclusion: packaging and inert-default behavior work on the exact tested
  snapshot.
- Confidence: high for install and initial runtime smoke; upgrade, uninstall,
  reboot, and long soak remain untested.
- Follow-up experiment: reboot with `enabled=0`, then uninstall and verify
  configuration ownership behavior.

## Compatibility impact

Packaging on this exact target becomes `observed`, not `supported`.
