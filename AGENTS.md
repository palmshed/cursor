# Cursor Engineering Guide

This document defines the operational rules for every engineering cycle.

It is intentionally concise.

Long-form discussions belong under `docs/`.

---

# Mission

Cursor is an AI coding agent.

The planner owns investigation.

The AI owns synthesis.

Tools produce deterministic evidence.

The planner is responsible for deciding what knowledge is still missing.

The AI is responsible for explaining verified evidence.

---

# Core Principle

> Evidence before opinion.

Every answer must be grounded in repository evidence.

Never compensate for missing evidence with speculation.

---

# Planner Invariant

Before every investigation step, the planner must determine:

* What is already known?
* What is still unknown?
* What evidence is required next?

The next tool is selected because it is expected to reduce uncertainty.

---

# Runtime Architecture

```
User Question

↓

Goal Understanding

↓

Information Requirements

↓

Planning

↓

Evidence Collection

↓

EvidencePackage

↓

Completion Gate

↓

AI Synthesis (via Formatter)

↓

User
```

Investigation, evidence packaging, and synthesis are intentionally separated.

---

# Investigation Lifecycle

Every investigation produces an `InvestigationSession` — the canonical record of what the planner learned.

The session is consumed by:

* **AI synthesis** — transformed into a natural-language answer via the `Formatter`
* **`/inspect`** — displayed as evidence summary
* **Replay** — serialized for historical analysis
* **JSON output** — machine-readable diagnostics
* **Telemetry** — planner quality metrics

No consumer reconstructs investigation state independently.

---

# EvidencePackage

Tools produce `ToolResult` objects.

The planner groups them into an `EvidencePackage` containing:

* files examined
* symbols found
* grep matches
* git/CI output
* confidence scores

The `Formatter` converts `EvidencePackage` into the AI prompt.

Prompt changes never modify the planner or evidence collection.

---

# Completion Gate

The planner does not answer because tools finished.

It answers only when mandatory evidence requirements are satisfied.

Requirements are declared per goal type:

* `CommitHistory` requires `git log`
* `GitStatus` requires `git status`
* `CodebaseQuery` requires `FileSearch` + `FileContent`
* `ArchitectureReview` requires `FileSearch` + `FileContent` + `Discovery`

If mandatory evidence is missing, the planner continues investigating.

---

# Confidence

Confidence is a health metric, not a gate.

It measures how well evidence matches the expected profile for the goal type.

The confidence model:

* Groups evidence by category (search, read, git, discovery, ci, verification)
* Takes the strongest score per category
* Applies category weights
* Awards a convergence bonus when independent categories agree on the same target

Confidence values are:

* Internal to the planner
* Never exposed in normal output
* Visible only through `/inspect`, debug mode, and JSON diagnostics

---

# Recovery

Recovery activates when:

* confidence falls below the mid-loop threshold (0.2) during a primary tool
* completion is satisfied but confidence is below the post-completion threshold (0.5)

Recovery strategies include:

* broaden search (grep fallback)
* implementation lookup (find --impl)
* discovery analysis
* alternate evidence path

After recovery, the loop continues with the improved evidence state.

Recovery never terminates the investigation prematurely.

---

# Answer Finalization

When completion is satisfied:

1. The planner builds the `EvidencePackage` from tool results
2. The `Formatter` constructs the AI prompt (evidence only, no planner metadata)
3. The AI synthesizes a natural-language answer
4. Investigation details are suppressed in normal output

The user never sees:

* Tool calls or raw tool output
* Confidence values or calibration breakdown
* Planner state or recovery decisions
* "I'll check..." or "Preparing..."

These are available through `/inspect` and debug mode.

---

# Retrieval Pipeline

Always prefer deterministic retrieval.

```
Intent

↓

Filename lookup

↓

Symbol lookup

↓

Reference lookup

↓

Directory-aware ranking

↓

Read

↓

Synthesis
```

