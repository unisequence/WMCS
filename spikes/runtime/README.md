# Runtime and OpenWrt integration spike

This spike establishes a small C/libubus baseline before selecting the WMCS
runtime implementation strategy. It is not daemon production code.

Build both currently staged targets:

```sh
./spikes/runtime/build-c.sh aarch64
./spikes/runtime/build-c.sh mipsel
```

Override the buildroot with `OPENWRT_DIR=/path/to/openwrt`. Outputs are written
under `spikes/runtime/out/` and are not committed.

The next comparison is a Rust state-machine/core build using OpenWrt's Rust
package support, with the same target matrix and measurements:

- stripped binary/package size;
- dynamic dependencies;
- startup and idle RSS;
- ubus integration complexity;
- reproducibility on aarch64 and mipsel_24kc;
- build time and toolchain cost;
- unwind/panic and atomic support on MT7621.

## First C baseline

Using the locally staged OpenWrt 25.12 toolchains, the stripped ubus service
builds successfully for both targets:

| Target | ELF | Stripped size | Hardening |
|---|---|---:|---|
| aarch64 Cortex-A53 | 64-bit PIE | 67,632 bytes | PIE, RELRO, BIND_NOW, non-executable stack |
| mipsel_24kc | 32-bit MIPS32r2 PIE | 67,404 bytes | PIE, RELRO, BIND_NOW, non-executable stack |

Both variants dynamically use the platform `libubus`, `libubox`, `libgcc_s`,
and musl libc. These figures are a build/integration baseline, not a forecast
for the complete daemon. The loaded section baseline is 4,577 bytes on aarch64
and 3,362 bytes on mipsel; the larger file sizes primarily reflect ELF layout
and alignment.
