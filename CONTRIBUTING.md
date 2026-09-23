# Contributing to WMCS

WMCS begins with conservative ownership, clean-room provenance, and repeatable
evidence. A working demo is not sufficient if it cannot be safely explained,
tested, and rolled back.

## Clean-room inputs

Acceptable implementation inputs include:

- public standards and public vendor documentation;
- traffic captured between devices owned and operated in the lab;
- documented user-visible inputs and outputs;
- before/after configuration and state observations;
- neutral protocol specifications derived from controlled black-box tests.

Do not contribute:

- decompiled or disassembled proprietary implementation code;
- copied private schemas, symbols, command tables, constants, or strings;
- proprietary libraries, firmware images, extracted resources, or credentials;
- source or close transliterations from quarantined reference implementations;
- raw captures containing unrelated user traffic or reusable secrets.

When strict separation is used, researchers produce a neutral behavioral
specification and implementers work only from that specification and public
standards. Provenance uncertainty stops implementation until it is documented.

## Experiment discipline

Use [docs/experiments/TEMPLATE.md](docs/experiments/TEMPLATE.md). Prefer one
controlled variable per experiment. Record device and firmware versions,
topology, initial-state hashes, capture hashes, observations, confidence, and
unanswered questions. Store sensitive raw evidence outside Git.

Compatibility claims must be entered in
[docs/compatibility/MATRIX.md](docs/compatibility/MATRIX.md) and linked to
repeatable evidence.

## Implementation rules

- New source files use the SPDX identifier `Apache-2.0` where the file format
  supports comments.
- Native OpenWrt services remain the source of truth.
- Preserve unknown options and foreign sections.
- Mark every generated object with explicit ownership and scope.
- Do not use names, SSIDs, or MAC addresses as ownership proof.
- Do not use `sh -c`, `eval`, or generic command runners with untrusted data.
- Keep protocol parsing, platform mutation, UI, and release logic separated.
- Add a test for each parser, transition, ownership rule, and failure path.
- Never run destructive tests through the only management path or on a
  production router.

## Change workflow

1. State the invariant or observed behavior being changed.
2. Link public references or an experiment record.
3. Add or update deterministic tests.
4. Run `make check`.
5. Document compatibility impact and rollback behavior.
6. Keep commits focused; do not include generated release artifacts.

The implementation language is intentionally undecided until the OpenWrt
aarch64 and mipsel toolchain/footprint spike is recorded as an architecture
decision.
