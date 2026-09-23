# SPDX-License-Identifier: Apache-2.0

import json

from .engine import Agent, Controller
from .model import Identity


def main() -> None:
    controller = Controller(Identity.from_seed("demo-controller"))
    agent = Agent(
        Identity.from_seed("demo-agent"),
        initial_config={"network.lan.proto": "static", "user.note": "preserve-me"},
    )

    controller.start_discovery(now=0, duration_seconds=60)
    controller.observe(agent, now=1)
    controller.start_pairing(now=2, duration_seconds=120)
    job = controller.adopt(
        agent,
        now=3,
        confirmation=agent.identity.confirmation,
        home_wlan={"ssid": "WMCS Demo", "encryption": "sae-mixed", "key": "demo-secret"},
    )

    result = {
        "job_id": job.job_id,
        "job_state": job.state.value,
        "node_id": agent.identity.node_id,
        "node_state": agent.state.value,
        "generation": agent.generation,
        "managed_fields": sorted(agent.managed_fields),
        "foreign_fields_preserved": agent.config.get("user.note") == "preserve-me",
        "events": [
            {"code": event.code, "state": event.state, "detail": event.detail}
            for event in job.events
        ],
    }
    print(json.dumps(result, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
