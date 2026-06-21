# AGENTS Architecture Guide

This document describes the current architecture as it exists today.

It is not runtime help and does not describe CLI commands.

---

## Core Invariant

**Replay-backed evidence is the primary decision substrate.**

```text
Event
  ↓
Execution Path
  ↓
Replay
  ↓
Metrics
  ↓
Dashboard
  ↓
Decision Support
```

If replay integrity is preserved:

* behavior is traceable
* metrics are reproducible
* decisions can be audited

If replay integrity is compromised:

* dashboards become suspect
* confidence calibration becomes unreliable
* telemetry loses authority

The system is currently in the measurement phase. The primary objective is trustworthy observation rather than capability expansion.

---

## System Structure

```text
app/       runtime orchestration
ui/        rendering
core/      state + metric definitions
services/  execution, replay, observability, infrastructure
```

Ownership remains intentionally simple.

---

## Agent (the Coordinator Object)

The `Agent` class is a lightweight runtime coordinator — not the product's overall intelligence.

Responsibilities:

* initialize runtime components
* own SessionState
* connect Router, Engine, Replay, and UI
* propagate execution results into session state

The `Agent` object does not directly:

* perform analysis
* execute tools
* make domain decisions

Those responsibilities belong to CommandRouter, ExecutionEngine, and Services.

The product as a whole performs analysis through DiscoveryService, PlanningService,
ExecutionEngine, ConfidenceService, CI Investigation, VerificationService, and
benchmark execution. Do not read "Agent does not analyze" as "the system cannot analyze."

---

## SessionState (core)

SessionState is a runtime snapshot.

Includes:

* mode
* model selection
* runtime flags
* last outcome
* last recovery metrics
* last trust metrics
* confidence values

Rules:

* data only
* no logic
* no derived behavior
* no ownership of replay or metrics systems

---

## CommandRouter

CommandRouter is the runtime control plane.

Responsibilities:

* parse input
* select execution path
* dispatch to Engine, task pipeline, services, or meta commands
* coordinate replay updates
* coordinate state updates
* coordinate UI output

Current reality:

* NL→command mapping lives here
* execution-path selection lives here
* engine routing lives here
* task-pipeline routing lives here
* meta-command routing lives here

Rules:

* no long-lived ownership
* no persistent business state
* domain behavior should be delegated where practical

Although it is formally a routing component, it is currently the highest-authority runtime switchboard.

---

## Execution Engine

ExecutionEngine is the primary decision layer for engine-routed paths.

Responsibilities:

* classify goals
* coordinate investigation
* execute tool sequences
* evaluate confidence
* produce execution results

Outputs:

* Outcome
* RecoveryMetrics
* TrustMetrics
* confidence values

Current authority:

* CodebaseQuery
* GeneralChat
* CI-oriented investigation paths

Current limitation:

CodeChange goals transition into a separate task pipeline after classification. The Engine participates in classification and instrumentation but is not yet the sole authority for code-change execution.

This is a documented architectural split, not a known behavioral failure.

---

## Task Pipeline

CodeChange execution currently follows:

```text
Discovery
  ↓
Planning
  ↓
Approval
  ↓
AI Execution
  ↓
Preview
  ↓
Apply
  ↓
Verification
```

Current authority:

* CodeChange execution

Current relationship to Engine:

* Engine classifies
* Task pipeline executes

No failure cluster has yet justified unification work.

---

## UI Layer

UI is a rendering layer.

Responsibilities:

* execution traces
* plans
* diffs
* dashboards
* diagnostics
* benchmark output

Rules:

* no mutation
* no execution
* no state ownership
* no service orchestration

UI only renders supplied data.

---

## Services Layer

Services provide execution and infrastructure boundaries.

Examples:

* ReplayService
* ConfidenceService
* DiscoveryService
* PlanningService
* VerificationService
* DashboardService
* CapabilityRegistry
* WorkflowBenchmarkService
* CiInvestigationService

Rules:

* avoid ownership of system state
* prefer stateless behavior
* isolate side effects
* preserve replay compatibility where possible

Replay coverage is strongest for instrumented execution paths. Not every runtime path currently participates equally.

---

## Replay System

Replay is the canonical evidence store.

Replay events contain:

* input
* state_before
* state_after
* outcome
* recovery_metrics
* trust_metrics
* confidence_before
* confidence_after
* schema_version

Properties:

* append-only
* deterministic reconstruction
* dashboard source material
* calibration source material

Replay is the authoritative source for telemetry.

---

## Outcome Model

Every execution path should converge toward one outcome.

