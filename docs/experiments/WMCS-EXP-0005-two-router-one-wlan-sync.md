# Experiment WMCS-EXP-0005

Status: completed

## Question

Can the paired Globitel BT-RB300 controller copy one existing 5 GHz WLAN to a
clean Cudy TR3000 agent through authenticated control, roll back an injected
verification failure exactly, preserve unrelated configuration, survive both
router reboots, and continue authenticated operation?

## Provenance

- Researcher: local WMCS development session
- Date and timezone: 2026-09-20, Europe/Moscow
- Public references: ADR 0005, ADR 0006, Native WMCS protocol v0, OpenWrt UCI
  and ubus interfaces
- Clean-room role: implementation and validation
- Data handling classification: sanitized

## Topology

```text
development host (wired management)
                 |
  Globitel BT-RB300 LAN, 192.168.1.1
       controller and existing 5 GHz AP
                 |
          shared Ethernet/L2 LAN
                 |
  Cudy TR3000 LAN, 192.168.1.2
       agent and WMCS-created 5 GHz AP
```

Control traffic and client backhaul used `br-lan`. No wireless backhaul,
802.11s link, added DHCP authority, or WAN-to-LAN routing was introduced.
Wired SSH remained the independent management and recovery path.

## Devices

| Role | Vendor and model | Hardware revision | Firmware / OpenWrt build | MAC alias | Notes |
|---|---|---|---|---|---|
| Controller | Globitel BT-RB300 | unknown | OpenWrt SNAPSHOT `r0+36056-d019f0b2e3`, mediatek/filogic | router-a | `wmcsd 0.2.0-r1`, native APK/Snapshot ABI |
| Agent | Cudy TR3000 | v1, U-Boot layout | OpenWrt 25.12.5 `r33051-f5dae5ece4`, mediatek/filogic | router-b | `wmcsd 0.2.0-r1`; validated private runtime aliases for the available 24.10-built binary |
| Development host | Linux workstation | n/a | local WMCS/OpenWrt build environments | host-a | orchestration over wired LAN |

## Initial state

- The Cudy radio/AP configuration was reset and its existing default 2.4 and
  5 GHz AP sections were disabled.
- Both devices already held the mutually confirmed peer relationship produced
  by the pairing workflow. Each had one peer record.
- The controller source was `wireless.default_radio1` on `radio1`. Only the
  agent's `radio1` was selected as a target; 2.4 GHz was outside scope.
- `/etc/wmcs` was mode `0700`; identity and peer records were mode `0600`.
- Recovery snapshot storage was outside Git at
  `/home/uni/quarantine/wmcs-hil-20260920/`.
- Independent wired management was verified before mutation.

Initial UCI SHA-256 values:

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

1. Build/install `wmcsd 0.2.0-r1`, enable the persistent service and explicitly
   enable its mutation gate on both lab devices.
2. Execute sequence 2 as a dry-run from the discovered controller candidate to
   the paired agent identity.
3. Execute sequence 3 as the first apply. Its initial runtime verifier looked
   for a hostapd object named after the UCI section instead of the dynamically
   allocated interface. Require the resulting `verify_failure` to restore the
   exact pre-test wireless file.
4. Correct the verifier to resolve the section's real interface through
   `network.wireless status`, rebuild/reinstall, and execute sequence 4.
5. Verify the owned UCI section, the live hostapd object, LAN bridge membership,
   secret equality without printing either value, and all measured UCI hashes.
6. Reboot the agent and verify service, identity, peer generation, owned AP and
   management connectivity.
7. Reboot the controller and verify service, identity, peer generation and that
   the agent AP remains active independently.
8. Execute sequence 5 as an authenticated post-reboot dry-run and require no
   UCI change.

## Expected observation

Dry-run reports readiness without changing UCI. A failed runtime verification
restores the exact original wireless file. A valid apply creates only the
owned agent section, brings up a hostapd interface in `br-lan`, and returns a
committed result. Both services and the applied AP survive reboot; a later
authenticated sequence succeeds. No WLAN credential appears in ubus output,
logs, or this record.

## Captures and artifacts

| Artifact | Storage reference | SHA-256 | Sanitized fixture committed? |
|---|---|---|---|
| Controller APK used in the run | build path later regenerated | `0a46b71c91dfbb2094de4e7b16e4f43b93b8d5f16a75747558aaaaf944e75f7c` | no |
| Agent IPK | `/home/uni/openwrt-24.10/bin/packages/aarch64_cortex-a53/base/wmcsd_0.2.0-r1_aarch64_cortex-a53.ipk` | `80f9095500e66c140872563b59934e7bd81bcdced3aeb6ee9ce81583d11ba4ae` | no |
| Pre-test agent wireless backup | `/home/uni/quarantine/wmcs-hil-20260920/cudy-wireless.before` | `53a550a609c1a19a0fd3998b2448ec9958aca28bd4858cc8b95406d19c734255` | no |
| Runtime compatibility backup | `/home/uni/quarantine/wmcs-hil-20260920/runtime-before-0.2/` | recorded in external `SHA256SUMS` | no |
| Packet capture | not collected | — | no |

