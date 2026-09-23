# SPDX-License-Identifier: Apache-2.0

import json
import unittest

from wmcs_sim import Agent, Controller, Identity, JobState


class RedactionTests(unittest.TestCase):
    def test_job_events_do_not_contain_wlan_secret(self) -> None:
        secret = "correct-horse-test-only"
        controller = Controller(Identity.from_seed("controller-redaction"))
        agent = Agent(Identity.from_seed("agent-redaction"))
        controller.start_discovery(now=0, duration_seconds=60)
        controller.observe(agent, now=1)
        controller.start_pairing(now=2, duration_seconds=60)

        job = controller.adopt(
            agent,
            now=3,
            confirmation=agent.identity.confirmation,
            home_wlan={"ssid": "Home", "encryption": "sae", "key": secret},
        )

        self.assertEqual(job.state, JobState.COMMITTED)
        encoded = json.dumps(
            {
                "reason": job.reason,
                "events": [event.__dict__ for event in job.events],
            },
            sort_keys=True,
        )
        self.assertNotIn(secret, encoded)
        self.assertIsNone(agent.journal)


if __name__ == "__main__":
    unittest.main()
