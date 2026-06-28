---
---

# Level 2 UX Review: Terminal Experience

**Date:** 2026-06-28  
**Scope:** Current terminal interaction from first-time developer perspective  
**Method:** Source code audit of all user-facing strings, ANSI sequences, keyboard handling, terminal I/O, and interaction flow  
**Evaluator:** Static analysis only -- no live runtime testing

---

## 1. Conversation Flow

### Startup

The startup sequence proceeds as:

```
▌ CURSOR

[model selection cascade, 2-3 interactive menus]

  local · ollama · llama3.2:3b
```

**Observation:** The model selection is 2-3 screens deep. A first-time user sees a logo, then immediately a menu screen, then potentially another menu, then a third before they can type anything. This is 3-5 terminal clears/interactions before the first `>` prompt appears.

The status line (`local · ollama · llama3.2:3b`) is greyed out (`\033[90m`). It conveys model identity but not much else.

### First Prompt

```
> _
```

A bare `>` with a cursor. No welcome message, no hint about available commands, no example prompt. The user must guess what to type.

### Investigation

```
Investigating repository...
```

Printed once. Tools execute silently. If the investigation uses multiple tools (find, grep, read, git), the user sees no intermediate progress -- only the initial message. If the investigation is short, this message appears and disappears in under a second, which may feel like a flash rather than feedback.

Tool-specific labels exist (`Locating files...`, `Checking symbols...`, `Reading implementation...`) but are not printed -- the implementation gates all subsequent messages behind the first `current_section_` check. Only the first distinct label is shown.

**Effect:** A user who waits more than a few seconds sees only `Investigating repository...` with no indication of what is happening or whether progress is being made.

### Completion

```
✓ Investigation complete

▶ Ready to explain

```

The checkmark and play-button arrow signal readiness. The double-newline before the answer creates visual separation.

**Observation:** `▶ Ready to explain` is a two-step transition that adds latency. The user reads "Investigation complete," then "Ready to explain," then the answer appears. The AI synthesis typically takes <1s, so the play button reads as a pause rather than useful information.

### Answer

```
cursor

[formatted answer]
```

AI response is prefixed with `cursor` in dim text, then the answer rendered with markdown coloring. The formatting is functional: headers bold, code yellow, code blocks syntax-highlighted.

### Follow-up

The prompt `> ` returns immediately after the answer. There is no visual separation between the answer and the next prompt. On a terminal with limited scrollback, the conversation history collapses upward quickly.

### Post-Answer Inspect Window

```
Press I to inspect evidence
```

Greyed out, overwritten on the same line, stays for 3 seconds. If the user presses `i`, `/inspect` output appears. If ignored, the line clears and `> ` returns.

**Observation:** This is the most well-designed UX moment in the current flow. It is quiet, non-blocking, and provides direct access to investigation internals without documentation.

### Errors

```
cursor

Insufficient repository evidence to answer your question.
```

```
cursor

I couldn't complete the repository investigation for your question.
```

Clean, minimal. The "cursor" prefix may feel odd for error messages -- it establishes ownership but reads as attribution rather than the system speaking.

### Recovery

Silent in normal mode. The planner may attempt 1-3 additional tool calls without the user seeing anything change. The response time just appears longer.

### Review Commands

Mode toggles produce colored confirmation:

```
[debug] Verbose mode ON
```

```
[mode] Review mode: read-only, no writes
```

Clear, immediate feedback. Good pattern.

### /inspect

Produces a structured block:

```
▼ Investigation

Goal
  <goal description>

Reasoning
  <step 1>
  ...

Evidence
  ✓ <tool> <query>

Files examined
  • <file1>

Confidence
  <label> (<score>)

Duration
  <time>

Conclusion
  <conclusion>
```

Comprehensive but dense. A first-time user would likely not understand "Confidence: medium (0.562)" -- confidence labels are internal planner metrics exposed verbatim. The numerical score has no user-visible meaning.

---

## 2. Investigation Visibility

### Current Balance

Normal mode shows:

```
Investigating repository...
```

That is the **only** progress indicator during the entire investigation phase, regardless of whether the planner uses 1 tool or 8 tools. The implementation explicitly suppresses intermediate tool labels -- only the first `show_progress_section` call prints.

### Assessment

| Aspect | Rating | Notes |
|---|---|---|
| Noise level | Good | Very quiet, no tool internals leaking |
| Progress clarity | Poor | No indication of what is happening during long investigations |
| Time-to-first-feedback | Mixed | Flash appearance for short queries; stale message for long queries |
| Completion signal | Adequate | Clear but slightly verbose (2 lines where 1 would suffice) |