The controller's installed daemon binary hashed
`0be7e45227e7a169624fda1b29a7e9cabafa6047b3110bb972476504264cd08e`.
The agent's installed daemon binary hashed
`adc0c16e586986667ea64a33549942e01962485bcc3678553803a881cadbb28f`.
No WLAN credential, private key, relationship key, or raw peer record was
retained in the repository.

## Observed behavior

- Sequence 2 completed as `dry_run_ready`; all measured UCI hashes remained
  unchanged.
- During sequence 3, OpenWrt did create the AP, proving that apply had reached
  native services. The deliberately incorrect verifier then returned
  `rolled_back` with `verify_failure`. The agent wireless hash returned exactly
  to `53a550a609c1a19a0fd3998b2448ec9958aca28bd4858cc8b95406d19c734255`.
- The corrected verifier discovered runtime interface `phy1-ap0`, required
  `hostapd.phy1-ap0`, and sequence 4 completed as `committed`.
- The agent contained exactly the WMCS-owned `wireless.wmcs_home_5g` AP on
  `radio1`. Its encryption, SSID length (10 bytes), key length (9 bytes), and
  secret hashes matched the source; plaintext values were not emitted.
- `phy1-ap0` was a member of `br-lan` alongside the wired LAN interface. The
  controller remained on channel 112/HE160 and the agent on channel 36/HE80,
  confirming that the WLAN profile was synchronized without copying local
  radio/channel policy.
- Both real router reboots restored the enabled `wmcsd 0.2.0-r1` service,
  persistent identity, one trusted peer, and generation 4. The agent AP stayed
  active across the controller reboot.
- Sequence 5 completed as `dry_run_ready` after both reboots and advanced both
  durable generations to 5 without changing agent UCI.
- No `/etc/wmcs/wireless.pending` journal remained after rollback, commit, or
  reboot.

Final UCI SHA-256 values:

| Device | UCI file | SHA-256 |
|---|---|---|
| router-a | `/etc/config/network` | `fe7612ebc5c2d32f4d83f86003fb05c999bb1ec6431501c7d59a570ba3daf0f6` |
| router-a | `/etc/config/wireless` | `eb69217a5bd093e6dbff251ddc177ad71c689c5d1b91a564f6e74b0393fd05a8` |
| router-a | `/etc/config/firewall` | `4b204bfed25cdabd0566e2a2390cbd5b5fffadaeb74c2c5345bbd396c14679df` |
| router-a | `/etc/config/dhcp` | `27e76d9be33448dab114a4ec0ab629b715648ab02e89a8a6a13d1fc1675b329e` |
| router-b | `/etc/config/network` | `86746c56235c485b35f09acd73d8fa3ee752d8caed1f37a1a7ccfe7040ccad50` |
| router-b | `/etc/config/wireless` | `81c9b1f0a608a5af0b1a20ede3243caaae4b5863f5cb5dabb96623f135ec515b` |
| router-b | `/etc/config/firewall` | `7ec1d9cd5cf448cf26e1360b9fb23f59f60c40cd40a65da6a7d8a162b61af2c5` |
| router-b | `/etc/config/dhcp` | `3692e8591d44b5a5da12562b3ccc6247a65d9d8dce88e4add490f94b14ed0921` |

## State diff

- Controller before/after: all four measured UCI hashes matched.
- Agent before/after: only `/etc/config/wireless` changed, by addition of the
  explicitly owned AP; network, firewall and DHCP hashes matched.
- Unrelated state preserved: yes, within the measured UCI and runtime scope.
- Services restarted or interrupted: wireless was reloaded for apply and
  rollback; both routers were subsequently rebooted one at a time.

## Failure and recovery

- Failure injected: runtime verification looked up the wrong hostapd ubus
  object during sequence 3.
- Expected rollback: byte-for-byte restoration of the complete wireless UCI
  file and wireless reload.
- Observed rollback: exact original wireless SHA-256 restored; wired ping/SSH
  remained available; no pending journal remained.
- Management connectivity preserved: yes.
- Manual recovery required: no for rollback; the verifier correction was the
  intentional software change before retry.

## Interpretation

- Conclusion: this exact pair has a working authenticated, transactional,
  Ethernet-backed single-WLAN control path with demonstrated rollback and
  reboot persistence.
- Confidence: high for the measured one-agent path, ownership boundary,
  rollback, runtime AP existence and sequential reboot behavior; medium for
  crash/power-loss recovery because power was not cut at every journal state.
- Alternative explanations: no client station was used, so AP presence and
  matching profile do not by themselves prove handoff quality.
- Unknown fields or transitions: durable lost-result reconciliation, release,
  controller replacement, repeated power interruption, concurrent requests,
  multiple members, hostile traffic, and long-duration stability remain.
- Follow-up experiment: walk a real phone between APs while measuring BSSID,
  interruption and packet loss; then add 802.11k/v assistance and evaluate
  optional 802.11r separately.

## Compatibility impact

The exact Globitel/Cudy pair becomes `observed` for authenticated native
single-WLAN synchronization and reboot persistence. This is not yet a
`supported` release, a wireless-backhaul mesh claim, or evidence of seamless
client roaming.
