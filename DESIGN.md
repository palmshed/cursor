# Cursor Design

## Philosophy

Cursor is a terminal-native AI coding agent.

The interface should be quiet, readable, and focused on conversation rather than chrome. Every element must justify its presence. If an element does not help the current task, it should be removed.

The design goal is not to build a dashboard. The design goal is to create a lightweight terminal conversation.

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

### Input

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

## Update Menu

```text
Update available: v0.1.18 → v0.1.21

› Update now
  Later
```

Rules:

* Single selection indicator
* No radio buttons
* No checkbox glyphs
* Keyboard-first navigation

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

## Accessibility

The interface must remain usable with:

* monochrome terminals
* low-color terminals
* small terminal windows
* screen readers

Meaning should not depend on color alone.

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

The user should focus on the conversation, not the interface.
