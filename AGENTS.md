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

Architecture decisions should be informed by replay-backed evidence rather than intuition.

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

## Agent (Coordinator)

The `Agent` class is a lightweight runtime coordinator.

Responsibilities:

* initialize runtime components
* own SessionState
* connect Router, Engine, Replay, and UI
* propagate execution results into session state

The `Agent` object does not directly:

* perform analysis
* execute tools
* make domain decisions

Those responsibilities belong to CommandRouter, ExecutionEngine, Task Pipeline, and Services.

The system as a whole performs analysis through investigation, planning, verification, confidence evaluation, benchmarking, and repository exploration services.

Do not interpret "Agent does not analyze" as "the system cannot analyze."

---

## SessionState

SessionState is a runtime snapshot.

Includes:

* mode
* model selection
* runtime flags
* execution path
* last outcome
* recovery metrics
* trust metrics
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
* dispatch to Engine, Task Pipeline, Services, or Meta Commands
* coordinate replay updates
* coordinate state updates
* coordinate UI output

Current reality:

* NL routing lives here
* execution-path selection lives here
* engine routing lives here
* task-pipeline routing lives here
* meta-command routing lives here

Rules:

* no long-lived ownership
* no persistent business state
* delegate domain behavior where practical

Although formally a routing component, CommandRouter is currently the highest-authority runtime switchboard.

---

## Execution Engine

ExecutionEngine is the primary decision layer for engine-routed paths.

Responsibilities:

* classify goals
* establish repository context
* coordinate investigation
* execute tool sequences
* evaluate confidence
* produce execution results

Outputs:

* Outcome
* ExecutionPath
* RecoveryMetrics
* TrustMetrics
* confidence values

Current authority:

* CodebaseQuery
* GeneralChat
* CI-oriented investigation
* repository exploration

Current limitation:

CodeChange goals transition into a separate Task Pipeline after classification.

The Engine participates in classification, confidence evaluation, and instrumentation but is not the sole authority for code-change execution.

This is documented reality, not a known behavioral failure.

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

Authority:

* code-change execution

Relationship to Engine:

* Engine classifies
* Task Pipeline executes

No stable failure cluster has justified unification work.

---

## UI Layer

UI is a rendering layer.

Responsibilities:

* conversation rendering
* execution traces
* plans
* diffs
* diagnostics
* benchmark output
* dashboards

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
* preserve replay compatibility

Replay coverage is strongest for instrumented execution paths.

Not every runtime path currently participates equally.

---

## Replay System

Replay is the canonical evidence store.

Replay events contain:

* input
* execution_path
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

Replay is the authoritative telemetry source.

---

## Execution Path Model

```cpp
enum class ExecutionPath {
    Unknown,
    ChatOnly,
    Engine,
    TaskPipeline,
    DirectService,
    MetaCommand,
    ShellEscape
};
```

Purpose:

* identify which authority handled a request
* distinguish capability availability from capability usage
* measure discoverability of existing functionality

Observed path matters more than intended path.

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
* execution-path distributions
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

Measure recovery behavior rather than simple success or failure.

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

Measure trust and goal alignment separately from execution quality.

---

## Dashboard

Dashboard is a query layer over replay data.

Responsibilities:

* aggregate outcomes
* aggregate execution paths
* aggregate recovery metrics
* aggregate trust metrics
* expose drill-down paths into source events

A dashboard number is valid only if it can be traced back to replay evidence.

Dashboard honesty is more important than dashboard completeness.

---

## Confidence System

Confidence influences behavior.

Questions confidence attempts to answer:

* Should execution continue?
* Should more investigation occur?
* Should execution stop?

Confidence is evidence-backed, not certainty-backed.

Low confidence is a valid result.

"I do not have enough evidence yet" is correct behavior.

---

## Benchmark System

Benchmarks measure capability and recovery.

Current benchmark classes:

* workflow benchmarks
* recovery benchmarks

Purpose:

* identify recurring failure modes
* validate instrumentation
* measure recovery quality

Benchmarks provide telemetry inputs.

They do not justify capabilities by themselves.

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

## Telemetry Validation for Search Expansion

Before justifying a new search capability (such as `find()` expansion, semantic search, symbol indexing, AST search, or dependency graphs), the system must validate the need through the following specific telemetry filters.

**Status: These metrics are aspirational; current replay events do not capture them.**

### 1. Search Success Rate
Track:
* `grep_attempts` – Number of grep tool invocations
* `grep_success` – Greps returning ≥1 result
* `grep_zero_hit` – Greps returning 0 results → `InsufficientEvidence`

Current reality: Replay captures `execution_path`, `outcome`, and `recovery_metrics.evidence_found`, but not per-tool attempt counts. The `EvidenceStore` tracks whether grep produced results via `grep:results` facts, but not aggregate statistics.

If the dominant failure pattern is:
```text
grep
  ↓
0 results
  ↓
InsufficientEvidence
```
then search coverage is confirmed as the bottleneck.

### 2. Evidence Quality
We must distinguish between search failure (no results) and interpretation/quality failure (wrong results).
Currently measurable via:
* `evidence_found = true` & `outcome = Success` → Evidence used correctly
* `evidence_found = true` & `outcome = UserRejected` & `user_corrected_goal = true` → Evidence found but interpretation failed

If evidence is found but the goal is corrected by the user, it is an interpretation/evidence-quality failure, not a search capability issue.

### 3. Files Examined Distribution
Measure search breadth vs. search depth:
* `avg_files_examined` – Derived from `grep:results` fact counts across sessions
* `max_files_examined` – Max grep hits per CodebaseQuery event

