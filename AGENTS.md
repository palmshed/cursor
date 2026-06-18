# Agent Guide — Llamaware

## Build & Test

```bash
make build        # cmake .. && make
make test         # build + run with test input
./build/llamaware-tests   # unit tests
```

## Conventions

- C++20 — `starts_with()`, `std::optional`, structured bindings
- Snake case for functions/variables, PascalCase for classes
- Static service methods, no state
- No exceptions in hot paths — return `std::optional` / error codes
- No raw pointers — RAII wrappers or `unique_ptr`
- `#pragma once` in headers, `#ifdef _WIN32` for platform code

## Workflow

plan → implement → build → test → fix → cleanup → continue

- Detect stale, duplicated, or dead code as you go
- Fix warnings and failing tests immediately
- Keep dependencies and config clean
- Don't remove things that serve a purpose, even if lightweight

## Current State

- 16 services, 8 AI providers, CI/CD across 3 platforms
- God class `agent.cpp` (76KB) — main technical debt
- Low test coverage — 2 unit tests
- RAII wrappers: `CurlHandle`, `EvpCipherCtx`
- Extension list: `Utils::Validator::get_text_extensions()` (single source of truth)
- 6 GitHub Actions workflows active

## What Not To Do

- Don't add new files unless explicitly told
- Don't write comments in code unless needed
- Don't introduce new dependencies
- Don't create documentation or README files unless asked
