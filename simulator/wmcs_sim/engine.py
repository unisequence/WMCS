# SPDX-License-Identifier: Apache-2.0

"""Deterministic controller, agent, and transaction state machines."""

from __future__ import annotations

from copy import deepcopy
from dataclasses import dataclass
from enum import Enum
from typing import Optional

from .model import (
    Change,
    Identity,
    InvalidState,
    Job,
    JobState,
    Journal,
    NodeState,
    OwnershipError,
    Plan,
    ReplayError,
    TransactionError,
    TransactionPurpose,
    TransactionStage,
    ValidationError,
)


MIN_WINDOW_SECONDS = 5
MAX_DISCOVERY_SECONDS = 300
MAX_PAIRING_SECONDS = 300
MAX_CANDIDATE_AGE_SECONDS = 300
ALLOWED_ENCRYPTION = frozenset({"psk2", "sae", "sae-mixed"})


class FailurePoint(str, Enum):
    PREPARE = "prepare"
    APPLY = "apply"
    VERIFY = "verify"
    COMMIT = "commit"
    ROLLBACK = "rollback"
    REBOOT_AFTER_PREPARE = "reboot_after_prepare"
    REBOOT_AFTER_APPLY = "reboot_after_apply"


@dataclass(frozen=True)
class Candidate:
    node_id: str
    fingerprint: str
    observed_at: int