```cpp
enum class Outcome {
    Success,
    Failure,
    InsufficientEvidence,
    UserRejected
};
```

Interpretation:

* Success → capability validated
* Failure → capability insufficient
* InsufficientEvidence → investigation stopped correctly
* UserRejected → goal-understanding failure

These outcomes intentionally separate execution failures from trust failures.

---

## Metrics System

Metrics are deterministic functions over replay data.

Examples:

* outcome distributions
* recovery metrics
* trust metrics
* confidence calibration

Rules:

* deterministic
* replay-derived
* stateless
* reproducible

Metrics never become authoritative unless they can be traced back to replay evidence.

---

## Recovery Metrics

```cpp
struct RecoveryMetrics {
    int attempts;
    int strategy_changes;
    bool evidence_found;
    bool verification_found;
    double confidence_delta;
};
```

Purpose:

Measure recovery behavior rather than simple success/failure.

---

## Trust Metrics

```cpp
struct TrustMetrics {
    bool plan_approved;
    bool diff_approved;
    bool user_corrected_goal;
    bool reverted;
};
```

Purpose:

Measure user trust and goal alignment separately from execution quality.

---

## Dashboard

Dashboard is a query layer over replay data.

Responsibilities:

* aggregate outcomes
* aggregate trust metrics
* aggregate recovery metrics
* expose drill-down paths into source events

A dashboard number is only valid if it can be traced back to replay evidence.

---

## Confidence System

Confidence influences behavior.

Questions confidence attempts to answer:

* Should execution continue?
* Should more investigation occur?
* Should execution stop?

Confidence is evidence-backed, not certainty-backed.

Low confidence is a valid outcome.

"I do not have enough evidence yet" is considered correct behavior.

---

## Benchmark System

Benchmarks exist to measure capability and recovery.

Current benchmark classes:

* workflow benchmarks
* recovery benchmarks

Purpose:

* identify recurring failure modes
* validate instrumentation
* measure recovery quality

Benchmarks do not justify capabilities by themselves.

They provide inputs to telemetry.

---

## Architectural Boundaries

```text
app       → orchestration
ui        → rendering
core      → state + metric definitions
services  → execution + replay + infrastructure
```

Ownership should remain unambiguous.

Avoid creating new layers without demonstrated pressure.

---

## Extension Rule

When considering a new capability:

1. Which benchmark fails?
2. Which outcome dominates?
3. Which recovery path was exhausted?
4. Why did confidence remain low?
5. What replay evidence proves insufficiency?

If these questions cannot be answered, the capability has not yet been earned.

---

## Known Architectural Reality

### Split Authority

The runtime currently contains two authorities:

```text
ExecutionEngine
Task Pipeline
```

Engine authority:

* classification
* confidence
* instrumentation
* investigation-oriented paths

Task pipeline authority:

* code-change execution

This split is intentional documentation of reality, not a call for immediate refactoring.

No stable outcome cluster has yet justified unification.

---

### Partial Replay Coverage

Replay is authoritative for instrumented paths.

Not every execution path currently participates equally:

* direct service commands
* shell escapes
* certain bypass routes

This is known structural asymmetry.

No dominant failure trend has been attributed to it.

---

### CI Is External

CI operates on:

```text
Commit
  ↓
Workflow
  ↓
Pass/Fail
  ↓
Artifact
```

This is separate from the replay evidence chain.

CI is a validation system, not a replay system.

---

## Measurement Phase

Current project phase:

```text
Architecture → Complete
Capability   → Complete
Measurement  → Active
```

Primary artifact:

* replay-backed observations

Primary question:

```text
What repeatedly fails?
```

Decision rule:

```text
No capability work without telemetry justification.
```

The framework's purpose is not to generate capabilities.

Its purpose is to justify them.

Observation is the work.

## Product-Surface Gap

The system's internal capabilities exceed its surface-level behavior.

Common codebase questions (e.g. "tell me about this codebase") reach ChatOnly
instead of Engine-driven Repository Investigation because routing keywords
("tell me about") were missing from the engine's goal classifier.

All architectural capability already exists:

* DiscoveryService
* PlanningService
* ExecutionEngine
* ConfidenceService
* Replay
* Evidence collection
* Benchmarks

The gap is routing — not capability.

Tracking metric:

```
Codebase-oriented prompts → ExecutionPath → ChatOnly %
```

If ChatOnly % remains high under natural traffic, discoverability is the
bottleneck — not search, confidence, recovery, or capability.

No new architecture is justified until that metric stabilizes and reveals
a failure cluster that existing capability cannot address.
