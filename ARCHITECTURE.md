# Architecture

Cursor is organized around four top-level modules.

## Layout

| Module | Location | Responsibility |
|---|---|---|
| `app/` | `src/app/` | Startup, session lifecycle, command routing, menus |
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

`Agent` is a lightweight orchestrator responsible for wiring the application together.

Business logic belongs in services.

Rendering belongs in `ui/`.

Session flow belongs in `app/`.

## Design Principles

### Separation of Concerns

* `app/` controls flow
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
* `core/` → state ownership (SessionState)
* `services/` → effects + observability

The Replay system exists as an observability service in `services/`.

## Stability

The architecture prioritizes:

* predictable execution
* low coupling
* traceable execution
* minimal abstraction overhead

No new architectural layers are expected for current feature work.
