# Workflows

| Workflow | Trigger | What it does |
|----------|---------|--------------|
| `ci.yml` | PR, push | Build, test, Docker push |
| `cla.yml` | PR | CLA check |
| `pages.yml` | Push (HTML only) | Deploy site to GitHub Pages |
| `release.yml` | Tag `v*` | Build binaries, release, formula update, npm publish |
| `formula-sha.yml` | Manual | Update Homebrew formula SHA |
| `ci-status.yml` | Push, cron */5 | Cache workflow runs JSON for site fallback |
| `security.yml` | Push, PR | Trivy security scan |
| `version-bump.yml` | Tag `v*` | Bump version |
| `pr-body.yml` | PR | Clean PR body formatting |
| `issue-response.yml` | Issue opened | Label platform, respond |
| `stale.yml` | Weekly | Mark/close stale issues/PRs |
| `labeler.yml` | PR | Auto-label PRs |
| `docs-sync.yml` | Push | Sync docs |

## Secrets

| Secret | Used By |
|--------|---------|
| `NPM_TOKEN` | release.yml |
| `DOCKER_USERNAME` | ci.yml |
| `DOCKER_PASSWORD` | ci.yml |
| `CURSOR_BOT_CLIENT_ID` | formula-sha.yml, pr-body.yml, issue-response.yml, stale.yml |
| `CURSOR_BOT_PRIVATE_KEY` | formula-sha.yml, pr-body.yml, issue-response.yml, stale.yml |
