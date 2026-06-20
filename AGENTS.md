# Agents Architecture Guide

This document describes the current system architecture and rules.

It is not runtime help and does not describe CLI commands.

---

## System Structure

```
app/       execution flow (startup, session, routing)
ui/        terminal rendering
core/      session state ownership
services/  external effects + observability
```

---

## Agent

Agent is a thin orchestrator.

Responsibilities:
- initialize startup flow
- run session loop
- delegate input to CommandRouter
- hold SessionState

No business logic lives here.

---

## SessionState (core/SessionState)

Single source of truth for runtime state.

Includes:
- mode
- model selection
- flags (debug, verbose)
- counters (commands, tokens)

Rules:
- only shared mutable state object
- no duplication elsewhere
- passed by reference where needed

---

## UI Layer

UI is pure rendering.

Responsibilities:
- prompts
- menus
- logs
- formatting

Rules:
- no state mutation
- no command execution
- no service calls directly

---

## Command Router (app/command_router)

Central command dispatcher.

Responsibilities:
- parse input
- route commands
- call services and UI
- update SessionState when needed

Rules:
- all command logic stays here
- no routing in UI or Session

---

## Session Loop

Minimal execution loop:

- read input
- send to router
- update state
- repeat until exit

Rules:
- no feature logic in loop
- loop remains stable and small

---

## Services Layer

External effects only.

Examples:
- replay logging
- file IO
- system integration

Rules:
- no ownership of core state
- preferably stateless
- invoked from router/session only

---

## Replay System

Append-only execution log system.

Stores:
- input
- state_before
- state_after
- timestamp

Capabilities:
- list sessions
- step through execution
- deterministic replay

Rules:
- does not modify execution flow
- uses normal router execution path

---

## Boundaries

Strict separation:

- app → flow only
- ui → rendering only
- core → state only
- services → effects + observability

No cross-layer ownership.

---

## Extension Rule

When adding features:

- prefer extending existing modules
- do not introduce new layers without duplication evidence
- keep Agent minimal

---

## Stability Goal

System is optimized for:

- predictability
- low coupling
- traceable execution
- minimal abstraction overhead
```