### "Ready to explain" Assessment

The two-line sequence `✓ Investigation complete` / `▶ Ready to explain` is better as a single line. These are two separate planner events (evidence collection done, then synthesis done) exposed as separate user messages. For the user, these are a single wait: "waiting for answer."

### Cognitive Load

Normal mode is well-calibrated: the user sees startup, investigation signal, answer. No tool details, no confidence values, no planner state. The leap from investigation-complete to answer is the right abstraction boundary.

The `Press I to inspect evidence` window is the correct place to reveal investigation detail -- optional, non-blocking, user-initiated.

---

## 3. Keyboard Experience

### Current Shortcuts

| Key | Context | Action | Assessment |
|---|---|---|---|
| Enter | Input | Submit | Natural |
| Up/Down Arrow | Input | History navigation | Natural |
| Left/Right Arrow | Input | Cursor movement | Natural |
| Backspace | Input | Delete before cursor | Natural |
| Single ESC | Input | Show hint | Unusual -- consumes ESC, delays arrow keys |
| Double ESC | Input | Return to model selection | Unusual -- most terminal apps use Ctrl+C, Ctrl+D, or a command |
| `i`/`I` | Post-answer (3s) | Inspect evidence | Good discoverability |
| Enter/ESC | Menus | Confirm/cancel | Standard |
| Up/Down Arrow | Menus | Navigate | Standard |
| `q`/`Q` | Dashboard | Quit | Standard |

### Issues

**ESC handling creates latency.** The line editor reads a raw ESC byte, then waits 50ms (`select` timeout) to determine whether it is the start of an arrow key sequence or a standalone ESC. This means every arrow key press has an invisible 50ms delay. For a keyboard-driven tool, this is noticeable friction.

**No Ctrl+C handling.** SIGINT is not caught. Ctrl+C terminates the process. In most terminal REPLs, Ctrl+C cancels the current input and returns to a fresh prompt. Here it kills the agent.

**No Ctrl+D handling.** In the TTY path, EOF (`std::cin.eof()`) is checked after `read_prompt()` returns, but Ctrl+D is not explicitly handled. In non-TTY mode, `std::getline` returns empty on EOF, which exits the loop.

**No Tab completion.** Tab inserts a literal tab character. Users familiar with terminals, shells, or REPLs will instinctively press Tab for completion or suggestion and get no response.

**No Ctrl+L / Ctrl+U / Ctrl+W.** Common readline shortcuts (clear screen, kill line, kill word) are absent. The line editor supports only basic arrow navigation and backspace.

**Double-ESC for model selection is the only way to change models.** There is no `/model` command or `Ctrl+X` shortcut. The user must double-press ESC, which is not discoverable without the hint.

---

## 4. Modern Terminal Capabilities

| Capability | Currently Supported | Effort | User Value | Recommendation |
|---|---|---|---|---|
| Clickable file paths (OSC 8) | No | Medium | High -- users can click file paths directly in /inspect output and error messages | Implement for any path printed in investigation or answer output |
| Clickable URLs | No | Low | Medium -- would improve /fetch output and GitHub links | Simple OSC 8 wrapping for any URL printed |
| Bracketed paste | No | Low | Medium -- prevents accidental multi-line paste execution | Enable on raw mode entry |
| True color (24-bit) | Partial (PINK only) | Medium | Low -- current 8-color scheme is functional | Not worth effort unless theme system expands |
| Alternate screen buffer | No | Medium | Medium -- prevents scrollback pollution from investigation output | Consider for menu/dashboard screens |
| Cursor save/restore | No | Low | Low -- cursor hide/show is sufficient | Not needed |
| Inline cursor updates | No | Low | Low -- current redraw approach works | Not needed |
| Keyboard enhancement (kitty protocol) | No | High | Low -- only benefits kitty terminal users | Not worth effort |
| Synchronized output | No | Low | Low -- reduces flicker in some terminals | Nice-to-have cleanup |
| SIGWINCH (terminal resize) | No | Low | Medium -- menus and dividers don't adapt to resize | Implement basic SIGWINCH handler to re-render status line |
| Terminal clipboard (OSC 52) | No | Medium | Low -- not core to agent interaction | Not needed |

### Top Terminal Capability Recommendation

**OSC 8 hyperlinks for file paths.** This is the highest-value missing capability. Every `/inspect` output, every "Files examined" section, every error message that mentions a file path -- all should be clickable. Implementation is adding `\033]8;;file://<abs-path>\a<display>\033]8;;\a` around file paths in output. This alone changes how developers interact with the tool.

