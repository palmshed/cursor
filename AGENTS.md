# Cursor

C++20 AI coding agent. 16 services, 8 AI providers, CI/CD across 3 platforms.

## Build & Test

```bash
cmake -S . -B build && cmake --build build
./build/cursor-tests   # unit tests
```

## Style

- C++20: `starts_with()`, `std::optional`, structured bindings
- `snake_case` for functions, `PascalCase` for classes
- Static service methods — no state (except `AIService`, `DatabaseService`)
- No exceptions in hot paths — return `std::optional` or error codes
- No raw pointers — RAII wrappers or `unique_ptr`
- `#pragma once` in headers, `#ifdef _WIN32` for platform code

## Workflow

`plan → implement → build → test → fix → cleanup → continue`

Detect stale, duplicated, or dead code as you go. Fix warnings and failing tests immediately. Keep dependencies and config clean. Don't remove things that serve a purpose.

## Technical Debt

- `agent.cpp` (76KB) — God class, main refactoring target
- 2 unit tests — far below where we need to be

## Hard Rules

- No new files unless explicitly told
- No comments in code unless necessary
- No new dependencies
- No documentation files unless asked