class Agent:
    """In-memory model of durable agent state and owned configuration."""

    def __init__(self, identity: Identity, initial_config: Optional[dict[str, str]] = None):
        self.identity = identity
        self.state = NodeState.UNKNOWN
        self.config = dict(initial_config or {})
        self.managed_fields: set[str] = set()
        self.controller_fingerprint: Optional[str] = None
        self.generation = 0
        self.last_sequence = -1
        self.journal: Optional[Journal] = None
        self.last_transaction_stage: Optional[TransactionStage] = None

    def begin_adoption(self, controller_fingerprint: str) -> None:
        if self.state not in {NodeState.DISCOVERED, NodeState.FAILED, NodeState.RELEASED}:
            raise InvalidState(f"cannot begin adoption from {self.state.value}")
        self.state = NodeState.PAIRING
        self.controller_fingerprint = controller_fingerprint
        self.last_sequence = -1

    def accept_control_message(self, controller_fingerprint: str, sequence: int) -> None:
        if controller_fingerprint != self.controller_fingerprint:
            raise InvalidState("controller identity does not match the pairing session")
        if sequence < 0:
            raise ValidationError("sequence must be non-negative")
        if sequence <= self.last_sequence:
            raise ReplayError("control message sequence was already observed")
        self.last_sequence = sequence

    def abort_adoption(self) -> None:
        if self.journal is not None:
            raise InvalidState("an active transaction must be rolled back")
        self.state = NodeState.FAILED
        self.controller_fingerprint = None
        self.last_sequence = -1

    def prepare(self, plan: Plan) -> None:
        if self.journal is not None:
            raise InvalidState("another transaction is active")
        if plan.generation != self.generation + 1:
            raise InvalidState("transaction generation is not the next generation")

        if plan.purpose is TransactionPurpose.ADOPT:
            if self.state is not NodeState.PAIRING:
                raise InvalidState("adoption transaction requires pairing state")
        elif plan.purpose is TransactionPurpose.RELEASE:
            if self.state is not NodeState.ONLINE:
                raise InvalidState("release requires an online member")
        elif self.state is not NodeState.ONLINE:
            raise InvalidState("reconciliation requires an online member")

        before: dict[str, tuple[bool, Optional[str]]] = {}
        for change in plan.changes:
            if change.key in self.managed_fields:
                pass
            elif change.key in plan.claim_fields and change.key not in self.config:
                pass
            else:
                raise OwnershipError(f"field is not owned and cannot be claimed: {change.key}")
            before[change.key] = (change.key in self.config, self.config.get(change.key))

        if plan.purpose is TransactionPurpose.ADOPT:
            self.state = NodeState.ADOPTING
        elif plan.purpose is TransactionPurpose.RELEASE:
            self.state = NodeState.RELEASING
        self.journal = Journal(plan=plan, before=before)
        self.state = NodeState.CONFIGURING

    def apply(self) -> None:
        journal = self._require_stage(TransactionStage.PREPARED)
        for change in journal.plan.changes:
            if change.value is None:
                self.config.pop(change.key, None)
            else:
                self.config[change.key] = change.value
        journal.stage = TransactionStage.APPLIED
        self.state = NodeState.VERIFYING

    def verify(self, connectivity_ok: bool = True) -> None:
        journal = self._require_stage(TransactionStage.APPLIED)
        if not connectivity_ok:
            raise TransactionError("connectivity verification failed")
        for change in journal.plan.changes:
            if change.value is None:
                if change.key in self.config:
                    raise TransactionError(f"deleted field is still present: {change.key}")
            elif self.config.get(change.key) != change.value:
                raise TransactionError(f"field verification failed: {change.key}")
        journal.stage = TransactionStage.VERIFIED

    def commit(self) -> None:
        journal = self._require_stage(TransactionStage.VERIFIED)
        plan = journal.plan
        for field in plan.claim_fields:
            self.managed_fields.add(field)
        for change in plan.changes:
            if change.value is None:
                self.managed_fields.discard(change.key)

        self.generation = plan.generation
        journal.stage = TransactionStage.COMMITTED
        self.last_transaction_stage = journal.stage
        self.journal = None

        if plan.purpose is TransactionPurpose.RELEASE:
            self.state = NodeState.RELEASED
            self.controller_fingerprint = None
            self.last_sequence = -1
        else:
            self.state = NodeState.ONLINE

    def rollback(self, fail: bool = False) -> None:
        if self.journal is None:
            return
        plan = self.journal.plan
        if fail:
            self.journal.stage = TransactionStage.ROLLBACK_FAILED
            self.last_transaction_stage = self.journal.stage
            self.state = NodeState.ROLLBACK_FAILED
            return

        for key, (existed, value) in self.journal.before.items():
            if existed:
                assert value is not None
                self.config[key] = value
            else:
                self.config.pop(key, None)

        self.journal.stage = TransactionStage.ROLLED_BACK
        self.last_transaction_stage = self.journal.stage
        self.journal = None
        if plan.purpose is TransactionPurpose.ADOPT:
            self.state = NodeState.FAILED
            self.controller_fingerprint = None
            self.last_sequence = -1
        else:
            self.state = NodeState.ONLINE

    def reboot(self) -> None:
        """Model process restart with durable config and journal preserved."""

        self.config = deepcopy(self.config)
        self.managed_fields = set(self.managed_fields)
        self.journal = deepcopy(self.journal)
        if self.journal is not None:
            self.rollback()
        elif self.controller_fingerprint is not None:
            self.state = NodeState.ONLINE
        else:
            self.state = NodeState.UNKNOWN

    def _require_stage(self, stage: TransactionStage) -> Journal:
        if self.journal is None or self.journal.stage is not stage:
            actual = self.journal.stage.value if self.journal else "none"
            raise InvalidState(f"expected transaction stage {stage.value}, got {actual}")
        return self.journal


