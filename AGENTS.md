# AGENTS Architecture Guide

This document defines the current system architecture.

It is not runtime help and does not describe CLI commands.

---

## Core Invariant

**Evidence chain is the system.**

```
Event → Router → (Engine | Services | Pipelines) → Partial Replay → Metrics → Dashboard → Decision Support
```

If this chain is intact:

* system is reproducible
* behavior is verifiable
* correctness is recoverable

If it breaks:

* nothing else is trustworthy

**Current state**: the evidence chain is authoritative within the engine domain (unmatched NL queries), but not yet globally binding across the full execution surface. Service-direct commands (`:search`, `:git`, `:write`, etc.), meta commands, and shell escapes bypass the evidence chain — these paths may not fully participate in global replay semantics. This is structural asymmetry without observed harm — no behavioral failure has been traced to it. The correct end-state is unified authority under the engine; it will be pursued when (and only when) a failure cluster traceable to the split emerges.

---

## System Structure

```
app/       execution flow (startup, session loop, routing)
ui/        rendering only (no state, no logic)
core/      state + metrics ownership (source of truth)
services/  execution engine, replay, CI, observability
```

---

## Agent

Agent is a lightweight coordinator over the execution engine.

Responsibilities:

* initialize session
* delegate input to CommandRouter
* hold SessionState (as runtime snapshot only)
* forward results from ExecutionEngine into state + replay pipeline

Agent does NOT:

* decide execution paths
* contain business logic
* perform analysis

All decision logic is in ExecutionEngine (for engine-routed paths).

---

## Execution Engine (classification + evidence)

The ExecutionEngine is a domain-limited decision layer — authoritative only for engine-routed paths.

Responsibilities:

* classify goal (codebase query, code change, general)
* select execution path
* run tool sequence
* produce ExecutionResult

Outputs:

* Outcome (Success / Failure / InsufficientEvidence / UserRejected)
* RecoveryMetrics
* TrustMetrics
* Confidence (before/after)

---

## SessionState (core)

Runtime snapshot of session context.

Includes:

* mode
* model selection
* flags
* last outcome
* last recovery metrics
* last trust metrics
* confidence (before/after)

Rules:

* state is derived from execution engine output
* must not contain logic or derived decisions
* no duplication of replay or metric systems

---

## UI Layer

UI is pure rendering.

Responsibilities:

* display state and execution traces
* show plans, diffs, logs, dashboards

Rules:

* no mutation
* no execution
* no service calls
* only renders passed data

---

## Command Router

CommandRouter is a dispatch layer only.

Responsibilities:

* parse input
* route to ExecutionEngine or direct system commands
* pass results to UI + SessionState + ReplayService

Rules:

* no decision logic
* no tool orchestration logic
* no domain logic

Note: although CommandRouter is formally a dispatch layer, it is the runtime authority switchboard — it selects between engine path, service-bypass path, task pipeline, UI/meta, and escape paths. This makes it the de facto global control plane, even though it holds no domain logic.

---

## Services Layer

Services are effect and infrastructure boundaries.

Includes:

* ReplayService
* ExecutionEngine support tools
* CI investigation + repair
* file/system IO
* benchmark + validation tools

Rules:

* no ownership of system state
* stateless where possible
* all effects must be replayable

Note: CI workflows (GitHub Actions) are external validation pipelines — they operate on a Commit → CI → Pass/Fail → Artifact model, not the Event → Replay → Metrics → Dashboard → Decision evidence chain. They are not part of the replay or execution engine evidence system.

---

## Replay System

Replay is the canonical evidence store for instrumented execution paths.

Each event contains:

* input
* state_before
* state_after
* outcome
* recovery_metrics
* trust_metrics
* confidence_before / after
* schema_version

Properties:

* append-only
* deterministic replay via router path
* used for dashboard reconstruction
* source of truth for all metrics (within instrumented paths)

---

## Metrics System

Metrics are pure functions over replay events.

Rules:

* must be deterministic
* must be stateless
* must not depend on UI or runtime state
* defined by hash(metric_definition)

Metrics are computed as:

```
ReplayEvents × (metric_definition_hash, schema_version)
```

---

## Boundaries

Strict separation:

* app → orchestration only
* ui → rendering only
* core → state + metrics only
* services → execution + replay + infrastructure

No cross-layer ownership.

---

## Extension Rule

When adding features:

* extend existing systems first
* no new layers without evidence of necessity
* preserve replay compatibility
* maintain deterministic execution paths

---

## Stability Goal

System optimizes for:

* reproducibility
* traceability
* deterministic execution
* evidence-based decision making
* minimal abstraction drift

---

## Progress Summary (Corrected View)

### Completed Systems

* Phase 1–3a: architecture extraction (agent, ui, core, services)
* Replay system (schema-v1, deterministic logs)
* Execution Engine (goal classification + tool orchestration)
* CI investigation + repair pipeline
* Planning + evidence-based task execution
* Capability registry + self-test + benchmark suite
* Permission modes (REVIEW / APPLY / AGENT)
* Execution tracing + diff approval system
* Metrics system (Outcome, RecoveryMetrics, TrustMetrics, Confidence)
* Dashboard reconstruction from replay
* Confidence calibration (interactive vs benchmark bands)

### Current State

* 133 sessions, 903 events
* outcome + recovery + trust metrics fully instrumented
* replay-driven dashboard reconstruction active
* extraction fix validated (23/23, zero topology shift)
* schema_version ready for additive instrumentation

---

## Known Architecture (Latent)

### Dual-system: Engine + Task Pipeline

The ExecutionEngine classifies `CodeChange` goals, produces evidence + confidence + outcome, but its output is discarded by CommandRouter in favor of a separate task-pipeline (`DiscoveryService → PlanningService → approval → AI chat → verification loop`).

This means:

* Engine is authoritative for CodebaseQuery, CICheck, and GeneralChat goals
* Engine is observational for CodeChange — replays the classification but not the execution
* Task pipeline is the actual authority for CodeChange

No behavioral failure or outcome cluster has been traced to this split. It is documented here as structural drift without observed cost. The correct resolution (unifying authority under Engine) will be pursued when — and only when — a CodeChange failure cluster appears that replay can trace to the split.

## Next Steps

* Observe natural traffic for signal clustering
* Classify InsufficientEvidence distribution across:
  * representation gaps
  * execution gaps
  * intent ambiguity
* Maintain replay integrity under all new changes
* No new architecture unless evidence demands it
```
