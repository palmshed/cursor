# Workflows

| Workflow | Trigger | What it does |
|---|---|---|
| `ci.yml` | PR, push | Build, test, Docker push |
| `cla.yml` | PR | CLA check |
| `pages.yml` | Push (HTML only) | Deploy site to GitHub Pages |
| `release.yml` | Tag `v*` | Build binaries, release, formula update, npm publish |
| `brew.yml` | Manual | Update Homebrew formula SHA |
| `status.yml` | Cron */5 | Cache workflow runs JSON on `ci-cache` branch for site fallback |
| `security.yml` | Push, PR | Trivy security scan |
| `pr-body.yml` | PR | Clean PR body formatting |
| `response.yml` | Issue opened | Label platform, respond |
| `stale.yml` | Weekly | Mark/close stale issues/PRs |
| `labeler.yml` | PR | Auto-label PRs |
| `docs.yml` | Push | Sync docs |

## Secrets

| Secret | Used By |
|---|---|
| `NPM_TOKEN` | release.yml |
| `DOCKER_USERNAME` | ci.yml |
| `DOCKER_PASSWORD` | ci.yml |
| `CURSOR_BOT_CLIENT_ID` | brew.yml, pr-body.yml, response.yml, stale.yml |
| `CURSOR_BOT_PRIVATE_KEY` | brew.yml, pr-body.yml, response.yml, stale.yml |

## Notes

- `status.yml` pushes to `ci-cache` branch (not main), zero main history noise
