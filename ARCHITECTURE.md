# Architecture

Cursor is organized around four top-level modules.

## Layout

| Module | Location | Responsibility |
|---|---|---|
| `app/` | `src/app/` | Startup, session lifecycle, command routing, menus |
| `core/` | `src/core/` | Runtime state ownership |
| `ui/` | `src/ui/` | Terminal rendering, markdown output, spinners, prompts |
| `services/` | `src/services/` | AI, Git, GitHub, Web, File, Auth, MCP, and other integrations |
| `utils/` | `src/utils/` | Shared utilities and low-level helpers |

## Entry Point

```
main.cpp
  ↓
Agent
  ↓
app/
  ↓
services/
  ↓
ui/
```

`Agent` is a thin lifecycle coordinator — no decision logic.

**ExecutionEngine is the decision layer. Replay is the evidence layer.**

Goal classification, tool orchestration, outcome generation, and confidence/recovery metrics all live in the engine. Replay captures every event as an append-only evidence record. Together they replace what a traditional "business logic" layer would do.

Rendering belongs in `ui/`.

Session flow belongs in `app/`.

## Design Principles

### Separation of Concerns

* `app/` controls flow
* `core/` owns runtime state
* `ui/` controls presentation
* `services/` provide capabilities
* `utils/` provide shared helpers

### Conversation First

The terminal interface is centered around conversation.

UI code should remain isolated from application logic.

### Service-Oriented

External integrations should be implemented as services rather than added directly to orchestration code.

## Current State

The system has reached a set of stable boundaries:

* `app/` → flow control only
* `ui/` → rendering only
* `core/` → runtime state + metrics ownership (`SessionState`, `Outcome`, `RecoveryMetrics`, `TrustMetrics`)
* `services/` → execution engine, replay, CI repair, file IO, benchmarks

The ExecutionEngine is the decision layer: goal classification, tool orchestration, outcome generation, confidence scoring.

Replay is the evidence layer: append-only event store carrying outcome, recovery_metrics, trust_metrics, confidence_before/after, and schema_version. All metrics are derived from replay events via pure functions.

## Stability

The architecture prioritizes:

* reproducibility
* traceability
* deterministic execution
* evidence-based decision making
* minimal abstraction drift

No new architectural layers are expected without evidence of necessity.
