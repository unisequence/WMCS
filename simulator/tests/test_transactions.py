# SPDX-License-Identifier: Apache-2.0

import unittest

from wmcs_sim import Agent, Controller, FailurePoint, Identity, JobState, NodeState
from wmcs_sim.model import Change, InvalidState, Plan, TransactionPurpose


HOME_WLAN = {"ssid": "Test Home", "encryption": "sae-mixed", "key": "test-only-secret"}


class TransactionTests(unittest.TestCase):
    def new_pair(self, initial_config=None):
        controller = Controller(Identity.from_seed("controller-tx"))
        agent = Agent(
            Identity.from_seed("agent-tx"),
            initial_config or {"network.lan.proto": "static", "user.note": "preserve"},
        )
        controller.start_discovery(now=0, duration_seconds=60)
        controller.observe(agent, now=1)
        controller.start_pairing(now=2, duration_seconds=60)
        return controller, agent

    def adopt(self, controller, agent, failure=None):
        return controller.adopt(
            agent,
            now=3,
            confirmation=agent.identity.confirmation,
            home_wlan=HOME_WLAN,
            failure=failure,
        )

    def test_failures_restore_original_configuration(self) -> None:
        points = (
            FailurePoint.PREPARE,
            FailurePoint.APPLY,
            FailurePoint.VERIFY,
            FailurePoint.COMMIT,
            FailurePoint.REBOOT_AFTER_PREPARE,
            FailurePoint.REBOOT_AFTER_APPLY,
        )
        for point in points:
            with self.subTest(point=point.value):
                controller, agent = self.new_pair()
                before = dict(agent.config)
                job = self.adopt(controller, agent, failure=point)

                self.assertIn(job.state, {JobState.REJECTED, JobState.ROLLED_BACK})
                self.assertEqual(agent.config, before)
                self.assertEqual(agent.managed_fields, set())
                self.assertNotIn(agent.identity.node_id, controller.members)
                self.assertIsNone(agent.journal)
                self.assertIsNone(agent.controller_fingerprint)

    def test_rollback_failure_is_not_reported_as_success(self) -> None:
        controller, agent = self.new_pair()
        job = self.adopt(controller, agent, failure=FailurePoint.ROLLBACK)

        self.assertEqual(job.state, JobState.ROLLBACK_FAILED)
        self.assertEqual(agent.state, NodeState.ROLLBACK_FAILED)
        self.assertIsNotNone(agent.journal)
        self.assertNotIn(agent.identity.node_id, controller.members)

    def test_foreign_field_collision_cannot_be_claimed(self) -> None:
        controller, agent = self.new_pair(
            {"wireless.wmcs_home.ssid": "foreign", "user.note": "preserve"}
        )
        job = self.adopt(controller, agent)

        self.assertEqual(job.state, JobState.REJECTED)
        self.assertEqual(job.reason, "ownership_conflict")
        self.assertEqual(agent.config["wireless.wmcs_home.ssid"], "foreign")
        self.assertEqual(agent.state, NodeState.FAILED)
        self.assertIsNone(agent.controller_fingerprint)

    def test_generation_must_be_monotonic(self) -> None:
        _controller, agent = self.new_pair()
        agent.state = NodeState.PAIRING
        plan = Plan(
            transaction_id="tx-manual",
            generation=2,
            purpose=TransactionPurpose.ADOPT,
            changes=(Change("wireless.wmcs_test", "1"),),
            claim_fields=frozenset({"wireless.wmcs_test"}),
        )

        with self.assertRaises(InvalidState):
            agent.prepare(plan)

    def test_release_deletes_only_managed_fields(self) -> None:
        controller, agent = self.new_pair()
        adopt_job = self.adopt(controller, agent)
        self.assertEqual(adopt_job.state, JobState.COMMITTED)
        managed = set(agent.managed_fields)

        release_job = controller.release(agent)

        self.assertEqual(release_job.state, JobState.COMMITTED)
        self.assertEqual(agent.state, NodeState.RELEASED)
        self.assertEqual(agent.managed_fields, set())
        self.assertEqual(agent.config["network.lan.proto"], "static")
        self.assertEqual(agent.config["user.note"], "preserve")
        self.assertTrue(managed.isdisjoint(agent.config))
        self.assertNotIn(agent.identity.node_id, controller.members)

    def test_failed_release_restores_member(self) -> None:
        controller, agent = self.new_pair()
        self.assertEqual(self.adopt(controller, agent).state, JobState.COMMITTED)
        before = dict(agent.config)
        managed = set(agent.managed_fields)

        release_job = controller.release(agent, failure=FailurePoint.VERIFY)

        self.assertEqual(release_job.state, JobState.ROLLED_BACK)
        self.assertEqual(agent.state, NodeState.ONLINE)
        self.assertEqual(agent.config, before)
        self.assertEqual(agent.managed_fields, managed)
        self.assertIs(controller.members[agent.identity.node_id], agent)


if __name__ == "__main__":
    unittest.main()
