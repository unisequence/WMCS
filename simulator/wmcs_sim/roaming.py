# SPDX-License-Identifier: Apache-2.0

"""Deterministic roaming-policy model derived from the public WMCS spec."""

from __future__ import annotations

import re
from dataclasses import dataclass
from enum import Enum
from typing import Iterable, Optional

from .model import IDENTIFIER_RE, InvalidState, ValidationError


BSSID_RE = re.compile(r"^[0-9a-f]{2}(?::[0-9a-f]{2}){5}$")
MAX_CANDIDATES = 64


class RoamState(str, Enum):
    ASSOCIATED = "associated"
    MEASURING = "measuring"
    BTM_PENDING = "btm_pending"
    COOLDOWN = "cooldown"
    BLOCKED = "blocked"


class RoamAction(str, Enum):
    NONE = "none"
    BTM_REQUEST = "btm_request"


class RoamReason(str, Enum):
    SOURCE_HEALTHY = "source_healthy"
    SOURCE_OBSERVATION_STALE = "source_observation_stale"
    NO_ELIGIBLE_TARGET = "no_eligible_target"
    INSUFFICIENT_IMPROVEMENT = "insufficient_improvement"
    MINIMUM_DWELL = "minimum_dwell"
    CANDIDATE_CONFIRMING = "candidate_confirming"
    BTM_REQUESTED = "btm_requested"
    BTM_PENDING = "btm_pending"
    BTM_ACCEPTED = "btm_accepted"
    BTM_REJECTED = "btm_rejected"
    TRANSITION_OBSERVED = "transition_observed"
    DIFFERENT_TARGET_OBSERVED = "different_target_observed"
    CLIENT_ROAM_OBSERVED = "client_roam_observed"
    TRANSITION_TIMEOUT = "transition_timeout"
    TARGET_UNAVAILABLE = "target_unavailable"
    COOLDOWN = "cooldown"
    CLIENT_WITHOUT_BTM = "client_without_btm"
    RETRY_BUDGET_EXHAUSTED = "retry_budget_exhausted"


@dataclass(frozen=True)
class StationCapabilities:
    btm: bool
    neighbor_report: bool = False
    beacon_report: bool = False
    ft: bool = False

    def __post_init__(self) -> None:
        for name in ("btm", "neighbor_report", "beacon_report", "ft"):
            if type(getattr(self, name)) is not bool:
                raise ValidationError(f"station capability {name} must be boolean")


@dataclass(frozen=True)
class RoamPolicy:
    source_trigger_dbm: int
    improvement_margin_db: int
    observation_max_age_ms: int
    confirmation_ms: int
    minimum_dwell_ms: int
    cooldown_ms: int
    transition_timeout_ms: int
    max_attempts: int

    def __post_init__(self) -> None:
        integer_fields = (
            "source_trigger_dbm",
            "improvement_margin_db",
            "observation_max_age_ms",
            "confirmation_ms",
            "minimum_dwell_ms",
            "cooldown_ms",
            "transition_timeout_ms",
            "max_attempts",
        )
        for name in integer_fields:
            if type(getattr(self, name)) is not int:
                raise ValidationError(f"roaming policy {name} must be an integer")

        if not -127 <= self.source_trigger_dbm <= -1:
            raise ValidationError("source trigger must be between -127 and -1 dBm")
        if not 1 <= self.improvement_margin_db <= 100:
            raise ValidationError("improvement margin must be between 1 and 100 dB")
        if self.observation_max_age_ms < 1:
            raise ValidationError("observation age must be positive")
        if self.confirmation_ms < 1:
            raise ValidationError("confirmation window must be positive")
        if self.minimum_dwell_ms < 0:
            raise ValidationError("minimum dwell cannot be negative")
        if self.cooldown_ms < 1:
            raise ValidationError("cooldown must be positive")
        if self.transition_timeout_ms < 1:
            raise ValidationError("transition timeout must be positive")
        if not 1 <= self.max_attempts <= 16:
            raise ValidationError("max attempts must be between 1 and 16")


