# Cursor Engineering Guide

This document defines the operational rules for every engineering cycle.

It is intentionally concise.

Long-form discussions belong under `docs/`.

---

# Mission

Build a deterministic investigation engine that helps developers understand a codebase with evidence before opinion.

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

Planner

↓

Investigation State

↓

Tool Selection

↓

Repository Evidence

↓

Planner

↓

Enough evidence?

No
↓

Continue investigation

Yes
↓

AI synthesis

↓

User
```

---

# Investigation State

The planner continuously maintains:

* Known facts
* Unknown facts
* Active hypotheses
* Rejected hypotheses
* Evidence collected
* Evidence still required
* Current confidence

The investigation state is authoritative.

---

# Intelligence Ownership

Planner

* Investigation strategy
* Tool selection
* Recovery
* Confidence
* Completion

Tools

* Produce deterministic evidence

AI

* Explain verified evidence
* Never invent missing facts

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

# Completion Rule

Do not answer because tools finished.

Answer only when the planner concludes that sufficient evidence has been collected.

If evidence remains insufficient:

State what is still unknown.

---

# Recovery Rule

If confidence is low:

Investigate again.

Recovery is preferred over early completion.

---

# Transparency

Expose investigation progress.

Example:

* Classifying intent
* Searching filenames
* Ranking candidates
* Reading implementation
* Collecting evidence
* Synthesizing answer

Never hide investigation steps.

---

# Current Engineering Phase

Level 2

Planner Investigation

Mission:

Teach the planner to investigate before answering.

Primary objective:

Improve planner behaviour rather than adding capabilities.

---

# Success Criteria

Search Quality

* ≥95% architecture query success
* ≤4 files read on average
* Grep fallback ≤20%

Planner Quality

* Planner Recovery Rate
* Premature Stop Rate
* Investigation Revision Count
* Evidence Completeness
* Confidence Calibration

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

Evidence drives architecture.

Not ideas.

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

One planner.

Many deterministic tools.

---

# Documentation

Operational rules:

AGENTS.md

Engineering:

docs/engineering/

Telemetry:

docs/telemetry/

Architecture:

docs/architecture/

Proposals:

docs/proposals/

Release:

docs/release/

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
