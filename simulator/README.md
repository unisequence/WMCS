# WMCS executable model

The simulator is a transport-independent executable specification for the WMCS
controller, agent, pairing, ownership, and transaction invariants. It is not
production daemon code and its synthetic identities are not cryptography.

Run all tests:

```sh
PYTHONPATH=simulator python3 -m unittest discover -s simulator/tests -v
```

Run one successful adoption trace:

```sh
PYTHONPATH=simulator python3 -m wmcs_sim
```

The model deliberately leaves wire encoding, discovery transport, and TLS
implementation undecided. Production implementations must reproduce these
state transitions and pass shared protocol vectors rather than import Python
runtime behavior onto the router.

Current invariants include:

- bounded discovery and pairing windows;
- explicit identity confirmation;
- monotonic anti-replay sequence numbers;
- monotonic configuration generations;
- no mutation or ownership claim over existing foreign fields;
- prepare/apply/verify/commit with rollback;
- conservative rollback after restart during an active transaction;
- rollback failure reported as a distinct terminal condition;
- release deletes only WMCS-managed fields;
- job events never contain WLAN credentials.

The first roaming-policy model additionally covers:

- same-profile, fresh and healthy target eligibility;
- source trigger, improvement hysteresis and a confirmation window;
- BTM-only active steering with no forced legacy-client fallback;
- one in-flight transition generation per station;
- reject/timeout/target-loss cooldowns and a bounded retry budget;
- minimum dwell after an observed transition to prevent ping-pong.

The model consumes normalized observations and emits decisions only. It does
not invoke hostapd, configure 802.11k/v/r, or model the backhaul.
