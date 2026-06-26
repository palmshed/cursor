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

```text
User Query
    ↓
ExecutionEngine
    ↓
ToolResult
    ↓
tool_history
    ↓
EvidenceStore
    ↓
Completion Gate
    ↓
AIService
    ↓
Answer
```

Investigation and synthesis are separate responsibilities.

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
Active Engineering Phase: Code Search Excellence
Repair Loop: Deferred
```

---

# Active Directives: Code Search Excellence

Code Search Excellence is the **only approved engineering target**.

## Blocked Work (Freeze Active)
Do NOT implement or add:
* Code modification or editing capabilities to the agent (Autonomous editing freeze remains active).
* Subagents or multi-agent planners.
* Autonomous repair loops.
* Natural language -> shell command translation.
* Git dashboard visualization tools.
* AI-based ranking layers (deterministic ranking only).

## Telemetry & Validation Preservation
Preserve all existing telemetry schemas and modules:
* `ToolResult`
* `tool_history`
* `Replay` telemetry schema
* `validation_runner`
* `docs/telemetry/failure_topology.md` generation logic

## Core Priorities & Design Constraints

### Priority 1: Search Pipeline
Implement/enforce a deterministic search pipeline that cascades sequentially. The pipeline must explicitly separate **Retrieval** (gathering evidence) from **Synthesis** (explaining evidence) to prevent the LLM from inventing facts when evidence is weak. The agent must never jump directly to broad `grep` if a higher-confidence stage succeeds:
1. Intent classification
2. Filename lookup
3. Symbol lookup
4. Reference lookup
5. Directory-aware ranking
6. Read selected files (Retrieval phase complete)
7. Synthesize answer from retrieved evidence only (Synthesis phase)

### Priority 2: Evidence-First Navigation
Every response must ground itself explicitly in collected evidence:
* List which files were examined and why they were selected.
* Detail which symbols and references matched.
* Explicitly state if the repository evidence gathered is weak or insufficient (preventing hallucinations).

### Priority 3: UX Clarity & Measurability
Avoid silent transitions. The user must always see clear, incremental progress updates on current operations:
* *e.g.* `Locating files...` $\rightarrow$ `✓ 4 candidates`
* *e.g.* `Finding references...` $\rightarrow$ `✓ 9 call sites`
* *e.g.* `Reading implementation...` $\rightarrow$ `✓ replay_service.cpp`
* *e.g.* `Preparing answer...`

We must measure UX quality via:
* **Time until first visible progress:** Latency before the first progress token/section is emitted to the user.
* **Time until first useful result:** Latency before the first successful file or reference search result is retrieved.
* **Total investigation time:** End-to-end execution duration of the search and synthesis pipeline.

### Priority 4: Search Quality Metrics
Track and optimize the following metrics:
* `filename_hit_rate`
* `symbol_hit_rate`
* `reference_hit_rate`
* `grep_fallback_rate`
* `average_files_read`
* `average_search_latency`

### Priority 5: Architectural Queries Benchmark Set
The system must be validated against a broad set of senior-level architectural questions:
* **Implemented Queries:**
  * `"How is model wiring done?"`
  * `"Where does replay begin?"`
  * `"What owns this service?"`
  * `"Which layer depends on this component?"`
  * `"Where is this configured?"`
* **Relational & Flow Queries:**
  * `"Who owns X?"`
  * `"Where is X configured?"`
  * `"How does X flow to Y?"`
  * `"What depends on X?"`
  * `"What initializes X?"`

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

