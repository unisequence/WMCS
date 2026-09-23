# WMCS

[Русская версия](README.ru.md)

[Releases](docs/RELEASES.md) · [Changelog](docs/CHANGELOG.md)

[Security](SECURITY.md) · [Contributing](CONTRIBUTING.md) · [Architecture](ARCHITECTURE.md) · [Compatibility](docs/compatibility/MATRIX.md) · [Plan](PLAN.md)

WMCS (Wi-Fi Mesh Coordination System) is a clean-room coordination layer for
managed Wi-Fi systems on OpenWrt. It discovers and adopts members, coordinates
owned configuration, and exposes topology and health without becoming part of
the packet-forwarding or radio datapath.

WMCS is an independent project. Freenetic may provide an optional presentation
layer for the versioned WMCS ubus API, but the daemon, protocol and ownership
model remain separate.

> **Disclaimer**: WMCS is not affiliated with NDM Systems or Keenetic. It does
> not contain proprietary vendor source, donor binaries, extracted firmware
> resources, or copied private schemas. The public implementation is based on
> public standards, public documentation, and observations from hardware owned
> and operated in the lab.

## Project status

WMCS is an experimental alpha. The native v0 controller/agent path has reached
an observed two-router pilot:

- bounded same-LAN discovery;
- persistent P-256 identities and SAS-confirmed pairing;
- encrypted, replay-protected control;
- transactional synchronization of one owned home WLAN;
- exact rollback and ownership-safe release;
- durable peer, operation and result state;
- OpenWrt IPK/APK packaging and a LuCI view;
- real daemon restart, router reboot and sysupgrade preservation checks.

The current pilot uses a Globitel BT-RB300 controller and a Cudy TR3000 v1
agent. This is not yet a supported mesh release. Power-cut recovery, hostile
network testing, multiple members, wireless backhaul, key rotation and repeated
roaming validation remain release gates.

The roaming boundary is deliberately conservative. WMCS can observe and
coordinate native OpenWrt capabilities, but it does not replace hostapd,
netifd, nl80211 or the kernel. Roaming policy remains opt-in and is disabled by
default.

## Tested hardware

| Device | OpenWrt / target | Role | Status |
|---|---|---|---|
| Globitel BT-RB300 | MediaTek Filogic, aarch64, OpenWrt SNAPSHOT | Controller | Observed pilot |
| Cudy TR3000 v1 | MediaTek Filogic, aarch64, OpenWrt 25.12.5 | Agent | Observed pilot |

Compatibility claims are intentionally tied to exact hardware, firmware,
target, role and evidence. See the [compatibility matrix](docs/compatibility/MATRIX.md).

## Quick start

Run the repository checks:

    make check

Run the transport-independent simulator:

    PYTHONPATH=simulator python3 -m wmcs_sim

Build the daemon for the local cross-toolchains:

    make check-wmcsd-cross

Build and inspect the OpenWrt package in a matching buildroot:

    make check-openwrt-package OPENWRT_DIR=/path/to/openwrt

The first public release will publish target-specific package instructions and
immutable hashes. Until then, deployment is a controlled development-lab
operation, not a general installation path.

## Core invariants

- OpenWrt UCI, ubus, netifd, hostapd, nl80211 and the kernel remain the native
  source of truth.
- WMCS is not in the data path.
- WMCS stores only protocol-specific state.
- Every generated OpenWrt object has an explicit ownership marker.
- Mutations are transactional and have a verified rollback path.
- Pairing is explicit, local, authenticated and time limited.
- Unknown or foreign configuration is preserved.
- Public compatibility claims link to repeatable evidence.

## Freenetic relationship

WMCS is designed to be an optional Freenetic integration boundary, not a
hidden subsystem. Freenetic can render WMCS state and request documented ubus
methods through the method-exact rpcd ACL. Browser code must not contain
protocol logic, access private WMCS state, or mutate native configuration
outside the daemon's authenticated ownership checks.

This separation keeps WMCS useful with plain LuCI and allows Freenetic to
remain a presentation and management layer.

## Repository layout

- src/wmcsd/ — target-side daemon, protocol state machines and native adapters.
- simulator/ — transport-independent protocol and roaming model.
- package/openwrt/ — wmcsd and luci-app-wmcs package sources.
- tests/ — deterministic wire, store and transaction tests.
- docs/ — decisions, protocol contracts, requirements, experiments and
  compatibility evidence.
- scripts/ — repository and contract checks.
- tools/lab/ — controlled hardware-lab observation and deployment helpers.
- spikes/ — isolated toolchain and crypto probes that inform architecture
  decisions but are not runtime dependencies.

Generated binaries, raw captures, firmware, credentials, private keys and
device backups stay outside Git.

## Roadmap

The first release line is intentionally narrow:

- one controller and one agent;
- Ethernet or same-LAN backhaul;
- explicit time-limited adoption;
- one managed WLAN profile;
- authenticated control with rollback;
- no modification of unrelated configuration.

After the alpha gates: power-cut recovery, a signed release path, multiple
members, additional WLAN segments, optional wireless backhaul, native roaming
policy integration and a stable Freenetic view.

Keenetic MWS interoperability is a separate future protocol-adapter track. It
is not part of the native WMCS MVP.

## License

WMCS is licensed under the [Apache License 2.0](LICENSE).
