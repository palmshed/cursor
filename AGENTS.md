# AGENTS Architecture Guide

This document describes the current architecture as it exists today.

It is not runtime help and does not document CLI commands.

---

# Core Principle

**Evidence drives decisions.**

The system should prefer repository evidence over assumptions.

Investigation happens before synthesis.

Capabilities are expanded only when telemetry demonstrates a recurring failure pattern.

---

# System Structure

```text
app/       runtime orchestration
ui/        rendering
core/      state, model catalog, metrics
services/  execution, replay, AI, infrastructure
utils/     shared helpers
```

Ownership should remain simple and explicit.

---

# Runtime Flow

The system is an **investigation engine**, not an AI agent wrapper.
Intelligence is concentrated in the Planner; everything else is deterministic infrastructure.

```text
User Query
    ↓
Intent Classification
    ↓
Planner
    ↓
Investigation
    ├── Retrieval  (find, symbols, references, grep, read)
    ├── Evidence   (ranking, tool_history, EvidenceStore)
    ├── Confidence (gate evaluation)
    └── Completion (is the goal achieved?)
    ↓
AIService (synthesis — uses evidence only, does not investigate)
    ↓
Answer
```

Investigation and synthesis are separate responsibilities.
The Planner is the only intelligence layer.

---

# Agent

The `Agent` class is a runtime coordinator.

Responsibilities:

* initialize runtime components
* own SessionState
* connect Engine, Replay, Router, and UI
* coordinate lifecycle

The Agent is not a decision layer.

---

# SessionState

SessionState is a runtime snapshot.

Examples:

* active_model
* execution state
* outcome data
* recovery metrics
* trust metrics

Rules:

* data only
* no business logic
* no provider-specific behavior

---

# ExecutionEngine

The ExecutionEngine is the primary decision layer.

Responsibilities:

* goal classification
* repository investigation
* tool orchestration
* evidence collection
* confidence evaluation
* completion decisions
* outcome generation

Outputs:

* Outcome
* RecoveryMetrics
* TrustMetrics
* confidence values
* tool history

The ExecutionEngine owns investigation.

---

# AIService

AIService is a synthesis layer.

Responsibilities:

* receive repository evidence
* answer from evidence
* explain findings

AIService does not:

* investigate repositories
* execute tools
* emit commands
* perform orchestration

Its role is synthesis only.

---

# ToolResult

Every tool execution produces a structured result.

```cpp
struct ToolResult {
    std::string tool;
    std::string args;
    std::string stdout;
    std::string stderr;
    int exit_code;

    bool success() const;
};
```

ToolResult is the canonical observation unit.

---

# Tool History

Investigations accumulate an ordered history of observations.

```text
ToolResult
    ↓
tool_history
    ↓
EvidenceStore
    ↓
Replay
```

Tool history exists to support:

* trace analysis
* validation
* telemetry
* future repair-loop design

---

# Replay

Replay is the evidence layer.

Responsibilities:

* append-only event storage
* outcome recording
* confidence tracking
* trust metrics
* recovery metrics
* schema versioning

Replay is the authoritative telemetry source.

Metrics should be replay-derived whenever possible.

---

# Model Catalog

ModelCatalog is the single source of truth for:

* providers
* models
* capabilities
* categories
* pricing tiers
* display metadata

Provider behavior is data-driven.

New providers and models should be added through catalog entries rather than enums and switch chains.

---

# Architecture Review

ArchitectureReview is a read-only investigation mode.

Responsibilities:

* detect dead code
* identify duplication
* inspect state ownership
* inspect telemetry usage
* review validation coverage

ArchitectureReview does not modify files.

---

# Validation

Current validation layers:

## Benchmark Suite

Synthetic capability validation.

## Validation Runner

End-to-end investigation validation.

Tracked metrics include:

* goal type
* iterations
* tools executed
* files read
* duration
* duplicate tools
* failed tools
* outcome
* failure class
* recoverable flag

---

# Outcome Model

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
* UserRejected → goal understanding failure

---

# Design Principles

## Evidence Before Opinion
Repository evidence takes priority over model assumptions.

## Separation of Concerns
* app → orchestration
* ui → rendering
* core → state and domain models
* services → capabilities
* utils → helpers

## Data-Driven Configuration
Models and providers belong in catalogs, not enum chains.

## Minimal Abstraction Drift
Avoid new layers unless telemetry justifies them.

## Senior Software Engineer Agent Definition
A senior software engineer agent is defined by its disciplined *behavior* and communication, rather than complex autonomous *capabilities*. The agent must:
* Find the right code quickly.
* Read only what is necessary.
* Explain why it chose those files.
* State uncertainty when evidence is weak.
* Never hide its investigation.
* Avoid unnecessary tool calls.
* Produce deterministic answers whenever possible.

*(Note: Capabilities like autonomous editing, repair loops, subagents, and planning complexity are secondary to these behavioral traits that build trust.)*