---

## 5. Prompt Design

### Startup Text

| Element | Assessment |
|---|---|
| `▌ CURSOR` logo | Distinctive but takes 2 lines. A single-line header would be less intrusive. |
| Model selection cascade | Too many screens before first prompt. Could be a single `/model` command or config file. |
| Status line `local · ollama · llama3.2:3b` | Grey text is low-visibility. Status line lacks branch, mode, or file context. |

### The `>` Prompt

Bare `>` with no context. Compare to:

- Shell: `user@host:~/path $` (context: user, host, directory)
- Git: `main*>` (context: branch, dirty status)
- Python: `>>>` (familiar, recognizable)

**Missing context** that would be useful without being noisy:
- Current mode (review/apply/agent)
- Git branch
- Whether working tree is dirty

### Investigation Wording

| Phrase | Issue |
|---|---|
| `Investigating repository...` | Always says "repository" even for git status queries or CI checks. Not all investigations are repository searches. |
| Tool labels (`Locating files...`, etc.) | Defined but never printed. Good instinct to be quiet, but the labels exist and go unused. |
| `✓ Investigation complete` / `▶ Ready to explain` | Two lines for one transition. The play button arrow suggests the user needs to take action, which they do not. |

### Error Wording

Clean and appropriate. The only suggestion: prefixing errors with `cursor` in dim text reads as attribution. Error messages work better as direct system speech: `Insufficient repository evidence.` rather than `cursor\n\nInsufficient repository evidence...`.

### The `/help` Output

The single largest UX problem in prompt design. A new user typing `/help` sees ~40 commands across ~30 lines. Categories (meta commands, direct commands, file injection, shell) are not visually separated. There is no "getting started" section at the top.

---

## 6. Discoverability

### What Users Find Naturally

| Feature | Discoverable? | How |
|---|---|---|
| `/inspect` | Good | Post-answer `Press I to inspect evidence` hint |
| ESC → model selection | Adequate | Hint line: `press ESC again to go back` |
| History navigation | Good | Arrow keys are standard |
| `help` / `?` | Reasonable | Common terminal convention |

### What Users Miss

| Feature | Discoverable? | Problem |
|---|---|---|
| Shell mode (`!`) | Poor | No hint, no mention in startup. Only found via `/help`. |
| `@file` injection | Poor | No examples shown. Only found via `/help`. |
| Direct command prefixes (`search:`, `git:`, etc.) | Poor | 20+ prefixes with no discoverability. Exposed only through `/help`. |
| Debug mode (`/debug`) | Adequate | Listed in `/help` but new users won't know to look. |
| `/goal`, `/task`, `/params` workflow | Poor | Full project management system hidden in `/help`. |
| `--doctor`, `--self-test`, `--benchmark` CLI flags | Poor | Only visible via `--help` at startup. |
| Review/apply/agent mode toggles | Poor | `/mode` listed in `/help` but no visible indicator of current mode in prompt. |

### The `/help` First Experience

A new user typing `/help` sees:

```
Available meta commands:
  /help or /?             - Show this help
  /debug                  - Toggle verbose/debug mode
  /clear                  - Clear screen
  /goal set <description> ...
  ...
```

40+ entries, flat list, no categories, no examples, no "here is what you typically want." The cognitive load of parsing this list discourages further exploration.

---

## 7. Interaction Problems

Every place where a user may hesitate or feel uncertain:

### Startup Hesitation (Problem 1)

```
▌ CURSOR

  Select Model Type:
    Local Models
    Free Online Models
    Paid Online Models
```

A first-time user sees this before they can type anything. They must make a model choice without knowing what models are available or what the tradeoffs are. The natural reaction is uncertainty -- "which one should I pick?"

**Severity:** Medium. One-time cost.

### The Flash (Problem 2)

```
Investigating repository...

✓ Investigation complete

▶ Ready to explain
```

For a simple query (e.g., "what files are modified?"), this sequence appears and resolves in under 200ms. The user sees a flash of text that disappears before they can read it. This is disorienting -- it feels like the terminal glitched rather than showing progress.

**Severity:** Medium. Repeated on every fast query.

### Stale Progress (Problem 3)

For long investigations (CI analysis, architecture review, multi-tool queries), the user sees:

```
Investigating repository...
```

...then nothing for 5-30 seconds. No spinner, no progress update, no indication of what is happening. The user cannot distinguish between "investigating" and "hung."

