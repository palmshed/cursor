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

---

# Current Phase

```text
Architecture Phase: Complete
Observation Phase: Complete (Telemetry Baseline Cleansed & Validated)
Active Engineering Phase: Directory-Aware Find
Repair Loop: Deferred
```

---

# Active Directives: Directory-Aware Find

Directory-Aware Find is the **only approved engineering target**.

## Blocked Work (Freeze Active)
Do NOT implement or add:
* New GoalTypes.
* New agent loops.
* Autonomous repair loops or code modifications.
* Natural language -> shell command translation.
* Git dashboard visualization tools.
* Review frameworks or code thought streaming.
* AI-based ranking layers.

## Telemetry Preservation
Preserve all existing telemetry schemas and modules:
* `ToolResult`
* `tool_history`
* `Replay` telemetry schema
* `validation_runner`
* `failure_topology.md` generation logic
* Permanent production vs. synthetic trace separation (excluding unit tests and benchmarks by default).

## Design Constraints
1. **Deterministic ranking only:** Cascade sequentially via:
   $$\text{Filename lookup} \rightarrow \text{Symbol lookup} \rightarrow \text{Implementation lookup (.cpp boost)} \rightarrow \text{Grep fallback}$$
   No LLM ranking.
2. **Full trace visibility:** Output search candidates, scores, winner, and reason in the trace JSON.
3. **Explicit Performance Metrics:** Track:
   - `filename_hits`
   - `symbol_hits`
   - `directory_hits`
   - `grep_hits`
4. **Validation:** Re-run the existing failure set:
   * `"where is replay implemented"`
   * `"find cursor binary"`
   * `"where is CommandRouter implemented"`
5. **Success Criterion:** Reduce production-only `insufficient_evidence` rate below **2.0%**.

If the implementation cannot demonstrate a measurable reduction in topology failures, stop and review traces before further work.

## Implementation Handoff Guardrails
1. **Tight Scope Control:** Keep implementation strictly limited to the `ExecutionEngine`, search layer, ranking layer, and retrieval telemetry. Do NOT touch `AIService`, goal routing, `SessionState`, `Replay`, or model selection mechanisms.
2. **Dedicated Retrieval Report:** Upon shipping, compile a dedicated report named `directory_aware_find_report.md` capturing:
   - Before/after metrics.
   - Top failing queries.
   - Queries fixed.
   - Queries still failing.
3. **Protect Benchmark Honesty:** Real developer production traces, synthetic benchmark traces, and unit-test execution traces must remain permanently separated. Never merge or contaminate them again.
4. **Immediate Stop Upon Shipping:** Immediately after implementing and validating, generate the updated topology and stop. Do not chain directly into another feature without explicit, data-driven approval from the new topology.


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

