# Experiment WMCS-EXP-0008

Status: runtime package gate passed; Cudy live rollback not executed

## Question

Can each lab node replace its installed basic wpad build with an exact,
repository-trusted full wpad build in one solver transaction, and is a tested
package-level rollback available before any 802.11k/v configuration is
changed?

## Provenance

- Researcher: local WMCS development session
- Date and timezone: 2026-09-20, Europe/Moscow
- Package source: official OpenWrt download repositories
- Trust anchors: the OpenWrt APK key already installed on each target
- Clean-room role: deployment preflight only

No roamd binary, source, trace, or configuration was used.

## Initial state

| Node | Installed wpad | Installed `hostapd-common` | Recovery path |
|---|---|---|---|
| Globitel BT-RB300, `192.168.1.1` | `wpad-basic-mbedtls 2026.08.07~831364bf-r2` | `2026.08.07~831364bf-r2` | wired management |
| Cudy TR3000 v1, `192.168.1.2` | `wpad-basic-mbedtls 2025.08.26~ca266cc2-r1` | `2025.08.26~ca266cc2-r1` | wireless WDS only during this preflight |

Both packages are explicit constraints in `/etc/apk/world`. Therefore merely
adding `wpad-mbedtls` is correctly rejected by the solver: the two packages
provide conflicting versioned `hostapd` and `wpa-supplicant` identities.

## Candidate artifacts

| Node | Signed index | Full package | Rollback package | Common package |
|---|---|---|---|---|
| Globitel | `snapshots/packages/aarch64_cortex-a53/base/packages.adb` | `wpad-mbedtls-2026.08.07~831364bf-r2.apk` | `wpad-basic-mbedtls-2026.08.07~831364bf-r2.apk` | `hostapd-common-2026.08.07~831364bf-r2.apk` |
| Cudy | `releases/25.12.5/packages/aarch64_cortex-a53/base/packages.adb` | `wpad-mbedtls-2025.08.26~ca266cc2-r2.apk` | `wpad-basic-mbedtls-2025.08.26~ca266cc2-r2.apk` | `hostapd-common-2025.08.26~ca266cc2-r2.apk` |

The Cudy repository no longer publishes the installed `r1` artifacts. Its
forward transaction must therefore update `hostapd-common` to `r2` together
with full wpad. The available rollback returns it to basic wpad `r2`, not to
the byte-identical `r1` package.

Raw downloaded-file SHA-256 values:

| Artifact | SHA-256 |
|---|---|
| release `packages.adb` | `1ffc9a7c2a839084ccf58841a669f98376be026b16e73bbd4f5d1ee1a3233150` |
| release full wpad | `aa52258eba54933359521e887a31ae6ae40a6666e3661765e1f883f075d5a4d2` |
| release basic wpad | `2a3fbe4846270b85a3ab59a0def41c37c538e52a808276b2f60150d71956b0e6` |
| release `hostapd-common` | `55a71b147772f9bcb5b66953cd44f7606b4670f7c81639e0c3a870b88a5f58ff` |
| snapshot `packages.adb` | `56a509266ea54e311aa33cd70bc5b722e775f06d9a0a00a0855f18270339935d` |
| snapshot full wpad | `3afca649e6681408ffdd807fec323359a961cc363cc8b58f9cb05a749af3544d` |
| snapshot basic wpad | `530ba1f99894b61b2fd4a2af06996d55c6b912a9254b13a2c74cb6b3ebb5a8b8` |
| snapshot `hostapd-common` | `9c31696b57a7b52eff74d40f179fb621656579896ae3c05ec2ed813902d3e997` |

These are transport fingerprints, not APK v3 package identity hashes.

## Controlled action

1. Download the two official indexes and six package files to a temporary
   workstation directory.
2. Stream each index through the target node's own `apk verify`; both returned
   `OK` using the node's installed OpenWrt trust anchor.
3. Expose the unchanged index and artifacts over an isolated HTTP listener on
   `192.168.1.182`.
4. For every candidate and rollback artifact, run `apk fetch --stdout` against
   only that signed index and discard the returned bytes. All six fetches
   completed successfully, exercising APK's repository-integrity path.
5. Run both forward transactions with `apk add --simulate`.
6. Run both rollback selections with `apk add --simulate`.

Direct `apk verify` of a detached package reported `UNTRUSTED signature`; that
result was not treated as package provenance. The accepted verification path
was the trusted repository index plus `apk fetch`, which validates the package
selected from that index.

The package-gate phase changed no package, world constraint, UCI value, or
service. A later runtime phase is recorded below.

## Solver result

Forward selection for Globitel:

```text
(1/2) Purging wpad-basic-mbedtls (2026.08.07~831364bf-r2)
(2/2) Installing wpad-mbedtls (2026.08.07~831364bf-r2)
OK
```

Forward selection for Cudy:

```text
(1/3) Purging wpad-basic-mbedtls (2025.08.26~ca266cc2-r1)
(2/3) Upgrading hostapd-common (...-r1 -> ...-r2)
(3/3) Installing wpad-mbedtls (2025.08.26~ca266cc2-r2)
OK
```

The transaction is made atomic at the solver level by replacing the positive
basic-wpad world constraint with a negative one while adding full wpad:

```sh
apk add '!wpad-basic-mbedtls' 'wpad-mbedtls=<exact-version>'
```

The rollback reverses those world constraints:

```sh
apk add '!wpad-mbedtls' 'wpad-basic-mbedtls=<exact-version>'
```

Rollback selection succeeded for both repositories. On the current Cudy state
the rollback simulation selects the internally consistent basic/common `r2`
pair; on Globitel it is already a no-op because exact basic `r2` is installed.

## Runtime result: Globitel

The wired-recovery node completed the full runtime round trip:

1. APK replaced basic wpad with exact full wpad in the predicted two-package
   transaction.
2. A `wifi reload` alone did not replace the running executable. `/proc` showed
   hostapd and wpa_supplicant still executing the deleted basic-wpad inode.
3. Restarting `/etc/init.d/wpad` loaded the full binary. The running and on-disk
   SHA-256 values both became
   `fc9178f2f65c066d0fcca94c3a63359143ed3c08e3d6d67061b952b66adbdc39`.
4. `hostapd.phy1-ap0` then exposed `bss_transition_request` with the expected
   public argument schema. No transition request was sent.
5. The exact rollback restored basic wpad and, after a wpad restart, the method
   disappeared.
6. Reinstalling full wpad and restarting wpad made the method reappear.
7. Cudy's WDS link recovered after each controller-side service restart.

The managed-UCI digest immediately before the first package transaction and
after the final full-wpad start was identical:

```text
5ab679cf28e05b5792ed99cc6974bc69c78ec105cfa8ee2f61231b73300de690
```

The workstation route to `192.168.1.0/24` remained on the isolated lab
Ethernet interface; its Internet default route remained on the unrelated
`192.168.64.1` interface.

## Runtime result: Cudy

Before mutation, an offline rescue repository was staged in Cudy tmpfs. Its
index verified with the installed OpenWrt 25.12 key, all three artifacts passed
`apk fetch`, and the forward transaction simulated successfully.

The live transaction emitted:

```text
(1/3) Purging wpad-basic-mbedtls (...-r1)
(2/3) Upgrading hostapd-common (...-r1 -> ...-r2)
(3/3) Installing wpad-mbedtls (...-r2)
```

The WDS management link then dropped before APK returned its final status.
The intended watchdog was sequenced after the foreground `apk add`, so it was
never started. Restarting the controller-side WDS BSS did not restore an
association; the controller observed zero backhaul clients. The most likely
failure mode is that `hostapd-common` service handling stopped Cudy's old wpad
inside the multi-package transaction and no final service start occurred.
Whether APK committed the final package must be inspected after local access
returns; it is not inferred from partial terminal output.

Cudy's managed-UCI digest before the transaction was:

```text
5b108c340af411cf591a6fc49106544e4f8a4e98261606c4d4f007174e340223
```

No UCI mutation was requested. A normal power cycle restored Cudy's WDS path.
Post-boot inspection observed exact full wpad `r2`, exact `hostapd-common r2`,
the full-wpad world constraint, a live/on-disk binary match, an enabled 5 GHz
AP, the BTM ubus method, zero filtered Wi-Fi boot errors, and the unchanged
managed-UCI digest above. APK reported the current world as solved and the
exact basic-wpad `r2` rollback selection simulated successfully.

## Recovery correction

For a node managed only through the WLAN being restarted, the rollback process
must be armed **before** `apk add`, and its complete signed offline repository
must already be local. Host confirmation may cancel the timer only after:

- SSH has returned through the expected backhaul;
- the running wpad inode matches the installed full package;
- all required hostapd objects are enabled; and
- the BTM method is present.

If those checks are not confirmed before the deadline, the pre-armed process
must select exact basic wpad from the local signed index and restart wpad. A
post-transaction watchdog is not a valid wireless-only recovery mechanism.

## Installation gate

The package gate is passed, but it is not yet a roaming result. Runtime work
must proceed in this order:

1. retain the pre-armed offline rollback requirement for future wireless-only
   package changes;
2. enable WMCS-owned 802.11k/v fields and repeat the passive phone capability
   observation;
3. send no BTM request until the client advertises support and the policy
   state machine has a fresh eligible target.

## Interpretation

- Conclusion: matching official artifacts and solver paths exist. Full wpad
  exposes the required BTM method on Globitel and has a proven exact rollback.
  Cudy exposed a real wireless-only recovery-order defect, then booted the
  expected full package successfully. Its live basic-wpad rollback was not
  repeated.
- Confidence: high for package trust, solver behavior, and full-wpad runtime
  on both nodes; high for Globitel live rollback and simulated for Cudy.
- Compatibility impact: none yet. `802.11k/v roaming assistance` remains
  `planned`.
