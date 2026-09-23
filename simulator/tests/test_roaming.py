# SPDX-License-Identifier: Apache-2.0

import unittest

from wmcs_sim import InvalidState, ValidationError
from wmcs_sim.roaming import (
    BssObservation,
    RoamAction,
    RoamPolicy,
    RoamReason,
    RoamSession,
    RoamState,
    RoamingCore,
    StationCapabilities,
)


SOURCE_BSSID = "02:00:00:00:00:01"
TARGET_BSSID = "02:00:00:00:00:02"
OTHER_BSSID = "02:00:00:00:00:03"
PROFILE = "home-5g"


class RoamingTests(unittest.TestCase):
    def setUp(self) -> None:
        self.policy = RoamPolicy(
            source_trigger_dbm=-70,
            improvement_margin_db=11,
            observation_max_age_ms=4_000,
            confirmation_ms=1_500,
            minimum_dwell_ms=20_000,
            cooldown_ms=7_000,
            transition_timeout_ms=3_000,
            max_attempts=2,
        )
        self.core = RoamingCore(self.policy)
        self.session = RoamSession(
            station_key="station-a",
            profile_id=PROFILE,
            current_bssid=SOURCE_BSSID,
            associated_at_ms=0,
        )
        self.capabilities = StationCapabilities(
            btm=True,
            neighbor_report=True,
            beacon_report=True,
        )

    @staticmethod
    def observation(
        bssid: str,
        signal: int,
        now_ms: int,
        profile: str = PROFILE,
        **kwargs,
    ) -> BssObservation:
        return BssObservation(
            node_id=f"node-{bssid[-1]}",
            bssid=bssid,
            profile_id=profile,
            signal_dbm=signal,
            observed_at_ms=now_ms,
            **kwargs,
        )

    def evaluate_good_target(self, now_ms: int):
        source = self.observation(SOURCE_BSSID, -82, now_ms)
        target = self.observation(TARGET_BSSID, -55, now_ms)
        return self.core.evaluate(
            self.session,
            source,
            (target,),
            self.capabilities,
            now_ms,
        )

    def start_transition(self, now_ms: int = 21_000):
        first = self.evaluate_good_target(now_ms)
        self.assertEqual(first.reason, RoamReason.CANDIDATE_CONFIRMING)
        decision = self.evaluate_good_target(now_ms + self.policy.confirmation_ms)
        self.assertEqual(decision.action, RoamAction.BTM_REQUEST)
        return decision

    def test_policy_values_are_strictly_validated(self) -> None:
        with self.assertRaises(ValidationError):
            RoamPolicy(
                source_trigger_dbm=-70,
                improvement_margin_db=0,
                observation_max_age_ms=4_000,
                confirmation_ms=1_500,
                minimum_dwell_ms=20_000,
                cooldown_ms=7_000,
                transition_timeout_ms=3_000,
                max_attempts=2,
            )

    def test_same_profile_fresh_target_is_required(self) -> None:
        now_ms = 21_000
        source = self.observation(SOURCE_BSSID, -82, now_ms)
        wrong_profile = self.observation(TARGET_BSSID, -50, now_ms, profile="guest")
        decision = self.core.evaluate(
            self.session,
            source,
            (wrong_profile,),
            self.capabilities,
            now_ms,
        )
        self.assertEqual(decision.reason, RoamReason.NO_ELIGIBLE_TARGET)

        stale = self.observation(TARGET_BSSID, -50, now_ms - 4_001)
        decision = self.core.evaluate(
            self.session,
            source,
            (stale,),
            self.capabilities,
            now_ms,
        )
        self.assertEqual(decision.reason, RoamReason.NO_ELIGIBLE_TARGET)

    def test_hysteresis_and_confirmation_produce_one_btm_request(self) -> None:
        decision = self.start_transition()

        self.assertEqual(decision.target_bssid, TARGET_BSSID)
        self.assertEqual(decision.generation, 1)
        self.assertEqual(self.session.state, RoamState.BTM_PENDING)
        self.assertEqual(self.session.attempts, 1)

        pending = self.evaluate_good_target(22_600)
        self.assertEqual(pending.action, RoamAction.NONE)
        self.assertEqual(pending.reason, RoamReason.BTM_PENDING)
        self.assertEqual(self.session.attempts, 1)

    def test_client_without_btm_is_never_forced(self) -> None:
        now_ms = 21_000
        source = self.observation(SOURCE_BSSID, -82, now_ms)
        target = self.observation(TARGET_BSSID, -55, now_ms)
        no_btm = StationCapabilities(btm=False, neighbor_report=True)

        self.core.evaluate(self.session, source, (target,), no_btm, now_ms)
        decision = self.core.evaluate(
            self.session,
            self.observation(SOURCE_BSSID, -82, now_ms + 1_500),
            (self.observation(TARGET_BSSID, -55, now_ms + 1_500),),
            no_btm,
            now_ms + 1_500,
        )

        self.assertEqual(decision.action, RoamAction.NONE)
        self.assertEqual(decision.reason, RoamReason.CLIENT_WITHOUT_BTM)
        self.assertEqual(self.session.state, RoamState.BLOCKED)
        self.assertEqual(self.session.attempts, 0)

    def test_rejected_response_enters_cooldown(self) -> None:
        decision = self.start_transition()
        rejected = self.core.record_btm_response(
            self.session,
            generation=decision.generation,
            accepted=False,
            now_ms=22_600,
        )
        self.assertEqual(rejected.reason, RoamReason.BTM_REJECTED)
        self.assertEqual(self.session.state, RoamState.COOLDOWN)

        during = self.evaluate_good_target(22_700)
        self.assertEqual(during.reason, RoamReason.COOLDOWN)
        self.assertEqual(during.action, RoamAction.NONE)

    def test_stale_response_generation_is_rejected(self) -> None:
        decision = self.start_transition()
        with self.assertRaises(InvalidState):
            self.core.record_btm_response(
                self.session,
                generation=decision.generation + 1,
                accepted=True,
                now_ms=22_600,
            )
        self.assertEqual(self.session.state, RoamState.BTM_PENDING)

    def test_timeout_and_target_loss_are_non_disruptive(self) -> None:
        decision = self.start_transition()
        source = self.observation(SOURCE_BSSID, -82, 25_500)

        timed_out = self.core.evaluate(
            self.session,
            source,
            (self.observation(TARGET_BSSID, -55, 25_500),),
            self.capabilities,
            25_500,
        )
        self.assertEqual(timed_out.reason, RoamReason.TRANSITION_TIMEOUT)
        self.assertEqual(timed_out.target_bssid, decision.target_bssid)
        self.assertEqual(self.session.current_bssid, SOURCE_BSSID)

        second_session = RoamSession(
            station_key="station-b",
            profile_id=PROFILE,
            current_bssid=SOURCE_BSSID,
            associated_at_ms=0,
        )
        self.session = second_session
        self.start_transition(now_ms=30_000)
        lost = self.core.evaluate(
            self.session,
            self.observation(SOURCE_BSSID, -82, 31_600),
            (),
            self.capabilities,
            31_600,
        )
        self.assertEqual(lost.reason, RoamReason.TARGET_UNAVAILABLE)
        self.assertEqual(self.session.current_bssid, SOURCE_BSSID)

    def test_success_updates_source_and_prevents_ping_pong(self) -> None:
        decision = self.start_transition()
        observed = self.core.record_association(
            self.session,
            bssid=TARGET_BSSID,
            profile_id=PROFILE,
            now_ms=22_700,
            generation=decision.generation,
        )
        self.assertEqual(observed.reason, RoamReason.TRANSITION_OBSERVED)
        self.assertEqual(self.session.current_bssid, TARGET_BSSID)
        self.assertEqual(self.session.attempts, 0)

        after_cooldown = 30_000
        reverse_source = self.observation(TARGET_BSSID, -82, after_cooldown)
        old_ap = self.observation(SOURCE_BSSID, -50, after_cooldown)
        blocked = self.core.evaluate(
            self.session,
            reverse_source,
            (old_ap,),
            self.capabilities,
            after_cooldown,
        )
        self.assertEqual(blocked.reason, RoamReason.MINIMUM_DWELL)
        self.assertEqual(blocked.action, RoamAction.NONE)

    def test_retry_budget_blocks_after_repeated_failures(self) -> None:
        first = self.start_transition()
        self.core.record_btm_response(
            self.session,
            generation=first.generation,
            accepted=False,
            now_ms=22_600,
        )

        retry_start = 30_000
        self.evaluate_good_target(retry_start)
        second = self.evaluate_good_target(retry_start + self.policy.confirmation_ms)
        self.assertEqual(second.action, RoamAction.BTM_REQUEST)
        self.core.record_btm_response(
            self.session,
            generation=second.generation,
            accepted=False,
            now_ms=31_600,
        )

        after_cooldown = 39_000
        blocked = self.evaluate_good_target(after_cooldown)
        self.assertEqual(blocked.reason, RoamReason.RETRY_BUDGET_EXHAUSTED)
        self.assertEqual(self.session.state, RoamState.BLOCKED)


if __name__ == "__main__":
    unittest.main()
