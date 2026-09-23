# ADR-0002: Apache License 2.0

Status: accepted

## Context

WMCS is intended to integrate with Freenetic while remaining usable as a
separate OpenWrt daemon and protocol boundary. The project needs one explicit
license before implementation and external contributions begin.

## Decision

WMCS is licensed under the Apache License, Version 2.0. New source files use the
SPDX identifier `Apache-2.0` where their format supports comments.

Third-party dependencies, fixtures, generated artifacts, and imported public
specifications retain their own licenses and require explicit provenance and
compatibility review. The WMCS license does not authorize proprietary firmware,
vendor resources, or quarantined implementation code to enter this repository.

## Consequences

WMCS can be distributed independently and incorporated into Freenetic subject
to the Apache-2.0 notice and attribution requirements. A dependency inventory
and generated notices will be required before binary releases.

## Validation

Repository and package checks must require the root `LICENSE`, verify package
metadata, and reject dependencies or artifacts without recorded provenance.
