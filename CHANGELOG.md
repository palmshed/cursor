# Changelog

## [0.1.6] - 2026-05-25

- **CI/CD** – Bump trivy action to v0.36.0
- **Docs** – Quiet README, simplify contributing guide for collaborators

## [0.1.5] - 2026-02-18

- **Build** – Auto-update changelog in version bump script

## [0.1.4] - 2026-02-18

- **CI/CD** – Fix tag format in bump-version action

## [0.1.3] - 2026-02-18

- **CI/CD** – Internal release process refinements

## [0.1.2] - 2026-02-18

- **CI/CD** – Add release and version bump workflows, tracking workflow, locked issue handling
- **Build** – Fix CMake compatibility (remove +build from version), use libattr1-dev on Ubuntu
- **Docs** – Add opencode agent guide

## [0.1.1] - 2026-02-18

- **Security** – Exclude third-party deps from CodeQL to eliminate false positive DES alerts
- **CI/CD** – Add build deps, health checks, milestone assignment for PRs
- **Workflow Fix** – Check locked issue before commenting to prevent failures
- **Build** – Fix invalid macOS deployment target in CMake
- **AI** – Resolve Ollama API timeout using std::chrono::seconds
- **Code Quality** – C++20 modernization, clang-tidy fixes, naming conventions
- **Dependencies** – Update actions/checkout, codeql-action, trivy-action to latest versions
- **Maintenance** – License year update (2024 → 2026)

## [0.1.0] - 2025-11-05

- **CI/CD** – Add automated release system with GitHub Actions

## [0.0.1..0.0.9] - 2025-09..2025-10

- Initial development releases with E2E testing, Docker mocks, and CI/CD setup
