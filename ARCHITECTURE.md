# Architecture

Cursor is a terminal-native investigation engine.

Its architecture is centered around a deterministic planner that investigates repositories, gathers evidence, recovers when evidence is insufficient, and only then produces answers.

The system is intentionally layered so that investigation, synthesis, presentation, and telemetry remain independent.

---

# High-Level Architecture

```text
User
 │
 ▼
Session
 │
 ▼
ExecutionEngine
 │
 ▼
Planner
 │
 ▼
Investigation
 │
 ▼
InvestigationSession
 │
 ├── AIService
 ├── Replay
 ├── UI
 ├── JSON Output
 └── /inspect
```

The planner is the architectural center of the system.

Everything else either supports investigation or consumes its results.

---

# Repository Layout

| Module      | Location        | Responsibility                                       |
| ----------- | --------------- | ---------------------------------------------------- |
| `app/`      | `src/app/`      | Session lifecycle, command routing, interaction flow |
| `core/`     | `src/core/`     | Runtime state, planner domain types, shared models   |
| `ui/`       | `src/ui/`       | Terminal rendering and conversation presentation     |
| `services/` | `src/services/` | Investigation capabilities and integrations          |
| `utils/`    | `src/utils/`    | Shared helpers and low-level utilities               |

---

# Runtime Flow

```text
main.cpp
    │
    ▼
Session
    │
    ▼
ExecutionEngine
    │
    ▼
Planner
    │
    ▼
Investigation
    │
    ▼
Answer
```

The Session owns interaction.

The ExecutionEngine owns planning.

The Planner owns investigation.

The AI produces explanations from verified evidence.

---

# Execution Engine

The ExecutionEngine coordinates every investigation.

Responsibilities:

* intent classification
* investigation planning
* tool selection
* recovery decisions
* confidence evaluation
* completion decisions
* answer synthesis

It never directly assumes facts.

Facts must be discovered.

---

# Planner

The planner is responsible for deciding what knowledge is still missing.

Its responsibilities are:

* classify user intent
* determine investigation strategy
* select tools
* evaluate gathered evidence
* decide whether recovery is necessary
* determine when sufficient evidence exists
* authorize answer generation

The planner's primary responsibility is **not choosing tools**.

Its primary responsibility is deciding **what it still needs to know**.

---

# Investigation Pipeline

The investigation pipeline is deterministic.

```text
Goal
 │
 ▼
Intent Classification
 │
 ▼
Investigation Planning
 │
 ▼
Select Tool
 │
 ▼
Execute Tool
 │
 ▼
Collect Evidence
 │
 ▼
InvestigationSession
 │
 ▼
Confidence Evaluation
 │
 ├── Recover
 │
 └── Complete
        │
        ▼
Answer Synthesis
```

Each stage has one responsibility.

---

# Planner Recovery

Recovery allows investigations to continue when evidence is insufficient.

Recovery activates when:

* confidence falls below threshold
* expected evidence is missing
* only declarations were found
* investigation terminates prematurely
* tool exhaustion occurs before sufficient evidence

Recovery strategies include:

* broaden search
* implementation lookup
* grep escalation
* discovery search
* alternate evidence path

Recovery attempts are recorded to prevent infinite loops.

---

# InvestigationSession

InvestigationSession is the canonical investigation artifact.

It represents everything the planner learned while answering a question.

It replaces ad-hoc evidence reconstruction throughout the system.

Typical contents include:

* investigation goal
* conclusion
* confidence
* duration
* files examined
* tools executed
* evidence collected
* reasoning steps
* recovery attempts
* completion status

InvestigationSession is the shared language between architectural layers.

---

# Investigation Consumers

InvestigationSession is consumed by multiple systems.

```text
ExecutionEngine
        │
        ▼
InvestigationSession
        │
        ├── UI
        ├── Replay
        ├── JSON
        ├── /inspect
        └── Telemetry
```

No consumer reconstructs investigation state independently.

---

# AI Service

AIService performs synthesis.

It does not investigate repositories.

Its responsibility is to transform planner-approved evidence into human-readable explanations.

```text
ExecutionEngine
        │
        ▼
InvestigationSession
        │
        ▼
AIService
        │
        ▼
Answer
```

Investigation and explanation remain intentionally separated.

---

# Runtime State

SessionState owns runtime information.

Examples include:

* active model
* conversation history
* planner state
* recovery metrics
* trust metrics
* last_investigation

The last investigation survives long enough to support:

* `/inspect`
* replay
* structured JSON output

Runtime state remains provider-independent.

---

# Model Catalog

ModelCatalog is the single source of truth for model metadata.

It defines:

* providers
* models
* capabilities
* pricing
* categories
* display metadata

Adding new providers should require catalog changes rather than architectural changes.

---

# Replay

Replay records planner behavior.

Responsibilities include:

* append-only event history
* InvestigationSession persistence
* planner recovery recording
* confidence evolution
* trust metrics
* execution timing
* schema versioning

Replay is the architectural source of truth for historical investigations.

---

# Telemetry

Telemetry measures planner quality rather than interface activity.

Key metrics include:

* investigation duration
* files examined
* tools executed
* recovery attempts
* grep fallback rate
* planner confidence
* completion decisions

Metrics are derived from InvestigationSession rather than reconstructed afterward.

---

# Tool Execution

Every capability returns a structured ToolResult.

Typical fields include:

* tool
* arguments
* stdout
* stderr
* exit code
* execution duration

ToolResults feed InvestigationSession.

They are not exposed directly to normal users.

---

# User Interface

The UI is intentionally lightweight.

Responsibilities include:

* conversation rendering
* investigation progress
* answer presentation
* optional investigation inspection

The UI never performs planning.

It simply presents planner decisions.

---

# Design Principles

## Evidence Before Opinion

Repository evidence always takes priority over model assumptions.

---

## Planner First

Planning determines investigation quality.

Tool execution follows planner decisions.

---

## Separation of Concerns

* Session manages interaction.
* Planner manages investigation.
* AI manages explanation.
* UI manages presentation.
* Replay manages historical evidence.

---

## Deterministic Investigation

Repository investigation should be reproducible.

Equivalent questions should produce equivalent investigation behavior.

---

## Recovery Before Failure

The planner should recover when evidence is weak instead of answering prematurely.

---

## Shared Investigation State

InvestigationSession is the canonical representation of investigation.

Every architectural consumer reads the same artifact.

---

# Architectural Stability

Architecture evolves only when supported by production evidence.

New layers require measurable improvement in:

* correctness
* efficiency
* transparency
* maintainability

Speculative abstractions are intentionally avoided.

---

# Current Maturity

**Level 2 — Planner Recovery**

Current architectural focus:

* planner reasoning
* investigation quality
* recovery behavior
* evidence completeness
* confidence-driven completion

Future work should strengthen planner intelligence before introducing autonomous investigation units or multi-planner orchestration.