**Severity:** High. Creates real uncertainty.

### No Mode Visibility (Problem 4)

The prompt `> ` does not indicate the current mode (review/apply/agent). A user who types `/mode review` sees confirmation, then the prompt returns to `> ` with no persistent indicator. In review mode, write operations silently fail. The user would only discover the mode change when a write attempt is blocked.

**Severity:** High. Silent behavior change without visible state.

### Ctrl+C Kills the Process (Problem 5)

Pressing Ctrl+C during any interaction terminates the agent. There is no confirmation, no graceful shutdown, no "are you sure?" This is the most abrupt possible failure mode.

**Severity:** High. Data loss potential for unsaved conversation state.

### Arrow Key Latency (Problem 6)

Every arrow key press incurs a 50ms `select()` timeout while the line editor checks whether ESC is standalone or part of a CSI sequence. This is noticeable to a fast typist. The implementation reads byte-by-byte rather than parsing the full escape sequence atomically.

**Severity:** Low-Medium. Cumulative friction for keyboard-first usage.

### `/help` Overload (Problem 7)

The full command list is too long for a first read. Users scan, find nothing familiar, and stop. The list is also the only documentation -- there is no `--welcome`, `--tips`, or gradual introduction.

**Severity:** Medium. Reduces feature adoption.

### No Tab Completion (Problem 8)

Tab does nothing useful. Users accustomed to shells, `redis-cli`, `psql`, `iex`, or any modern REPL will press Tab and get no response. This undermines the keyboard-first impression.

**Severity:** Medium. Missed opportunity for discoverability and speed.

### Multi-line Input Gap (Problem 9)

The line editor is single-line only. For prompts that benefit from multi-line input (code generation, multi-step instructions), the user must type everything on one line or use `\n` characters. This is limiting for the primary interaction mode.

**Severity:** Low-Medium. Depends on usage patterns.

---

## Summary

### Strengths

1. **Quiet default mode.** Tool internals, confidence values, and planner metadata stay hidden. The user sees investigation → answer, which is the right abstraction.
2. **Post-answer inspect window.** `Press I to inspect evidence` is the ideal UX pattern: non-blocking, discoverable, user-initiated.
3. **Color-coded output.** Markdown rendering and mode toggles use color meaningfully without overusing it.
4. **History navigation.** Up/down arrows work naturally.
5. **Mode confirmation.** Mode changes produce immediate visual feedback.
6. **Clean error messages.** Error states are communicated without technical details.
7. **Signal handling for SIGPIPE.** Properly ignored.

### Friction Points

Ranked by user impact:

| Priority | Problem | Current Behavior | User Impact |
|---|---|---|---|
| **P1** | Ctrl+C kills process | SIGINT terminates agent | Data loss, abrupt exit |
| **P2** | No mode visibility in prompt | `>` never shows review/apply/agent | Silent failures in review mode |
| **P3** | Stale progress on long queries | `Investigating repository...` sits unchanged for 5-30s | User cannot distinguish running from hung |
| **P4** | `/help` overload | 40+ flat entries, no categories, no examples | New users stop exploring |
| **P5** | No tab completion | Tab inserts literal `\t` | Missed discoverability |
| **P6** | Model selection at startup | 2-3 screens before first prompt | One-time confusion |
| **P7** | Flash on fast queries | Progress text appears and disappears in <200ms | Disorienting |
| **P8** | Single-line input only | No multi-line editing | Limiting for complex prompts |
| **P9** | 50ms arrow key latency | ESC timeout before CSI parsing | Cumulative friction |

### Terminal Capabilities Worth Adopting

1. **OSC 8 clickable file paths** (high value, medium effort) -- every output path becomes navigable
2. **Bracketed paste** (medium value, low effort) -- prevents accidental multi-line execution
3. **SIGWINCH handler** (medium value, low effort) -- status line and dividers adapt to resize
4. **Synchronized output** (low value, low effort) -- reduces flicker

### Keyboard Improvements

1. **Ctrl+C cancels input, not process** -- return to fresh `>` prompt instead of terminating
2. **Ctrl+D on empty line exits** -- standard REPL convention
3. **Tab completion for commands** -- `/h` + Tab → `/help`, `/d` + Tab → `/debug`
4. **Ctrl+L clears screen** -- standard terminal shortcut
5. **Eliminate 50ms arrow key delay** -- use `select()` with 0ms timeout, or parse CSI sequences directly

### Recommendations Ranked by User Value