class Controller:
    """Controller model with bounded discovery/pairing windows and jobs."""

    def __init__(self, identity: Identity):
        self.identity = identity
        self.discovery_deadline: Optional[int] = None
        self.pairing_deadline: Optional[int] = None
        self.candidates: dict[str, Candidate] = {}
        self.members: dict[str, Agent] = {}
        self.jobs: dict[str, Job] = {}
        self._next_transaction = 1
        self._next_job = 1

    def start_discovery(self, now: int, duration_seconds: int) -> None:
        self._validate_window(duration_seconds, MAX_DISCOVERY_SECONDS)
        self.discovery_deadline = now + duration_seconds

    def observe(self, agent: Agent, now: int) -> Candidate:
        if self.discovery_deadline is None or now >= self.discovery_deadline:
            raise InvalidState("discovery window is closed")
        candidate = Candidate(
            node_id=agent.identity.node_id,
            fingerprint=agent.identity.fingerprint,
            observed_at=now,
        )
        self.candidates[candidate.node_id] = candidate
        agent.state = NodeState.DISCOVERED
        return candidate

    def start_pairing(self, now: int, duration_seconds: int) -> None:
        self._validate_window(duration_seconds, MAX_PAIRING_SECONDS)
        self.pairing_deadline = now + duration_seconds

    def adopt(
        self,
        agent: Agent,
        now: int,
        confirmation: str,
        home_wlan: dict[str, str],
        failure: Optional[FailurePoint] = None,
    ) -> Job:
        job = self._new_job("adopt", agent.identity.node_id)
        candidate = self.candidates.get(agent.identity.node_id)

        if self.pairing_deadline is None or now >= self.pairing_deadline:
            return self._reject(job, "pairing_window_closed")
        if candidate is None or candidate.fingerprint != agent.identity.fingerprint:
            return self._reject(job, "candidate_not_observed")
        if now < candidate.observed_at or now - candidate.observed_at > MAX_CANDIDATE_AGE_SECONDS:
            return self._reject(job, "candidate_observation_expired")
        if confirmation != agent.identity.confirmation:
            return self._reject(job, "confirmation_mismatch")

        try:
            agent.begin_adoption(self.identity.fingerprint)
            agent.accept_control_message(self.identity.fingerprint, 0)
            plan = self._adoption_plan(agent, home_wlan)
            self._run_transaction(agent, plan, job, failure)
        except (InvalidState, OwnershipError, TransactionError, ValidationError) as exc:
            self._rollback(agent, job, exc.code, failure)
            return job

        self.members[agent.identity.node_id] = agent
        return job

    def release(
        self,
        agent: Agent,
        failure: Optional[FailurePoint] = None,
    ) -> Job:
        job = self._new_job("release", agent.identity.node_id)
        if self.members.get(agent.identity.node_id) is not agent:
            return self._reject(job, "member_not_found")
        if not agent.managed_fields:
            return self._reject(job, "no_managed_state")

        changes = tuple(Change(key=key, value=None) for key in sorted(agent.managed_fields))
        plan = Plan(
            transaction_id=self._transaction_id(),
            generation=agent.generation + 1,
            purpose=TransactionPurpose.RELEASE,
            changes=changes,
        )
        try:
            self._run_transaction(agent, plan, job, failure)
        except (InvalidState, OwnershipError, TransactionError, ValidationError) as exc:
            self._rollback(agent, job, exc.code, failure)
            return job

        self.members.pop(agent.identity.node_id, None)
        return job

    def _run_transaction(
        self,
        agent: Agent,
        plan: Plan,
        job: Job,
        failure: Optional[FailurePoint],
    ) -> None:
        job.transition(JobState.PREPARING, "transaction_preparing")
        if failure is FailurePoint.PREPARE:
            raise TransactionError("injected prepare failure")
        agent.prepare(plan)
        if failure is FailurePoint.REBOOT_AFTER_PREPARE:
            agent.reboot()
            raise TransactionError("agent rebooted after prepare")

        job.transition(JobState.APPLYING, "transaction_applying")
        if failure is FailurePoint.APPLY:
            raise TransactionError("injected apply failure")
        agent.apply()
        if failure is FailurePoint.REBOOT_AFTER_APPLY:
            agent.reboot()
            raise TransactionError("agent rebooted after apply")

        job.transition(JobState.VERIFYING, "transaction_verifying")
        if failure in {FailurePoint.VERIFY, FailurePoint.ROLLBACK}:
            agent.verify(connectivity_ok=False)
        else:
            agent.verify()

        if failure is FailurePoint.COMMIT:
            raise TransactionError("injected commit failure")
        agent.commit()
        job.transition(JobState.COMMITTED, "transaction_committed")

    def _rollback(
        self,
        agent: Agent,
        job: Job,
        reason: str,
        failure: Optional[FailurePoint],
    ) -> None:
        job.reason = reason
        if agent.journal is None:
            if agent.last_transaction_stage is TransactionStage.ROLLED_BACK:
                job.transition(JobState.ROLLED_BACK, "recovered_after_reboot")
            else:
                if job.operation == "adopt" and agent.controller_fingerprint is not None:
                    agent.abort_adoption()
                job.transition(JobState.REJECTED, reason)
            return

        job.transition(JobState.ROLLING_BACK, reason)
        agent.rollback(fail=failure is FailurePoint.ROLLBACK)
        if agent.state is NodeState.ROLLBACK_FAILED:
            job.transition(JobState.ROLLBACK_FAILED, "rollback_failed")
        else:
            job.transition(JobState.ROLLED_BACK, "rollback_complete")

    def _adoption_plan(self, agent: Agent, home_wlan: dict[str, str]) -> Plan:
        allowed = {"ssid", "encryption", "key"}
        if set(home_wlan) != allowed:
            raise ValidationError("home WLAN requires exactly ssid, encryption, and key")
        if not home_wlan["ssid"] or not home_wlan["encryption"] or not home_wlan["key"]:
            raise ValidationError("home WLAN fields cannot be empty")

        ssid = home_wlan["ssid"]
        encryption = home_wlan["encryption"]
        key = home_wlan["key"]
        if len(ssid.encode("utf-8")) > 32 or any(ord(character) < 32 for character in ssid):
            raise ValidationError("SSID must contain 1..32 bytes without control characters")
        if encryption not in ALLOWED_ENCRYPTION:
            raise ValidationError("unsupported home WLAN encryption")
        key_bytes = key.encode("utf-8")
        is_hex_psk = len(key) == 64 and all(character in "0123456789abcdefABCDEF" for character in key)
        if not (8 <= len(key_bytes) <= 63 or is_hex_psk):
            raise ValidationError("WLAN key must be an 8..63 byte passphrase or a 64-digit hex PSK")

        values = {
            "wireless.wmcs_home.mode": "ap",
            "wireless.wmcs_home.ssid": home_wlan["ssid"],
            "wireless.wmcs_home.encryption": home_wlan["encryption"],
            "wireless.wmcs_home.key": home_wlan["key"],
            "wireless.wmcs_home.network": "lan",
            "wireless.wmcs_home.wmcs_managed": "1",
        }
        changes = tuple(Change(key=key, value=value) for key, value in values.items())
        return Plan(
            transaction_id=self._transaction_id(),
            generation=agent.generation + 1,
            purpose=TransactionPurpose.ADOPT,
            changes=changes,
            claim_fields=frozenset(values),
        )

    def _new_job(self, operation: str, node_id: str) -> Job:
        job_id = f"job-{self._next_job:06d}"
        self._next_job += 1
        job = Job(job_id=job_id, operation=operation, node_id=node_id)
        self.jobs[job_id] = job
        return job

    def _transaction_id(self) -> str:
        transaction_id = f"tx-{self._next_transaction:06d}"
        self._next_transaction += 1
        return transaction_id

    @staticmethod
    def _reject(job: Job, reason: str) -> Job:
        job.reason = reason
        job.transition(JobState.REJECTED, reason)
        return job

    @staticmethod
    def _validate_window(duration_seconds: int, maximum: int) -> None:
        if not isinstance(duration_seconds, int):
            raise ValidationError("window duration must be an integer")
        if duration_seconds < MIN_WINDOW_SECONDS or duration_seconds > maximum:
            raise ValidationError(
                f"window duration must be between {MIN_WINDOW_SECONDS} and {maximum} seconds"
            )