@dataclass(frozen=True)
class BssObservation:
    node_id: str
    bssid: str
    profile_id: str
    signal_dbm: int
    observed_at_ms: int
    healthy: bool = True
    reachable: bool = True
    security_compatible: bool = True

    def __post_init__(self) -> None:
        if not IDENTIFIER_RE.fullmatch(self.node_id):
            raise ValidationError("invalid observation node identifier")
        if not IDENTIFIER_RE.fullmatch(self.profile_id):
            raise ValidationError("invalid ESS profile identifier")
        if not BSSID_RE.fullmatch(self.bssid):
            raise ValidationError("BSSID must use canonical lowercase notation")
        if type(self.signal_dbm) is not int or not -127 <= self.signal_dbm <= 0:
            raise ValidationError("signal must be an integer between -127 and 0 dBm")
        if type(self.observed_at_ms) is not int or self.observed_at_ms < 0:
            raise ValidationError("observation time must be a non-negative integer")
        for name in ("healthy", "reachable", "security_compatible"):
            if type(getattr(self, name)) is not bool:
                raise ValidationError(f"observation field {name} must be boolean")


@dataclass(frozen=True)
class RoamDecision:
    action: RoamAction
    reason: RoamReason
    target_bssid: Optional[str] = None
    generation: int = 0


@dataclass
class RoamSession:
    station_key: str
    profile_id: str
    current_bssid: str
    associated_at_ms: int
    state: RoamState = RoamState.ASSOCIATED
    candidate_bssid: Optional[str] = None
    candidate_since_ms: Optional[int] = None
    pending_since_ms: Optional[int] = None
    cooldown_until_ms: Optional[int] = None
    generation: int = 0
    attempts: int = 0
    last_reason: Optional[RoamReason] = None

    def __post_init__(self) -> None:
        if not IDENTIFIER_RE.fullmatch(self.station_key):
            raise ValidationError("invalid redacted station key")
        if not IDENTIFIER_RE.fullmatch(self.profile_id):
            raise ValidationError("invalid ESS profile identifier")
        if not BSSID_RE.fullmatch(self.current_bssid):
            raise ValidationError("current BSSID must use canonical lowercase notation")
        if type(self.associated_at_ms) is not int or self.associated_at_ms < 0:
            raise ValidationError("association time must be a non-negative integer")


