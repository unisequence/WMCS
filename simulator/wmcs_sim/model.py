# SPDX-License-Identifier: Apache-2.0

"""Transport-independent WMCS protocol types and validation rules."""

from __future__ import annotations

import hashlib
import re
from dataclasses import dataclass, field
from enum import Enum
from typing import Optional


IDENTIFIER_RE = re.compile(r"^[a-z0-9][a-z0-9_.-]{0,63}$")
FIELD_RE = re.compile(r"^[a-z][a-z0-9_.-]{0,127}$")
MAX_VALUE_BYTES = 512


class WmcsError(Exception):
    """Base class for errors represented by stable protocol reason codes."""

    code = "wmcs_error"


class ValidationError(WmcsError):
    code = "invalid_request"


class InvalidState(WmcsError):
    code = "invalid_state"


class OwnershipError(WmcsError):
    code = "ownership_conflict"


class ReplayError(WmcsError):
    code = "replay_rejected"


class TransactionError(WmcsError):
    code = "transaction_failed"


class NodeState(str, Enum):
    UNKNOWN = "unknown"
    DISCOVERED = "discovered"
    PAIRING = "pairing"
    ADOPTING = "adopting"
    CONFIGURING = "configuring"
    VERIFYING = "verifying"
    ONLINE = "online"
    DEGRADED = "degraded"
    RELEASING = "releasing"
    RELEASED = "released"
    FAILED = "failed"
    ROLLBACK_FAILED = "rollback_failed"


class TransactionPurpose(str, Enum):
    ADOPT = "adopt"
    RECONCILE = "reconcile"
    RELEASE = "release"


class TransactionStage(str, Enum):
    PREPARED = "prepared"
    APPLIED = "applied"
    VERIFIED = "verified"
    COMMITTED = "committed"
    ROLLED_BACK = "rolled_back"
    ROLLBACK_FAILED = "rollback_failed"


class JobState(str, Enum):
    QUEUED = "queued"
    PREPARING = "preparing"
    APPLYING = "applying"
    VERIFYING = "verifying"
    COMMITTED = "committed"
    ROLLING_BACK = "rolling_back"
    ROLLED_BACK = "rolled_back"
    ROLLBACK_FAILED = "rollback_failed"
    REJECTED = "rejected"


@dataclass(frozen=True)
class Identity:
    """Synthetic identity for the simulator; not production cryptography."""

    node_id: str
    fingerprint: str

    @classmethod
    def from_seed(cls, seed: str) -> "Identity":
        if not seed or len(seed.encode("utf-8")) > 256:
            raise ValidationError("identity seed must contain 1..256 bytes")
        fingerprint = hashlib.sha256(seed.encode("utf-8")).hexdigest()
        return cls(node_id=fingerprint[:16], fingerprint=fingerprint)

    @property
    def confirmation(self) -> str:
        return "-".join((self.fingerprint[0:4], self.fingerprint[4:8], self.fingerprint[8:12]))


@dataclass(frozen=True)
class Change:
    key: str
    value: Optional[str]

    def __post_init__(self) -> None:
        if not FIELD_RE.fullmatch(self.key):
            raise ValidationError(f"invalid configuration field: {self.key!r}")
        if self.value is not None:
            encoded = self.value.encode("utf-8")
            if len(encoded) > MAX_VALUE_BYTES:
                raise ValidationError(f"value for {self.key!r} exceeds {MAX_VALUE_BYTES} bytes")


@dataclass(frozen=True)
class Plan:
    transaction_id: str
    generation: int
    purpose: TransactionPurpose
    changes: tuple[Change, ...]
    claim_fields: frozenset[str] = frozenset()

    def __post_init__(self) -> None:
        if not IDENTIFIER_RE.fullmatch(self.transaction_id):
            raise ValidationError("invalid transaction identifier")
        if self.generation < 1:
            raise ValidationError("generation must be positive")
        if not self.changes or len(self.changes) > 64:
            raise ValidationError("a plan must contain 1..64 changes")

        keys = [change.key for change in self.changes]
        if len(keys) != len(set(keys)):
            raise ValidationError("a plan cannot change one field twice")
        if not self.claim_fields.issubset(keys):
            raise ValidationError("claimed fields must be present in the plan")


@dataclass
class Journal:
    plan: Plan
    before: dict[str, tuple[bool, Optional[str]]]
    stage: TransactionStage = TransactionStage.PREPARED


@dataclass(frozen=True)
class Event:
    code: str
    state: str
    detail: str = ""


@dataclass
class Job:
    job_id: str
    operation: str
    node_id: str
    state: JobState = JobState.QUEUED
    reason: str = ""
    events: list[Event] = field(default_factory=list)

    def transition(self, state: JobState, code: str, detail: str = "") -> None:
        self.state = state
        self.events.append(Event(code=code, state=state.value, detail=detail))
