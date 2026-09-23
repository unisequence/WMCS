# SPDX-License-Identifier: Apache-2.0

import unittest

from wmcs_sim import Agent, Controller, Identity, InvalidState, JobState, NodeState
from wmcs_sim.model import ReplayError, ValidationError


HOME_WLAN = {"ssid": "Test Home", "encryption": "sae-mixed", "key": "test-only-secret"}


class PairingTests(unittest.TestCase):
    def setUp(self) -> None:
        self.controller = Controller(Identity.from_seed("controller-a"))
        self.agent = Agent(Identity.from_seed("agent-a"), {"user.note": "keep"})

    def discover(self) -> None:
        self.controller.start_discovery(now=100, duration_seconds=30)
        self.controller.observe(self.agent, now=101)

    def test_discovery_requires_open_window(self) -> None:
        with self.assertRaises(InvalidState):
            self.controller.observe(self.agent, now=0)

    def test_discovery_deadline_is_exclusive(self) -> None:
        self.controller.start_discovery(now=10, duration_seconds=5)
        with self.assertRaises(InvalidState):
            self.controller.observe(self.agent, now=15)

    def test_window_duration_is_bounded(self) -> None:
        for duration in (0, 4, 301):
            with self.subTest(duration=duration), self.assertRaises(ValidationError):
                self.controller.start_pairing(now=0, duration_seconds=duration)

    def test_confirmation_mismatch_does_not_mutate_agent(self) -> None:
        self.discover()
        self.controller.start_pairing(now=102, duration_seconds=30)

        job = self.controller.adopt(
            self.agent,
            now=103,
            confirmation="0000-0000-0000",
            home_wlan=HOME_WLAN,
        )

        self.assertEqual(job.state, JobState.REJECTED)
        self.assertEqual(job.reason, "confirmation_mismatch")
        self.assertEqual(self.agent.state, NodeState.DISCOVERED)
        self.assertEqual(self.agent.config, {"user.note": "keep"})
        self.assertIsNone(self.agent.controller_fingerprint)

    def test_stale_candidate_cannot_be_adopted(self) -> None:
        self.discover()
        self.controller.start_pairing(now=500, duration_seconds=30)

        job = self.controller.adopt(
            self.agent,
            now=501,
            confirmation=self.agent.identity.confirmation,
            home_wlan=HOME_WLAN,
        )

        self.assertEqual(job.state, JobState.REJECTED)
        self.assertEqual(job.reason, "candidate_observation_expired")
        self.assertEqual(self.agent.config, {"user.note": "keep"})

    def test_home_wlan_fields_are_strictly_validated(self) -> None:
        invalid_profiles = (
            {"ssid": "x" * 33, "encryption": "sae", "key": "12345678"},
            {"ssid": "Home", "encryption": "open", "key": "12345678"},
            {"ssid": "Home", "encryption": "sae", "key": "short"},
        )
        for profile in invalid_profiles:
            with self.subTest(profile=profile):
                controller = Controller(Identity.from_seed("controller-validation"))
                agent = Agent(Identity.from_seed("agent-validation"))
                controller.start_discovery(now=0, duration_seconds=60)
                controller.observe(agent, now=1)
                controller.start_pairing(now=2, duration_seconds=60)
                job = controller.adopt(
                    agent,
                    now=3,
                    confirmation=agent.identity.confirmation,
                    home_wlan=profile,
                )
                self.assertEqual(job.state, JobState.REJECTED)
                self.assertEqual(job.reason, "invalid_request")
                self.assertEqual(agent.config, {})

    def test_successful_adoption_binds_identity_and_advances_generation(self) -> None:
        self.discover()
        self.controller.start_pairing(now=102, duration_seconds=30)

        job = self.controller.adopt(
            self.agent,
            now=103,
            confirmation=self.agent.identity.confirmation,
            home_wlan=HOME_WLAN,
        )

        self.assertEqual(job.state, JobState.COMMITTED)
        self.assertEqual(self.agent.state, NodeState.ONLINE)
        self.assertEqual(self.agent.generation, 1)
        self.assertEqual(
            self.agent.controller_fingerprint,
            self.controller.identity.fingerprint,
        )
        self.assertIs(self.controller.members[self.agent.identity.node_id], self.agent)
        self.assertEqual(self.agent.config["user.note"], "keep")
        self.assertIsNone(self.agent.journal)

    def test_control_sequence_rejects_replay(self) -> None:
        self.agent.state = NodeState.DISCOVERED
        self.agent.begin_adoption(self.controller.identity.fingerprint)
        self.agent.accept_control_message(self.controller.identity.fingerprint, 7)

        with self.assertRaises(ReplayError):
            self.agent.accept_control_message(self.controller.identity.fingerprint, 7)
        with self.assertRaises(ReplayError):
            self.agent.accept_control_message(self.controller.identity.fingerprint, 6)


if __name__ == "__main__":
    unittest.main()
