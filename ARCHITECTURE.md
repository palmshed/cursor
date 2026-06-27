# Architecture

Cursor is a terminal-native AI coding agent.

The architectural center of the system is a deterministic planner that investigates repositories before answering, generating, reviewing, or modifying code.

Evidence is gathered into an `EvidencePackage` and delegated to the `Formatter` + AI for synthesis.

The system is intentionally layered so that investigation, packaging, synthesis, presentation, and telemetry remain independent.

---

# High-Level Architecture

```
User
 │
 ▼
Session
 │
 ▼
ExecutionEngine (Planner)
 │
 ▼
Evidence Collection (Tools)
 │
 ▼
EvidencePackage
 │
 ▼
Completion Gate
 │
 ▼
Formatter
 │
 ▼
AI Synthesis
 │
 ▼
User
```

The planner is the architectural center.

Everything else either supports evidence collection or consumes its results.

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

```
main.cpp
    │
    ▼
Session
    │
    ▼
ExecutionEngine (Goal → Plan → Collect → Gate → Package → Format → Synthesize)
    │
    ▼
User
```

The Session owns interaction.

The ExecutionEngine owns planning, evidence collection, completion, and synthesis orchestration.

The planner (within ExecutionEngine) owns investigation strategy.

The Formatter prepares evidence for AI consumption.

The AI produces explanations from verified, formatted evidence.

---

# Execution Engine

The ExecutionEngine coordinates every investigation.

Responsibilities:

* intent classification
* investigation planning
* tool selection and execution
* recovery decisions
* confidence evaluation
* completion decisions
* evidence packaging
* synthesis orchestration

It never directly assumes facts.

Facts must be discovered.

---

# Planner

The planner is the subsystem within ExecutionEngine responsible for deciding what knowledge is still missing.

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

```
Goal
 │
 ▼
Intent Classification
 │
 ▼
Investigation Planning
 │
 ▼
Select Tool → Execute Tool → Collect Evidence
 │                                           │
 └──(loop until completion or tool exhaustion)┘
 │
 ▼
Confidence Evaluation
 │
 ├── Recover (broaden search, alternate path)
 │
 └── Complete
        │
        ▼
EvidencePackage
        │
        ▼
Formatter
        │
        ▼
AI Synthesis
        │
        ▼
User
```

Each stage has one responsibility.

The recovery loop continues without breaking the primary tool sequence.

---

# EvidencePackage

EvidencePackage is the bridge between investigation and synthesis.

It is constructed by the planner after the completion gate is satisfied.

Contents:

* files examined (with relevant excerpts)
* symbols found
* grep matches
* git/CI output grouped by command
* confidence scores (per category and overall)
* mandatory evidence satisfaction status

EvidencePackage is **not** the same as `InvestigationSession`.

`InvestigationSession` records everything the planner did (including recovery attempts, confidence transitions, planner decisions).

`EvidencePackage` contains only what the AI needs to answer: the verified evidence.

The planner constructs `EvidencePackage` from the session at completion time.

---

# Formatter

The Formatter transforms `EvidencePackage` into an AI prompt.

Responsibilities:

* select evidence relevant to the goal type
* format evidence concisely (no planner metadata)
* construct a prompt structure suitable for the goal type
* strip internal bookkeeping (confidence values, recovery details, tool names)

The Formatter is the boundary that prevents planner internals from leaking into answers.

Prompt changes never modify the planner or evidence collection.

---

# Answer Finalization

Answer finalization is the process that keeps raw investigation details out of normal output.

Sequence:

```
1. Completion Gate satisfied
2. Planner constructs EvidencePackage from InvestigationSession
3. Formatter converts EvidencePackage to a clean evidence summary
4. AI receives evidence summary (not raw tool output)
5. AI synthesizes natural-language answer
6. User sees synthesized answer (no tool calls, no confidence, no planner state)
```

This replaces the earlier design where `summary` was used for both AI synthesis and debug inspection. Now `evidence_summary` serves AI synthesis; `summary` serves planner debug and `/inspect`.

---

# Planner Recovery

Recovery allows investigations to continue when evidence is insufficient.

Recovery activates when:

* confidence falls below mid-loop threshold (0.2) during a primary tool
* completion is satisfied but confidence is below post-completion threshold (0.5)

Recovery strategies include:

* broaden search (grep fallback)
* implementation lookup (find --impl)
* discovery analysis
* alternate evidence path

After recovery, the loop continues with the improved evidence state.

Recovery never terminates the investigation prematurely.

Debug view (`/inspect`) exposes recovery attempts and their outcomes.

---

# InvestigationSession

InvestigationSession is the canonical record of what the planner learned during investigation.

It is the permanent, inspectable investigation artifact.

Contents:

* investigation goal
* conclusion
* confidence (per-category and overall)
* duration
* files examined
* tools executed
* evidence collected
* reasoning steps
* recovery attempts
* completion status

InvestigationSession is consumed by:

* **EvidencePackage** -- builds evidence for AI (not all session data)
* **`/inspect`** -- displays evidence summary to developers
* **Replay** -- serialized for historical analysis
* **JSON output** -- machine-readable diagnostics
* **Telemetry** -- planner quality metrics

No consumer reconstructs investigation state independently.

---

# Investigation Consumers

```
ExecutionEngine
        │
        ▼
InvestigationSession
        │
        ├── EvidencePackage (→ Formatter → AI → User)
        ├── UI (/inspect)
        ├── Replay
        ├── JSON
        └── Telemetry
```

Each consumer reads the same canonical artifact.

---

# AI Service

AIService performs synthesis.

It does not investigate repositories.

Its responsibility is to transform formatted evidence into human-readable explanations.

```
ExecutionEngine
        │
        ▼
EvidencePackage
        │
        ▼
Formatter
        │
        ▼
AIService (receives evidence summary, not raw session data)
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
* reference by next investigation

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
* answer quality (human evaluation)

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

Evidence summaries for AI strip tool names and execution details, keeping only extracted content (file paths, symbols, matches).

---

# User Interface

The UI is intentionally lightweight.

Responsibilities include:

* conversation rendering
* investigation progress
* answer presentation
* optional investigation inspection (`/inspect`)

The UI never performs planning.

It simply presents planner decisions.

Normal output shows only:

```
Investigating repository…

✓ Ready to explain

[synthesized answer]
```

Everything else is available through `/inspect`, debug mode, or replay.

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
* Formatter prepares evidence.
* AI manages explanation.
* UI manages presentation.
* Replay manages historical evidence.

---

## Deterministic Investigation

Repository investigation should be reproducible.

Equivalent questions should produce equivalent investigation behavior.

---

## Recovery Before Premature Answer

The planner should recover when evidence is weak instead of answering prematurely.

---

## Shared Investigation State

InvestigationSession is the canonical representation of investigation.

Every architectural consumer reads the same artifact.

---

## EvidenceContent ≠ SessionContent

`EvidencePackage` contains only verified evidence for synthesis.

`InvestigationSession` contains the full planner record.

Mixing them leaks planner internals into answers.

The `Formatter` is the explicit boundary.

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

**Level 2 -- Planner Recovery and Answer Finalization**

Current architectural focus:

* planner reasoning
* evidence collection and packaging
* recovery behavior
* evidence completeness
* confidence-driven completion
* clean answer synthesis (no planner metadata in output)

Level 2 is feature-complete but not yet closed.

Level 3 begins only when Level 2 acceptance criteria are satisfied.
