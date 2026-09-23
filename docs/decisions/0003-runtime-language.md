# ADR-0003: Runtime implementation language

Status: evaluating

## Context

WMCS needs bounded untrusted-protocol parsing, explicit state machines, ubus and
UCI integration, and reproducible packages for aarch64 Cortex-A53 and
mipsel_24kc. The target includes routers with constrained memory.

Rust offers stronger memory-safety properties for protocol and state-machine
code. C has the smallest and best-established OpenWrt integration surface. A
language decision made from a host-only prototype would not test the actual
MT7621 and OpenWrt constraints.

## Evidence collected

- OpenWrt target staging contains `libubus`, `libubox`, and `libuci` for both
  aarch64 and mipsel_24kc.
- A hardened C/libubus status service cross-builds for both targets.
- The stripped C baseline is 67,632 bytes on aarch64 and 67,404 bytes on
  mipsel_24kc, with loaded section totals of 4,577 and 3,362 bytes respectively.
- OpenWrt's packages feed supports building Rust for aarch64 and mipsel, but the
  target standard libraries are not currently staged locally. Building the
  OpenWrt Rust host toolchain is therefore part of the cost under evaluation.

## Candidates

1. C daemon using `libubox`, `libubus`, and `libuci` directly.
2. Rust daemon using narrow FFI bindings to the native OpenWrt libraries.
3. Rust protocol/state core behind a small C OpenWrt adapter.

The third option is attractive only if it does not duplicate event loops,
ownership, error handling, or package complexity.

## Decision gate

Do not select a language until the Rust candidate has been measured for:

- reproducible aarch64 and mipsel_24kc cross-builds;
- stripped package size and dynamic dependencies;
- idle RSS and startup time on target hardware;
- ubus integration and generated/handwritten FFI surface;
- panic, unwind, allocator, atomics, and TLS behavior on MT7621;
- toolchain bootstrap time and CI storage cost;
- sanitizer, fuzzing, and simulator workflow.

If the Rust path cannot support both target families without a second runtime
architecture, choose C and isolate all untrusted parsing behind small,
fuzz-tested modules.