class RoamingCore:
    """Pure policy/state model; it never invokes hostapd or mutates OpenWrt."""

    def __init__(self, policy: RoamPolicy):
        self.policy = policy

    def evaluate(
        self,
        session: RoamSession,
        source: BssObservation,
        candidates: Iterable[BssObservation],
        capabilities: StationCapabilities,
        now_ms: int,
    ) -> RoamDecision:
        self._validate_now(now_ms)
        self._validate_source(session, source)
        candidate_list = tuple(candidates)
        if len(candidate_list) > MAX_CANDIDATES:
            raise ValidationError(f"candidate list exceeds {MAX_CANDIDATES} entries")

        if source.observed_at_ms > now_ms:
            raise ValidationError("source observation is from the future")
        if now_ms - source.observed_at_ms > self.policy.observation_max_age_ms:
            self._reset_candidate(session)
            return self._decision(session, RoamReason.SOURCE_OBSERVATION_STALE)

        eligible = self._eligible_targets(session, candidate_list, now_ms)

        if session.state is RoamState.BTM_PENDING:
            target = next(
                (item for item in eligible if item.bssid == session.candidate_bssid),
                None,
            )
            if target is None:
                target_bssid = session.candidate_bssid
                self._enter_cooldown(session, now_ms, RoamReason.TARGET_UNAVAILABLE)
                return self._decision(
                    session,
                    RoamReason.TARGET_UNAVAILABLE,
                    target_bssid=target_bssid,
                )
            assert session.pending_since_ms is not None
            if now_ms - session.pending_since_ms >= self.policy.transition_timeout_ms:
                target_bssid = session.candidate_bssid
                self._enter_cooldown(session, now_ms, RoamReason.TRANSITION_TIMEOUT)
                return self._decision(
                    session,
                    RoamReason.TRANSITION_TIMEOUT,
                    target_bssid=target_bssid,
                )
            return self._decision(
                session,
                RoamReason.BTM_PENDING,
                target_bssid=session.candidate_bssid,
            )

        if session.state is RoamState.BLOCKED:
            return self._decision(session, session.last_reason or RoamReason.RETRY_BUDGET_EXHAUSTED)

        if session.state is RoamState.COOLDOWN:
            assert session.cooldown_until_ms is not None
            if now_ms < session.cooldown_until_ms:
                return self._decision(session, RoamReason.COOLDOWN)
            session.cooldown_until_ms = None
            if session.attempts >= self.policy.max_attempts:
                session.state = RoamState.BLOCKED
                session.last_reason = RoamReason.RETRY_BUDGET_EXHAUSTED
                return self._decision(session, RoamReason.RETRY_BUDGET_EXHAUSTED)
            session.state = RoamState.ASSOCIATED

        if now_ms - session.associated_at_ms < self.policy.minimum_dwell_ms:
            self._reset_candidate(session)
            return self._decision(session, RoamReason.MINIMUM_DWELL)

        if source.signal_dbm > self.policy.source_trigger_dbm:
            self._reset_candidate(session)
            return self._decision(session, RoamReason.SOURCE_HEALTHY)

        if not eligible:
            self._reset_candidate(session)
            return self._decision(session, RoamReason.NO_ELIGIBLE_TARGET)

        best = max(eligible, key=lambda item: (item.signal_dbm, item.bssid))
        if best.signal_dbm - source.signal_dbm < self.policy.improvement_margin_db:
            self._reset_candidate(session)
            return self._decision(
                session,
                RoamReason.INSUFFICIENT_IMPROVEMENT,
                target_bssid=best.bssid,
            )

        if session.candidate_bssid != best.bssid or session.candidate_since_ms is None:
            session.state = RoamState.MEASURING
            session.candidate_bssid = best.bssid
            session.candidate_since_ms = now_ms
            session.last_reason = RoamReason.CANDIDATE_CONFIRMING
            return self._decision(
                session,
                RoamReason.CANDIDATE_CONFIRMING,
                target_bssid=best.bssid,
            )

        if now_ms - session.candidate_since_ms < self.policy.confirmation_ms:
            return self._decision(
                session,
                RoamReason.CANDIDATE_CONFIRMING,
                target_bssid=best.bssid,
            )

        if not capabilities.btm:
            session.state = RoamState.BLOCKED
            session.last_reason = RoamReason.CLIENT_WITHOUT_BTM
            return self._decision(
                session,
                RoamReason.CLIENT_WITHOUT_BTM,
                target_bssid=best.bssid,
            )

        if session.attempts >= self.policy.max_attempts:
            session.state = RoamState.BLOCKED
            session.last_reason = RoamReason.RETRY_BUDGET_EXHAUSTED
            return self._decision(
                session,
                RoamReason.RETRY_BUDGET_EXHAUSTED,
                target_bssid=best.bssid,
            )

        session.state = RoamState.BTM_PENDING
        session.pending_since_ms = now_ms
        session.generation += 1
        session.attempts += 1
        session.last_reason = RoamReason.BTM_REQUESTED
        return RoamDecision(
            action=RoamAction.BTM_REQUEST,
            reason=RoamReason.BTM_REQUESTED,
            target_bssid=best.bssid,
            generation=session.generation,
        )

    def record_btm_response(
        self,
        session: RoamSession,
        generation: int,
        accepted: bool,
        now_ms: int,
    ) -> RoamDecision:
        self._validate_now(now_ms)
        if type(accepted) is not bool:
            raise ValidationError("BTM response acceptance must be boolean")
        self._require_pending_generation(session, generation)
        target_bssid = session.candidate_bssid

        if accepted:
            session.last_reason = RoamReason.BTM_ACCEPTED
            return self._decision(
                session,
                RoamReason.BTM_ACCEPTED,
                target_bssid=target_bssid,
            )

        self._enter_cooldown(session, now_ms, RoamReason.BTM_REJECTED)
        return self._decision(
            session,
            RoamReason.BTM_REJECTED,
            target_bssid=target_bssid,
        )

    def record_association(
        self,
        session: RoamSession,
        bssid: str,
        profile_id: str,
        now_ms: int,
        generation: Optional[int] = None,
    ) -> RoamDecision:
        self._validate_now(now_ms)
        if not BSSID_RE.fullmatch(bssid):
            raise ValidationError("associated BSSID must use canonical lowercase notation")
        if profile_id != session.profile_id:
            raise ValidationError("association does not belong to the session ESS profile")

        if session.state is RoamState.BTM_PENDING:
            if generation is None:
                raise ValidationError("pending transition requires a decision generation")
            self._require_pending_generation(session, generation)
            intended = bssid == session.candidate_bssid
            reason = (
                RoamReason.TRANSITION_OBSERVED
                if intended
                else RoamReason.DIFFERENT_TARGET_OBSERVED
            )
        else:
            if generation is not None:
                raise InvalidState("unexpected decision generation without a pending transition")
            reason = RoamReason.CLIENT_ROAM_OBSERVED

        session.current_bssid = bssid
        session.associated_at_ms = now_ms
        session.attempts = 0
        self._enter_cooldown(session, now_ms, reason)
        return self._decision(session, reason, target_bssid=bssid)

    def _eligible_targets(
        self,
        session: RoamSession,
        candidates: tuple[BssObservation, ...],
        now_ms: int,
    ) -> tuple[BssObservation, ...]:
        eligible: list[BssObservation] = []
        seen: set[str] = set()
        for item in candidates:
            if item.observed_at_ms > now_ms:
                raise ValidationError("candidate observation is from the future")
            if item.bssid in seen:
                raise ValidationError("candidate BSSID appears more than once")
            seen.add(item.bssid)
            if item.bssid == session.current_bssid:
                continue
            if item.profile_id != session.profile_id:
                continue
            if now_ms - item.observed_at_ms > self.policy.observation_max_age_ms:
                continue
            if not item.healthy or not item.reachable or not item.security_compatible:
                continue
            eligible.append(item)
        return tuple(eligible)

    @staticmethod
    def _validate_now(now_ms: int) -> None:
        if type(now_ms) is not int or now_ms < 0:
            raise ValidationError("current time must be a non-negative integer")

    @staticmethod
    def _validate_source(session: RoamSession, source: BssObservation) -> None:
        if source.bssid != session.current_bssid:
            raise ValidationError("source observation does not match the current BSSID")
        if source.profile_id != session.profile_id:
            raise ValidationError("source observation does not match the ESS profile")

    @staticmethod
    def _require_pending_generation(session: RoamSession, generation: int) -> None:
        if session.state is not RoamState.BTM_PENDING:
            raise InvalidState("no BTM transition is pending")
        if type(generation) is not int or generation != session.generation:
            raise InvalidState("BTM response generation does not match the pending decision")

    @staticmethod
    def _reset_candidate(session: RoamSession) -> None:
        if session.state is RoamState.MEASURING:
            session.state = RoamState.ASSOCIATED
        session.candidate_bssid = None
        session.candidate_since_ms = None
        session.pending_since_ms = None

    def _enter_cooldown(
        self,
        session: RoamSession,
        now_ms: int,
        reason: RoamReason,
    ) -> None:
        session.state = RoamState.COOLDOWN
        session.cooldown_until_ms = now_ms + self.policy.cooldown_ms
        session.candidate_bssid = None
        session.candidate_since_ms = None
        session.pending_since_ms = None
        session.last_reason = reason

    @staticmethod
    def _decision(
        session: RoamSession,
        reason: RoamReason,
        target_bssid: Optional[str] = None,
    ) -> RoamDecision:
        session.last_reason = reason
        return RoamDecision(
            action=RoamAction.NONE,
            reason=reason,
            target_bssid=target_bssid,
            generation=session.generation,
        )