Signals:
* **Healthy:** 1 file examined → high confidence → success.
* **Too Broad:** 25 files examined → low confidence → `InsufficientEvidence` (indicates filtering/ranking needs work, not new search engines).

**Status:** Requires enhancement to `EvidenceStore` to track file counts per grep invocation.

### 4. Query Rewording Rate
Watch for:
```text
User prompt (fail with InsufficientEvidence)
  ↓
User rephrases (success)
```
High query rewording indicates a keyword/discoverability problem rather than a lack of underlying search capability.

**Note:** Current replay does not correlate queries within a session. This requires session-level evidence linkage to detect query A → failure → query B → success patterns.

### 5. Search Recovery Clusters
Classify all `InsufficientEvidence` outcomes into one of four clusters:
1. **No matches:** Search coverage issue.
2. **Wrong matches:** Ranking/relevancy issue.
3. **Too many matches:** Noise filtering issue.
4. **Low confidence after matches:** Interpretation issue.

### Decision Rule
A capability is earned only when replay data shows a stable failure cluster that existing recovery strategies cannot overcome.

Quantitative threshold example:
```text
CodebaseQuery events: 500
Success: 58%
InsufficientEvidence: 35%
  ↳ of which 80% = zero matches (confirming the capability is earned)
```
If instead:
* `evidence_found = true` rate is high (e.g., >90%)
* Outcome is `UserRejected` or `user_corrected_goal = true`
then interpretation/relevancy is the bottleneck, not search capability.

---

## Verification Checklist for Search Metrics

Before claiming a search capability is justified, verify:

### 1. Every metric is replay-backed
For each metric in AGENTS.md, confirm there is an actual replay field or derivable query.

Example:
```text
grep_attempts
grep_success
grep_zero_hit
```
If those are not currently stored or derivable from replay events, the doc is ahead of reality.

### 2. Evidence Quality is measurable
This one is easy to describe and hard to measure.

```text
EvidenceFound
AnswerAccepted
```

How is "AnswerAccepted" determined?

If it isn't currently observable, reword it as:
```text
EvidenceFound
UserCorrectedGoal
Outcome
```
Those already exist in your telemetry model.

### 3. Query Rewording Detection
Make sure a future implementation can actually connect:
```text
Query A
↓
InsufficientEvidence
↓
Query B
↓
Success
```
to the same session.

If replay cannot correlate those events today, document it as a future measurement target rather than an active metric.

### 4. Keep the Decision Rule Quantitative
The strongest part is the capability gate.

I'd make it explicit:
```text
Advanced search capability is not justified by
individual failures.

A capability is earned only when replay data
shows a stable failure cluster that existing
recovery strategies cannot overcome.
```
That aligns with the rest of the document.

### 5. One Metric I Would Add
Right now you're measuring:
```text
Can search find evidence?
```

I'd also measure:
```text
Was evidence actually used?
```

Something like:
```text
EvidenceFound
↓
Success

vs

EvidenceFound
↓
UserCorrectedGoal
```
because your recent failures were not search failures.

The system found information but answered from model priors instead of repository evidence.

That distinction is important.

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

Task Pipeline authority:

* code-change execution

This split documents reality.

No stable failure cluster has justified unification.

---

### Partial Replay Coverage

Replay is authoritative for instrumented paths.

Not every execution path participates equally:

* direct service commands
* shell escapes
* bypass routes

This is known structural asymmetry.

No dominant failure trend has yet been attributed to it.

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

## Current Focus

Current questions:

```text
Are existing capabilities being reached?

Are existing capabilities being exercised?

Are outcomes improving as evidence accumulates?
```

Primary artifacts:

* replay-backed observations
* execution-path distributions
* outcome distributions
* confidence calibration
* trust metrics

Decision rule:

```text
No capability expansion without telemetry justification.
```

---

## Capability Discoverability

Capability availability and capability usage are different measurements.

The system contains:

* repository investigation
* planning
* verification
* replay
* confidence evaluation
* benchmarking
* codebase exploration

The active question is not whether these capabilities exist.

The active question is whether users naturally reach them.

Tracking pattern:

```text
Codebase-oriented prompt
  ↓
ExecutionPath
  ↓
Observed authority
```

Example:

```text
ChatOnly
Engine
TaskPipeline
DirectService
```

If discoverability remains low under natural traffic, the bottleneck is surface behavior rather than capability absence.

Execution-path telemetry exists specifically to measure this distinction.

---

## Investigation Is Not Conversation

Cursor may investigate before answering.

Investigation output is not conversation output.

Normal mode prioritizes answers.

Debug mode prioritizes visibility.

Replay prioritizes evidence.

### Visibility Layers

| Layer               | Normal  | Inspect | Debug   | Replay/Dashboard |
| ------------------- | ------- | ------- | ------- | ---------------- |
| Answer              | visible | visible | visible | visible          |
| Spinner             | visible | visible | visible | n/a              |
| Investigation summary | hidden | visible | visible | visible        |
| Pipeline sections   | hidden  | hidden  | visible | visible          |
| Reasoning steps     | hidden  | hidden  | visible | visible          |
| Tool traces         | hidden  | hidden  | visible | visible          |
| Evidence collection | hidden  | hidden  | visible | visible          |
| Full telemetry      | n/a     | n/a     | n/a     | stored           |

### Repository Awareness

Cursor automatically establishes:

* current working directory
* repository root
* active branch
* repository status
* build artifacts

Repository context is operational state.

It informs answers without being printed unless relevant.

---

## Guiding Principle

The framework exists to answer:

```text
What repeatedly fails?
```

And equally important:

```text
What does not need to be built?
```

The goal is not capability accumulation.

The goal is evidence-backed decisions.
