# Cursor Design

## Philosophy

Cursor is a terminal-native AI coding agent.

The interface should be quiet, readable, and focused on conversation rather than chrome. Every element must justify its presence. If an element does not help the current task, it should be removed.

The design goal is not to build a dashboard. The design goal is to create a lightweight terminal conversation.

Two principles guide the interface:

**Investigation is not Conversation.**

Investigation exists to improve answers. The answer is the product.

**Capability exists ≠ Capability is discoverable.**

The interface should help users naturally reach existing capabilities without exposing internal machinery.

---

## Principles

### Conversation First

The conversation is the product.

Chat history and responses should occupy the majority of visual attention.

### Quiet by Default

Avoid:

* panels
* sidebars
* counters
* badges
* dashboards
* decorative separators
* repeated metadata

### Progressive Disclosure

Advanced functionality should appear only when needed.

Examples:

* command palette
* debug tools
* shell integration
* help menus

These should not remain visible during normal conversation.

### Terminal Native

The interface should feel like a terminal application, not a graphical application recreated in a terminal.

Prefer:

* text
* whitespace
* alignment

Over:

* boxes
* frames
* visual decoration

### Investigation Is Not Conversation

Investigation may occur before an answer is produced.

Users should not be exposed to implementation details unless they explicitly request them.

Normal mode prioritizes answers.

Debug mode prioritizes visibility.

Replay and dashboards prioritize evidence.

---

## Layout

### Startup

```text
▌ CURSOR
```

Simple and immediate.

No large ASCII art.

No splash screen.

### Ready State

```text
▌ CURSOR
offline · qwen2.5-coder:1.5b
```

The model line should be visually subdued.

It provides context without competing with conversation.

### First Prompt

Optional onboarding hints may appear before the first message:

```text
⌘P palette  :cmd  !shell  /help  /debug

> _
```

These hints disappear after the first submitted message.

### Conversation

```text
> hello

cursor

Hello! How can I assist you today?

> _
```

Rules:

* User messages are introduced by `>`
* Assistant responses are introduced by `cursor`
* Assistant labels are dim and lowercase
* Responses contain no decorative framing
* Conversation flows vertically

---

## Investigation Feedback

Cursor may investigate the repository before answering.

Users should receive lightweight progress feedback while investigation is active.

Normal mode:

```text
> explain the auth system

cursor

Investigating repository…
⠋
```

Followed by:

```text
cursor

The authentication flow is centered around TokenManager...
```

Rules:

* show intent, not tool names
* show progress, not telemetry
* remove progress indicators when the answer is ready
* investigation feedback should feel temporary

Prefer:

```text
Investigating repository…
Reading implementation…
Verifying findings…
Checking related files…
```

Avoid:

```text
Running grep
Running read
grep:results
read:results
tool #3 completed
```

Tool execution belongs to debug mode and replay, not normal conversation.

### Long Operations

For investigations longer than a few seconds:

```text
cursor

Investigating repository…
⠋
```

The spinner should update in place rather than creating new conversation entries.

When complete:

```text
cursor

Here's what I found...
```

The progress indicator disappears.

---

## Visibility Layers

### Normal Mode

Focus on answers.

```text
> find the cursor binary

cursor

The binary is `cursor-agent`.

Location:
- build/bin/cursor-agent
```

### Debug Mode

Focus on execution visibility.

```text
[ Repository Investigation ]
  - Searching repository
  - Reading implementation
  - Verifying findings

Evidence:
  build/bin/cursor-agent
  target name: cursor-agent
```

### Replay / Dashboard

Focus on evidence and telemetry.

May include:

* execution paths
* confidence values
* outcomes
* recovery metrics
* trust metrics

These views are operational tools, not conversation UI.

---

## Repository Awareness

Cursor should automatically understand:

* current working directory
* repository root
* active branch
* repository status
* available build artifacts

This context informs responses automatically.

Repository context should not be printed unless relevant to the answer.

Operational awareness is expected behavior, not user-facing output.

Good:

```text
The binary is `cursor-agent`.
```

Not:

```text
cwd: /Users/example/project
branch: main
repo root: ...
build artifacts: ...
```

unless those details directly answer the user's question.

---

## Input

```text
> _
```

The prompt should remain simple.

Avoid:

```text
[You]
Prompt >
□
```

The prompt should never resemble a button, checkbox, or form control.

---

## Update Menu

```text
Update available: v0.1.18 → v0.1.21

› Update now
  Later
```

Rules:

* single selection indicator
* no radio buttons
* no checkbox glyphs
* keyboard-first navigation

---

## Status Information

Current mode and model may be shown once near the top:

```text
offline · qwen2.5-coder:1.5b
```

Avoid repeating:

* mode
* provider
* model
* system state

throughout the conversation.

---

## Capability Discoverability

A capability existing internally does not mean users can naturally reach it.

The interface should help users discover capabilities through conversation rather than menus, dashboards, or feature inventories.

Avoid:

* persistent capability lists
* startup capability walls
* feature catalogs
* command encyclopedias

Prefer:

* natural language interaction
* contextual suggestions
* progressive disclosure

The goal is for users to reach capabilities naturally without needing to learn system internals.

---

## What Cursor Is Not

Cursor is not:

* a dashboard
* an IDE clone
* a monitoring console
* a metrics panel

Avoid introducing:

* token counters
* MCP counters
* LSP counters
* feature inventories
* enterprise status screens
* persistent help panels

unless explicitly requested.

---

## Accessibility

The interface must remain usable with:

* monochrome terminals
* low-color terminals
* small terminal windows
* screen readers

Meaning should not depend on color alone.

The interface should remain understandable when all styling is removed.

---

## Success Criteria

A successful Cursor session feels like:

```text
▌ CURSOR
offline · qwen2.5-coder:1.5b

> explain this code

cursor

Here's what the function does...

> _
```

The user focuses on the conversation.

The investigation remains invisible unless needed.

The interface stays out of the way.
