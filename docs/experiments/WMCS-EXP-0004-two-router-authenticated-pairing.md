# Experiment WMCS-EXP-0004

Status: completed

## Question

Can the Globitel BT-RB300 controller and Cudy TR3000 agent create persistent
key-based identities, mutually authenticate one ephemeral exchange, present the
same SAS, reject a wrong SAS, recover the paired records after daemon restart,
and leave native OpenWrt configuration unchanged?

## Provenance

- Researcher: local WMCS development session
- Date and timezone: 2026-09-20, Europe/Moscow
- Public references: ADR 0004, ADR 0005, PSA Crypto API, Native WMCS protocol v0
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

Both peers used `br-lan`. The existing Ethernet management and recovery path
from EXP-0003 was retained. No WAN or wireless link carried pairing traffic.

## Devices

| Role | Vendor and model | Hardware revision | Firmware / OpenWrt build | MAC alias | Notes |
|---|---|---|---|---|---|
| Controller | Globitel BT-RB300 | unknown | OpenWrt SNAPSHOT `r0+36056-d019f0b2e3`, kernel 6.18.44, mediatek/filogic | router-a | native Snapshot ABI |
| Candidate/member | Cudy TR3000 | v1, U-Boot layout | OpenWrt 25.12.5 `r33051-f5dae5ece4`, kernel 6.12.94, mediatek/filogic | router-b | tested with 24.10-compatible private library aliases |
| Development host | Linux workstation | n/a | local WMCS build environment | host-a | orchestration over wired LAN |

## Initial state

- Factory-clean or prior state: network state and persistent packaged services
  were unchanged from EXP-0003. Test daemons used new private directories under
  `/tmp`, so neither device initially had a test identity or peer record.
- Package inventory SHA-256: not collected.
- Relevant service state: persistent `wmcsd 0.1.0-r1` processes were stopped
  only for the temporary run and restored afterward.
- Recovery image/configuration: available outside Git.
- Independent management path verified: yes, wired SSH.

Measured UCI digests before and after:

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

1. Cross-build and run a standalone PSA Crypto spike on both devices for P-256
   ECDSA, P-256 ECDH and HKDF-SHA256.
2. Start temporary final daemons with roles controller/agent, interface
   `br-lan`, and separate `/tmp/wmcs-pairing-state` directories.
3. Run bounded discovery, retain the agent candidate ID, and close discovery.
4. Open 30-second agent and controller pairing windows.
5. Wait for `awaiting_confirmation`; compare SAS and pending peer identities.
6. Submit `0000-0000-0000` to the controller and require rejection without a
   peer record or state transition.
7. Submit the matching SAS locally on both devices.
8. Stop and restart both temporary daemons with the same test state directory;
   inspect identity fingerprint, peer count and generation.
9. Compare the eight UCI digests, delete all temporary test identities, peer
   records, binaries and logs, and restore both persistent services.

## Expected observation

Both devices derive one identical SAS and opposite peer identities. A wrong SAS
fails closed. Correct confirmation creates one mode-`0600` peer record per
device in a mode-`0700` state tree. Restart preserves fingerprint, peer count
and generation. No UCI digest changes.

## Captures and artifacts

| Artifact | Storage reference | SHA-256 | Sanitized fixture committed? |
|---|---|---|---|
| Final Snapshot-ABI controller binary | generated outside Git | `69ed5fab00e9be2c720383e067a0cfe86ae3b8da1c529a2b9cefdfad583adb7e` | no |
| Final 24.10-ABI agent binary | generated outside Git | `d9416d4344ae42f5a98562f2272ce67aeb447a6b32019fd2af6eb8280264df08` | no |
| Packet capture | not collected | — | no |
| Test identities and relationship records | deleted after validation | not retained | no |
| Temporary daemon logs | deleted after validation | not retained | no |

No private key, ECDH secret, relationship key, WLAN credential or raw peer
record was retained or committed.

## Observed behavior

- The PSA crypto and persistent identity/pairing spikes completed successfully
  on both devices.
- Both temporary daemons lazily created identities and reached
  `awaiting_confirmation` during the 30-second windows.
- Both reported SAS `6f36-8303-8396`; each pending peer ID matched the other
  device's public identity fingerprint.
- Wrong SAS returned ubus permission denied and left the controller awaiting
  confirmation with no peer count or generation change.
- Correct local confirmation on both sides produced `paired_peer_count=1` and
  `generation=1`.
- State directories were mode `0700`; the 40-byte identity record and 128-byte
  peer record were mode `0600`.
- After daemon restart, both sides recovered the same local fingerprints,
  `paired_peer_count=1`, and `generation=1`.

## State diff

- Controller before/after: all four UCI file digests matched.
- Member before/after: all four UCI file digests matched.
- Unrelated state preserved: yes, within the measured UCI scope.
- Services restarted or interrupted: temporary WMCS daemons only; persistent
  services were restored after deleting all test state and artifacts.

## Failure and recovery

- Failure injected: one wrong-SAS confirmation.
- Expected rollback: no peer record and exchange remains pending.
- Observed rollback: no persistent peer state or generation change; the correct
  SAS could subsequently complete pairing.
- Management connectivity preserved: yes.
- Manual recovery required: no.

## Interpretation

- Conclusion: authenticated identity pairing works for this exact wired device
  pair and survives daemon restart without changing measured OpenWrt config.
- Confidence: high for the observed happy path, wrong-SAS behavior, file modes
  and process restart; medium for network-adversary resistance because no
  capture, replay or malformed-packet injection was performed on hardware.
- Alternative explanations: router reboot, power loss during atomic write and
  simultaneous competing candidates were not exercised.
- Unknown fields or transitions: timeout after key exchange, half-confirmed
  recovery, identity rotation and coordinated peer deletion remain untested.
- Follow-up experiment: authenticated control-session establishment followed by
  a dry-run WLAN transaction, then apply/verify/rollback on an explicitly
  selected source WLAN.

## Compatibility impact

The exact Globitel/Cudy pair becomes `observed` for native authenticated
identity pairing. This is not a supported adoption, configuration, mesh,
roaming or secure application-session claim.