## The "Reality Check" Gate
Every new capability or proposed feature must answer these five questions **before implementation begins**:
1. **Which production traces fail today because this capability is missing?**
2. **How will we measure success?**
3. **What existing capability is insufficient?**
4. **What telemetry metric should improve?**
5. **When do we stop?**

If these five questions cannot be answered with empirical telemetry evidence, the feature proposal waits.


---

# Current Phase

```text
Architecture Phase: Complete
Observation Phase: Complete (Telemetry Baseline Cleansed & Validated)
Active Engineering Phase: Code Search Excellence — Complete
Maturity Level 1 (Retrieval): Achieved — deterministic, measurable, gated
Maturity Level 2 (Investigation): Next — planner must know when it has enough evidence
Repair Loop: Deferred
```

---

# Active Directives: Planner Investigation (Level 2)

The planner must know when it has enough evidence, when it doesn't,
and how to recover before answering.

## Core Principle

> **The planner owns the investigation. Tools only gather evidence.**

## Blocked Work (Freeze Active)
Do NOT implement or add:
* Code modification or editing capabilities to the agent (Autonomous editing freeze remains active).
* Subagents or multi-agent planners.
* Autonomous repair loops.
* Natural language -> shell command translation.
* Git dashboard visualization tools.
* AI-based ranking layers (deterministic ranking only).
* Plan visualization or investigation plan displays.

## Telemetry & Validation Preservation
Preserve all existing telemetry schemas and modules:
* `ToolResult`
* `tool_history`
* `Replay` telemetry schema
* `validation_runner`
* `docs/telemetry/failure_topology.md` generation logic
* `architecture_query_failure_report.md` — Level 1 closure evidence

## Core Priorities & Design Constraints

### Priority 1: Planner Recovery
The planner must be able to revise an investigation mid-flight instead
of stopping at the first successful read. If confidence is moderate
after reading, the planner should continue gathering evidence rather
than declaring success.

### Priority 2: Investigation State
The planner must maintain structured awareness during investigation:
* What files have already been examined?
* What evidence was found?
* What evidence is still missing?
* Which hypotheses or tool paths have failed?

This replaces the current pattern of checking for `find:results` /
`read` existence with a richer model of investigation progress.

### Priority 3: Completion Gating
Completion must answer "is the question answered?" not "did a tool
succeed?" A successful `read` should not automatically terminate the
investigation. The gate must consider:
* Question type (lookup vs explanation vs architecture)
* Confidence level after each evidence collection step
* Whether key evidence classes are still missing

### Priority 4: Confidence as Control Signal
Confidence must drive the planner, not just report final quality:
* High → synthesize and answer
* Medium → continue investigation (read more, use references)
* Low → report insufficient evidence or ask for clarification

### Priority 5: Planner Telemetry
Track new planner-level metrics alongside existing retrieval metrics:
* `recovery_rate` — how often the planner revises its investigation
* `average_revisions` — mean revisions per investigation
* `files_reread` — files examined more than once
* `premature_stop_rate` — investigations that stopped but had missing evidence classes
* `final_confidence_vs_success` — correlation between confidence and user-facing success

---

## Maturity Model

Planner capability progresses through levels. Each level must be validated by telemetry before the next begins.

| Level | Name | Success Metric | Status |
|---|---|---|---|
| 1 | **Retrieval** — find, grep, read, symbols, references | Can it reliably find the right evidence? | ✅ |
| 2 | **Investigation** — planner recovery, confidence gating, multi-read | Does it know when it doesn't know enough? | ▶ Next |
| 3 | **Investigation Plans** — visible multi-step plans | Can it explain why it is reading each file? | — |
| 4 | **Plan Revision** — planner edits its own investigation mid-flight | Can it recover from wrong evidence? | — |
| 5 | **Independent Tasks** — natural decomposition into parallel work | Queries that benefit from subagents common in production | — |
| 6 | **Domain Specialists** — per-domain subagents with deep expertise | Subagent specialization measurably improves quality | — |

---

## Subagents Promotion Rule
The subagents freeze remains fully active. Subagents will only be considered when:
1. Planner consistently achieves Level 4+ (Plan Revision) quality targets
2. Telemetry shows queries requiring naturally independent work (configuration, runtime, tests, performance) are common in production
3. A single planner cannot meet search quality targets across representative architectural queries

---

## Success Criteria (Explicit Acceptance Criteria)
* **Grep Fallback Rate:** Decreases by at least **25%** across the architectural benchmark query set.
* **Retrieval Efficiency:** A measurable decrease in `average_files_read` per query without reducing correctness.
* **Benchmark Target:** Architectural queries achieve a **90%** first-pass success rate (evidence-backed, accurate answers).
* **Regressions:** Zero regression in existing validation runs or benchmark suites.
* **Telemetry Reporting:** Stop immediately upon completing implementation and generate an updated telemetry report.




---

# Guiding Question

The architecture exists to answer:

```text
What repeatedly fails?
```

and equally important:

```text
What does not need to be built?
```

Capability growth should be justified by telemetry rather than intuition.