| Rank | Change | Rationale |
|---|---|---|
| 1 | Catch SIGINT, return to prompt | Eliminates worst failure mode. Single `signal()` call + flag check. |
| 2 | Show mode in prompt (`[review] >`) | Prevents silent failures. Single string change in `read_prompt()` display. |
| 3 | Add live progress for long operations | Show tool labels or a time-based indicator after 3s. Reduces uncertainty. |
| 4 | Add tab completion for `/` commands | High discoverability value. Dictionary lookup of meta commands. |
| 5 | Shorten completion to one line | `✓ Ready` instead of two-line sequence. Reduces transition friction. |
| 6 | Suppress progress flash for fast queries | Gate on execution time >500ms. Eliminates disorienting flicker. |
| 7 | Add Ctrl+C/D handling in line editor | Standard REPL expectations. Low effort. |
| 8 | Implement OSC 8 for file paths | Clickable paths in `/inspect` and answers. Changes terminal interaction quality. |
| 9 | Remove 50ms arrow key delay | Faster parsing of CSI sequences. Small but cumulative gain. |
| 10 | Categorize `/help` output | Sections with headers make scanning possible. New users find features. |
| 11 | Skip model selection if config exists | File-based config means zero prompts at startup for returning users. |
| 12 | Enable bracketed paste | One sequence on raw mode entry. Safety net for multi-line pastes. |
| 13 | Handle SIGWINCH | Status line width adapts to terminal resize. |
| 14 | Inherit last model from config | `/model` command instead of double-ESC flow. |
| 15 | Multi-line input mode | Editor mode triggered by `Ctrl+O` or triple-Enter. Lower priority for most queries. |

---

## Note

This review evaluates terminal UX only. It does not evaluate planner correctness, retrieval quality, answer accuracy, or architectural decisions. No implementation changes were made.

---

## Revised Priority Stack

Refined after discussion. The order follows user impact, not implementation effort.

### Priority 0: Reliability

These must happen before any UX polish. If users accidentally kill the session, everything else matters less.

1. **Graceful Ctrl+C handling** -- signal handler sets a flag; line editor returns to fresh prompt instead of process termination
2. **Proper terminal restoration** -- `tcsetattr` to restore cooked mode, cursor shown (`\033[?25h`), on any interruption
3. **Prompt redraw after interruption** -- restore terminal state, re-enter raw mode, redraw `> ` prompt

### Priority 1: Investigation Feedback

Replace the current single `Investigating repository...` with phased progress:

```
Investigating repository...

• Understanding request
• Locating files
• Reading implementation
• Verifying evidence
```

Only the current phase is highlighted. No spinner needed if phases change naturally. Phase labels are planner-focused, not tool-focused (show "Understanding request" not "classify_goal()").

### Priority 2: Discoverability

One line at first launch only:

```
Type /help if you're unsure where to begin.
```

No `!`, `@`, palettes, or shortcuts exposed immediately. `/help` remains the single entry point.

### Priority 3: Terminal Quality Standards

Standard terminal capabilities that make the agent feel native:

1. **Bracketed paste** -- `\033[?2004h` on raw mode entry, `\033[?2004l` on exit
2. **SIGWINCH handling** -- flag-check before prompt redraw to re-query terminal width
3. **OSC 8 hyperlinks** -- clickable file paths in `/inspect` and search output

### Priority 4: Prompt Context

Keep `>` minimal. Only surface mode changes explicitly:

```
Switched to review mode.

>
```

Not `[review] >` as a persistent indicator.

### Additional Items (from post-audit discussion)

1. **Consistent menu footer** -- every interactive screen uses `↑↓ Navigate   Enter Select   Esc Back`
2. **Esc to cancel long operations** -- lightweight alternative to Ctrl+C for investigations that haven't reached AI
3. **Planner phase labels** -- expose planner thinking phases, not tool names
4. **Progressively relaxed inspect prompt** -- `✓ Ready` instead of `▶ Ready to explain`; `Press I if you'd like to see how I reached this answer` instead of bare `Press I to inspect evidence`

---

## Implementation Order

```
Ctrl+C recovery (terminal safety, prompt redraw)
  → Goal understanding fixes (classification failures from audit)
  → Progress updates (planner phases instead of static text)
  → First-run discoverability (one-line hint, disappears after first prompt)
  → Bracketed paste (low risk, high quality-of-life)
  → SIGWINCH resize handling (terminal polish)
  → OSC 8 hyperlinks (progressive enhancement)
```

Every change must pass the regression checklist in `query_journey_audit.md` before closing.
