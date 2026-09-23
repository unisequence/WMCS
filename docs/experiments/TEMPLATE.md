# Experiment WMCS-EXP-XXXX

Status: planned

## Question

State one behavior or protocol question. Change one variable whenever possible.

## Provenance

- Researcher:
- Date and timezone:
- Public references:
- Clean-room role: research / implementation / both
- Data handling classification: public / sanitized / sensitive external

## Topology

Describe every link, VLAN, bridge, capture point, management path, and recovery
path. Add a compact diagram when useful.

## Devices

| Role | Vendor and model | Hardware revision | Firmware / OpenWrt build | MAC alias | Notes |
|---|---|---|---|---|---|
| Controller | | | | | |
| Candidate/member | | | | | |
| Capture host | | | | | |

Use experiment-local aliases in public records. Do not publish real credentials,
private keys, or unrelated user identifiers.

## Initial state

- Factory-clean or prior state:
- Configuration snapshot SHA-256:
- Package inventory SHA-256:
- Relevant service and interface state:
- Recovery image/configuration:
- Independent management path verified: yes / no

## Controlled action

Describe one action precisely, including its start condition and timeout.

## Expected observation

Record the expectation before running the experiment.

## Captures and artifacts

| Artifact | Storage reference | SHA-256 | Sanitized fixture committed? |
|---|---|---|---|
| Packet capture | | | no |
| Controller logs | | | no |
| Member logs | | | no |
| State snapshots | | | no |

Sensitive artifacts remain outside Git. A committed fixture must be minimized,
synthetic or sanitized, and independently reviewed for secrets.

## Observed behavior

Record timestamps, message ordering, state transitions, retries, timeouts, and
all configuration changes. Separate direct observations from interpretation.

## State diff

- Controller before/after:
- Member before/after:
- Unrelated state preserved:
- Services restarted or interrupted:

## Failure and recovery

- Failure injected:
- Expected rollback:
- Observed rollback:
- Management connectivity preserved:
- Manual recovery required:

## Interpretation

- Conclusion:
- Confidence: low / medium / high
- Alternative explanations:
- Unknown fields or transitions:
- Follow-up experiment:

## Compatibility impact

State whether this changes an entry in `docs/compatibility/MATRIX.md`. An
observation alone is not a `supported` compatibility claim.
