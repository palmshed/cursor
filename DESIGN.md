# Cursor Design

## Philosophy

Cursor is a terminal-native AI coding agent.

Its interface is optimized for conversation while its planner performs evidence-driven investigations behind the scenes.

Evidence is collected before synthesis, allowing the agent to explain, edit, and operate on a codebase with deterministic investigation rather than assumption.

The user should experience a fast, quiet, evidence-grounded conversation while the planner performs the necessary investigation behind the scenes.

The interface exists to reduce cognitive load.

Every visible element must help the user answer the current question.

If an element does not improve understanding, remove it.

---

# Design Principles

## Conversation First

Conversation is always the primary experience.

Users come to Cursor for answers, not execution logs.

The interface should always prioritize readability over operational visibility.

---

## Investigation Produces Answers

Answers should be grounded by investigation.

Investigation is not an optional feature.

It is the planner's primary responsibility.

Users should normally see the conclusion, not the investigation.

---

## Quiet by Default

Avoid persistent:

* dashboards
* panels
* counters
* badges
* feature inventories
* telemetry
* execution logs

The interface should remain visually calm.

---

## Progressive Disclosure

Advanced information appears only when requested.

Normal conversation remains concise.

Additional evidence is available through explicit interaction.

Examples:

* `/inspect`
* debug mode
* replay
* validation
* benchmark tools

---

## Terminal Native

Cursor should feel like software built for terminals.

Prefer:

* text
* whitespace
* alignment
* lightweight progress

Avoid:

* heavy borders
* decorative boxes
* artificial windows
* GUI-style layouts

---

## Planner-Centric UX

The planner owns the investigation.

The planner decides:

* whether enough evidence exists
* whether more investigation is needed
* when recovery should occur
* when answering is appropriate

The UI reflects planner decisions without exposing planner internals.

---

# User Experience Layers

## Normal Conversation

Users ask questions.

Cursor investigates.

Cursor answers.

Example:

```text
> explain the authentication system

cursor

Investigating repository…

✓ Ready to explain

Authentication is centered around TokenManager...
```

The investigation disappears once the answer begins.

No tool calls, confidence values, or planner decisions appear in normal output. The `Formatter` strips planner metadata before AI synthesis.

---

## Investigation Available

When an investigation completes, users may inspect it.

Example:

```text
✓ Ready to explain (press i for details)
```

or

```text
Use /inspect to view the investigation.
```

No additional information is shown unless requested.

---

## Investigation Inspection

Inspection exposes how the answer was produced.

Typical contents:

* investigation goal
* conclusion
* confidence
* duration
* files examined
* tools used
* evidence summary
* reasoning steps
* recovery attempts

Inspection explains the investigation.

It never exposes raw implementation details unnecessarily.

---

## Debug Mode

Debug mode exists for engineering.

It may display:

* planner decisions
* recovery attempts
* tool routing
* execution paths
* confidence transitions

Debug mode is intentionally verbose.

Normal users should never need it.

---

## Replay

Replay focuses on validation.

Replay exposes:

* planner behavior
* investigation history
* telemetry
* recovery events
* timing
* confidence evolution

Replay exists for improving the system.

---

# Startup

Startup should remain immediate.

```text
▌ CURSOR
```

No splash screens.

No ASCII art.

---

# Ready State

```text
▌ CURSOR
offline · qwen2.5-coder
```

The model information is contextual.

It should never dominate the interface.

---

# Prompt

```text
> _
```

Nothing more.

The prompt should remain lightweight.

---

# Investigation Feedback

Investigation progress communicates intent rather than implementation.

Good:

```text
Investigating repository…

Reading implementation…

Checking related files…

Verifying evidence…
```

Avoid:

```text
Running grep

Tool #3

read()

find()

confidence = 0.62
```

Those belong in debug mode.

---

# Planner Recovery

Investigations are not always linear.

The planner may determine that additional evidence is required.

Example:

```text
Investigating repository…

Reading implementation…

Need more evidence…

Checking related implementation…

Verifying findings…
```

Recovery should feel like natural investigation rather than failure.

---

# Long Investigations

Long investigations should update in place.

Example:

```text
Investigating repository…
⠋
```

Never create multiple progress messages.

When complete:

```text
✓ Ready to explain
```

The progress indicator disappears.

---

# Review Operations

Architecture reviews naturally expose more progress.

Example:

```text
Reviewing architecture…

✓ Structure

✓ Dependencies

✓ Execution flow

✓ Recovery logic

Architecture review complete.
```

Review operations expose progress.

Conversation exposes answers.

---

# Shell Execution

Native commands should never overwhelm the interface.

The execution engine enforces:

* timeout protection
* output limits
* background execution
* progress summaries

Users see investigation progress.

Developers may inspect execution details separately.

---

# Visibility Model

## Normal

Answer focused.

User sees: synthesized answer.

## Investigation Available

Inspection optional.

User sees: prompt to press `i` or use `/inspect`.

## Inspect

Evidence focused.

Evidence summary, files examined, confidence, timing, tools used.

## Debug

Planner focused.

Recovery decisions, confidence transitions, tool routing, execution paths.

## Replay

Telemetry focused.

Full investigation history, timing, planner behavior, recovery events.

Each layer reveals additional information without cluttering the previous one.

No layer exposes raw tool output or planner metadata in the normal conversation view.

The boundary between layers is enforced by the `Formatter`, not by convention.

---

# Answer Finalization

The system must never display:

* tool calls (`find()`, `read()`, `grep()`)
* confidence values or calibration breakdown
* planner state (mid-loop recovery, completion gates)
- "I'll check..." or "Preparing..."

These are available through `/inspect` and debug mode.

Normal output is:

```text
Investigating repository…

✓ Ready to explain

[synthesized answer grounded in evidence]
```

This is enforced architecturally (not by UI convention).

The planner passes evidence to the `Formatter`, which produces a clean evidence summary. The AI receives only formatted evidence — it has no access to raw session state.

---

# Repository Awareness

Cursor automatically understands:

* repository root
* working directory
* branch
* build artifacts
* project layout

Repository awareness informs planner decisions.

It is not printed unless relevant.

---

# Capability Discoverability

Capabilities should emerge naturally.

Avoid:

* capability catalogs
* startup menus
* feature walls

Prefer:

* conversation
* contextual hints
* progressive disclosure

The planner should make existing capabilities feel discoverable through interaction rather than documentation.

---

# Status Information

Show concise status once.

Example:

```text
offline · qwen2.5-coder
```

Avoid repeating provider, model, and execution state throughout the conversation.

---

# Accessibility

The interface must remain usable in:

* monochrome terminals
* narrow terminals
* screen readers
* low-color environments

Meaning must never depend solely on color.

---

# Design Constraints

Do not expose:

* planner internals
* recovery logic
* tool routing
* telemetry
* confidence values
* raw tool output
* evidence_summary (as distinct from answer)

unless explicitly requested via `/inspect` or debug mode.

These constraints are enforced by the architecture — the `Formatter` strips all planner metadata before evidence reaches the AI, and the AI receives no access to raw session state.

Normal conversation remains answer-first.

---

# Success Criteria

A successful Cursor session looks like:

```text
▌ CURSOR
offline · qwen2.5-coder

> explain planner recovery

cursor

Investigating repository…

✓ Ready to explain (press i for details)

Planner recovery activates when the planner determines additional evidence is required before producing a trustworthy answer...

> _
```

The investigation remains mostly invisible.

The conversation remains the product.

Evidence is always available through `/inspect`.

The interface stays out of the user's way.

The answer contains no tool names, no confidence values, no planner metadata — only a clean, evidence-grounded explanation.
