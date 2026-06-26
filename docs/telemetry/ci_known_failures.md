# Known CI Test Failures

These 8 `cursor-tester` scenarios fail consistently in CI.
They are **not regressions** — they require infrastructure
that the CI runner does not provide.

## CI-dependent (6)

| Scenario | Root cause |
|---|---|
| `docker_timeout` | Requires Docker daemon — not available in CI |
| `failure_without_logs` | Requires live GitHub workflow JSON — CI runner has no network access to GitHub API beyond checkout |
| `multiple_failed_jobs` | Same — requires live workflow data |
| `structural_extraction` | Same — requires live workflow data |
| `truncated_logs` | Same — requires live workflow data |
| `workflow_cancelled` | Same — requires live workflow data |

## Diagnostics / Telemetry (2)

| Scenario | Root cause |
|---|---|
| `evidence_export` | Depends on `cursor-agent --export-evidence` being a complete end-to-end run; CI runner lacks model/AI service configuration |
| `replay_integrity` | Requires reading and writing trace files with a full execution pipeline; CI runner limitations prevent deterministic replay |

## Validation pattern

To verify a change introduced no new failures, compare against this baseline:

```bash
./build/bin/cursor-tester scenarios/
# Expected: 41 scenarios, 33 passed, 8 failed
# Failed should be exactly the 8 above
```

## CI exit code

The CI job exits with code `8` when these 8 scenarios fail.
This is the expected value. A new regression would change the
failure count or add new scenario names to the failure list.