Broad grep is the last resort.

---

# Goal Understanding

The planner classifies user intent before selecting tools.

Classification is not keyword matching — it maps the user's question to an information need.

Supported goal types:

* `CommitHistory` — git log, status, diff
* `CodebaseQuery` — find, grep, read
* `CodebaseOverview` — discovery, read
* `ArchitectureReview` — cross-file structural analysis
* `CICheck` — GitHub Actions investigation
* `GitHubInvestigation` — CI run diagnostics
* `CodeChange` — edit with plan+verify
* `GeneralChat` — conversation only
* `SessionState` — runtime configuration queries

Each goal type declares mandatory evidence.

The planner must collect all mandatory evidence before completion.

---

# Intelligence Ownership

**Planner**

* Investigation strategy
* Tool selection
* Recovery decisions
* Confidence evaluation
* Completion decisions

**Tools**

* Produce deterministic evidence
* Never make planning decisions

**AI**

* Explain verified evidence
* Never invent missing facts

**Formatter**

* Transform `EvidencePackage` into AI prompt
* Control answer structure per goal type

---

# Transparency

Expose investigation progress to the developer, not to the user.

Normal output:

```
Investigating repository…

✓ Ready to explain

[synthesized answer]
```

Developer access:

* `/inspect` — evidence summary
* Debug mode — planner decisions, confidence, recovery
* Replay — full investigation history
* JSON output — machine-readable diagnostics

---

# Current Engineering Phase

**Level 2 — Planner Recovery and Answer Finalization**

Level 2 is feature-complete but not yet closed.

Level 3 must not begin until all Level 2 acceptance criteria have been satisfied.

---

# Success Criteria

**Search Quality**

* ≥95% architecture query success
* ≤4 files read on average
* Grep fallback ≤20%

**Planner Quality**

* Planner Recovery Rate
* Premature Stop Rate (target: 0%)
* Investigation Revision Count
* Evidence Completeness
* Confidence Calibration

**Answer Quality**

* No raw tool calls in answers
* No confidence values in normal output
* No planner metadata leaked to users
* Concise, evidence-grounded answers
* All goal types terminate with synthesized answers

---

# Reality Check Gate

Every proposed capability must answer:

* Which production failures require this?
* Which telemetry supports it?
* Which metric should improve?
* Why is the current capability insufficient?
* What is the stop condition?

No implementation begins without these answers.

---

# Senior Software Engineer Behaviour

The system should:

* Read only necessary files
* Explain why files were selected
* Declare uncertainty
* Avoid unnecessary searches
* Ground conclusions in evidence
* Prefer deterministic behaviour

---

# Capability Growth

New capabilities are added only when telemetry demonstrates a real limitation.

Evidence drives architecture. Not ideas.

---

# Investigation Tasks

The planner may dispatch investigation tasks.

Tasks do not own planning.

Tasks do not own memory.

Tasks return evidence.

The planner remains the single decision-maker.

---

# Subagents

Subagents remain frozen.

Promotion from investigation tasks to subagents requires telemetry demonstrating that planner quality has saturated and independent parallel investigations produce measurable benefit.

Until then:

One planner. Many deterministic tools.

---

# Documentation Map

| Document | Audience | Content |
|---|---|---|
| `AGENTS.md` | Contributors | Engineering rules, invariants, success criteria |
| `ARCHITECTURE.md` | Engineers | Runtime architecture, component relationships, data flow |
| `DESIGN.md` | Product | User experience philosophy, interaction model, visibility layers |

`docs/engineering/` — Engineering deep-dives (confidence calibration, planner recovery, acceptance criteria)

`docs/architecture/` — Architecture decisions (implementation audit, review records)

`docs/telemetry/` — Metrics and measurement (capacity review, human evaluation)

---

# Rule

Correctness

↓

Efficiency

↓

Transparency

↓

Communication

↓

Autonomy

Never reverse this order.
