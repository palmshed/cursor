# Cursor TUI Design Specification

## Design Philosophy

Cursor is an AI coding agent — the terminal is a **communication tool**, not a code editor. Every pixel of chrome serves one goal: reduce friction between thought and action.

**Principles:**
- **Input-first**: the cursor starts focused in the input bar. Every other UI element is secondary.
- **Progressive disclosure**: show the last exchange by default. Reveal history, metadata, and controls only when requested.
- **Zero-config startup**: the first launch asks one question (mode), then drops you into a conversation.
- **Keyboard-native**: every action has a keybinding. No required mouse interaction.
- **Responsive by default**: single layout that adapts across 80–200+ columns without breakpoints.

---

## 1. Global Layout Architecture

```
┌─ StatusLine ───────────────────────────────────────────── r1
│ ◉ Mode    Model    Provider    ~/project                 │
├──────────────────────────────────────────────────────────┤
│                                                          │ r2..N-3
│  Messages (scrollable, flex-grow)                       │
│                                                          │
│                                                          │
├──────────────────────────────────────────────────────────┤
│ build   ⌘P palette  :cmd  LSP:3  MCP:2  1.2k tokens     │ rN-2
├─ InputBar ───────────────────────────────────────────────┤ rN-1
│ > _                                                      │
└──────────────────────────────────────────────────────────┘ rN
```

### Region Definitions

| Region | Lines | Role | z-order |
|--------|-------|------|---------|
| StatusLine | 1 | Persistent context: mode, model, provider, project | 1 |
| Messages | 2..N-3 | Primary reading area: scrollable message history | 0 |
| ContextLine | N-2 | Active agent, token count, keybinding hints | 1 |
| InputBar | N-1 | Text input, file attachments, mode indicators | 2 |
| (overlay) | full | Modal dialogs: menus, help, sessions | 3 |

### Z-Order Rule

Overlays (z:3) always render **after** the base layout paint. On dismiss, the terminal is restored by re-entering the scroll region and re-drawing the affected lines — no full repaint needed.

### Scroll Region

```term
\033[2;N-3r   # scroll region: lines 2 through N-3
```

Lines 1 (StatusLine), N-2 (ContextLine), and N-1 (InputBar) are outside the scroll region and never move.

### Terminal Resize

On `SIGWINCH`:
1. Read new `ws_row` / `ws_col` via `ioctl(TIOCGWINSZ)`
2. Recompute scroll region boundaries
3. Clear screen with `\033[2J`
4. Repaint StatusLine, ContextLine, InputBar
5. Repaint visible messages trimmed to new height

The message buffer is never reflowed — only the viewport window changes. Messages wider than `ws_col` are soft-wrapped at display time only.

---

## 2. Screen: Chat (Primary)

This is where the user spends >95% of their time.

### 2.1 Layout

```
┌─ StatusLine ─────────────────────────────────────────────┐
│ ◉ Offline    qwen2.5-coder:1.5b    ~/cursor             │
├──────────────────────────────────────────────────────────┤
│                                                          │
│  ┌ You ─── 2:31 PM ──────────────────────────────────┐  │
│  │ What does this function do?                        │  │
│  └────────────────────────────────────────────────────┘  │
│                                                          │
│  ┌ Cursor ───────────────────────────────────────────┐  │
│  │ The function parses a JSON config file at the      │  │
│  │ given path. It reads the file, validates the       │  │
│  │ structure, and returns a Config object.            │  │
│  │                                                    │  │
│  │  │ path/to/config.json                             │  │
│  │  │ FileNotFound → return default config            │  │
│  │  │ Invalid JSON → return default config            │  │
│  │                                                    │  │
│  │ Would you like me to add logging?                  │  │
│  └────────────────────────────────────────────────────┘  │
│                                                          │
│  ┌ You ─── 2:32 PM ──────────────────────────────────┐  │
│  │ Yes, add logging too                               │  │
│  └────────────────────────────────────────────────────┘  │
│                                                          │
│  ┌ Cursor ───────────────────────────────────────────┐  │
│  │ [·] Processing...                                  │  │
│  └────────────────────────────────────────────────────┘  │
│                                                          │
├──────────────────────────────────────────────────────────┤
│ build   ⌘P palette  :cmd  !shell  842 tokens             │
├─ InputBar ───────────────────────────────────────────────┤
│ > _                                                      │
└──────────────────────────────────────────────────────────┘
```

### 2.2 Component Hierarchy

```
App
├── StatusLine
│   ├── ModeIndicator (◉ Online / ⚡ Offline)
│   ├── ModelName (claude-sonnet-4 / qwen2.5-coder:1.5b)
│   ├── ProviderName (Anthropic / Ollama) [online only]
│   └── ProjectDir (~/src/cursor)
├── MessageList (scrollbox, flex-grow)
│   ├── Message (user)
│   │   ├── Avatar ("You")
│   │   ├── Timestamp (2:31 PM) [configurable]
│   │   └── Body (markdown text)
│   ├── Message (assistant)
│   │   ├── Avatar ("Cursor")
│   │   ├── Timestamp
│   │   ├── Body
│   │   ├── CodeBlock (syntax-highlighted, foldable) [0..N]
│   │   ├── FileAttachment [0..N]
│   │   └── ToolCall [0..N]
│   └── LoadingIndicator (spinner + "Processing...")
├── ContextLine
│   ├── AgentName (build / plan)
│   ├── KeybindingHints (⌘P palette, :cmd, !shell)
│   ├── LspStatus (LSP:3) [if LSP active]
│   ├── McpStatus (MCP:2) [if MCP active]
│   └── TokenCount (842 tokens)
└── InputBar
    ├── ModePrefix (> for normal, ! for shell, / for cmd)
    ├── TextInput (line-editable, with cursor)
    ├── Placeholder ("Ask anything...")
    ├── FilePill [0..N] (attached file refs)
    └── CharCount (optional, >80% width warning)
```

### 2.3 Responsive Behavior

| Region | 80 col | 120 col | 160+ col |
|--------|--------|---------|----------|
| StatusLine | Mode + Model only | + Project | + Provider |
| Messages | No timestamps | Timestamps shown | + File tree sidebar (right panel) |
| Code blocks | No line nums | Line nums | + Fold indicators |
| ContextLine | palette hint only | + tokens | + LSP/MCP |
| InputBar | Single line | Single line | Multi-line (3 visible) |

**Sidebar (160+):** A right panel appears showing a file tree of the current project. Selecting a file with Tab focuses it; Enter opens in $EDITOR. The sidebar width is fixed at 36 columns.

### 2.4 Keyboard Shortcuts

| Key | Context | Action |
|-----|---------|--------|
| `Enter` | InputBar | Send message |
| `Shift+Enter` | InputBar | Insert newline |
| `Ctrl+Backspace` | InputBar | Delete word backward |
| `Ctrl+U` | InputBar | Clear line |
| `Ctrl+L` | InputBar | Clear screen (keep history in buffer) |
| `Esc` | InputBar | Blur input / cancel |
| `Tab` | InputBar | Insert 2 spaces / trigger completion |
| `Ctrl+P` | global | Command palette |
| `?` / `Ctrl+H` | global | Help overlay |
| `j` / `↓` | (scroll) | Scroll message list down 1 |
| `k` / `↑` | (scroll) | Scroll message list up 1 |
| `Ctrl+D` | (scroll) | Scroll half page down |
| `Ctrl+U` | (scroll) | Scroll half page up |
| `g` | (scroll) | Go to top of conversation |
| `G` | (scroll) | Go to bottom (latest message) |
| `/` | global | Search messages |
| `n` / `N` | (search) | Next / previous match |
| `Ctrl+Z` | global | Undo last response |
| `Ctrl+S` | global | Save session |
| `Ctrl+R` | global | Resume session picker |
| `Ctrl+E` | global | Toggle sidebar (160+ only) |
| `Ctrl+T` | global | Change theme |
| `Ctrl+Q` / `Ctrl+C` | global | Quit (with confirm if unsaved) |

### 2.5 State Transitions

```
[empty] ──type──→ [typing] ──Enter──→ [sending]
  ↑                                        │
  │                                  [streaming]
  │                                        │
  │                          ┌─────────────┤
  │                          │             │
  │                     [complete]    [error]
  │                          │             │
  └────────── Ctrl+Z ────────┘             │
                                           │
                                      [retry prompt]
                                           │
                                      [sending] → ...
```

**States detailed:**

| State | Appearance | Behavior |
|-------|-----------|----------|
| `empty` | Welcome message with starter prompts | Input bar shows placeholder |
| `typing` | Cursor blinking in input bar | Text accumulates, no send yet |
| `sending` | "Sending..." with spinner | Input bar disabled, message bubble shows "Sending..." |
| `streaming` | Partial response visible, animated cursor | Text appends character-by-character |
| `complete` | Full response rendered | Enable scroll, keyboard nav |
| `error` | Red banner: "Connection failed. [r] Retry [d] Dismiss" | `r` resends, `d` hides banner |
| `confirm-quit` | Overlay: "Exit? Unsaved session [s] Save [e] Exit without save [c] Cancel" | Choice-driven transition |

### 2.6 Example Terminal Rendering

**80-column terminal:**

```
┌─ Cursor ──────────────────────────────────────┐
│ ⚡ Offline  qwen2.5-coder:1.5b                │
├───────────────────────────────────────────────┤
│                                               │
│ You                                           │
│ Add error handling to parse_config            │
│                                               │
│ ┌ Cursor ─────────────────────────────────┐   │
│ │ I'll add try/except. Here's the         │   │
│ │ updated function:                       │   │
│ │ def parse(path):                        │   │
│ │   try:                                  │   │
│ │     return json.load(open(path))        │   │
│ │   except: return {}                     │   │
│ │ Want me to add logging too?             │   │
│ └─────────────────────────────────────────┘   │
│                                               │
├───────────────────────────────────────────────┤
│ build  ⌘P  :cmd  842 tok                      │
├───────────────────────────────────────────────┤
│ > _                                           │
└───────────────────────────────────────────────┘
```

**120-column terminal:**

```
┌─ StatusLine ─────────────────────────────────────────────────────────────┐
│ ◉ Online    claude-sonnet-4    Anthropic    ~/src/cursor                  │
├──────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│  You · 2:31 PM                                                          │
│  What does this function do?                                            │
│                                                                          │
│  ┌ Cursor · 4.2s · 328 tok ─────────────────────────────────────────┐   │
│  │ The function parses a JSON config file at the given path. It      │   │
│  │ reads the file, validates the structure, and returns a Config     │   │
│  │ object.                                                           │   │
│  │                                                                   │   │
│  │  1 │ def parse_config(path):                                      │   │
│  │  2 │     try:                                                     │   │
│  │  3 │         with open(path) as f:                                │   │
│  │  4 │             return json.load(f)                              │   │
│  │  5 │     except FileNotFoundError:                                │   │
│  │  6 │         return {}                                            │   │
│  │  7 │     except json.JSONDecodeError:                             │   │
│  │  8 │         return {}                                            │   │
│  │                                                                   │   │
│  │ Want me to add logging too?                                       │   │
│  └───────────────────────────────────────────────────────────────────┘   │
│                                                                          │
├──────────────────────────────────────────────────────────────────────────┤
│ build   ⌘P palette  :cmd  !shell  LSP:3  MCP:2  842 tokens               │
├─ InputBar ───────────────────────────────────────────────────────────────┤
│ > _                                                                      │
└──────────────────────────────────────────────────────────────────────────┘
```

---

## 3. Screen: Startup / Mode Selection

Shown once per session on first launch. Never shown again unless `/config` is invoked.

### 3.1 Layout

```
┌─ StatusLine ─────────────────────────────────────────────┐
│ ◉ Cursor  v1.0                                          │
├──────────────────────────────────────────────────────────┤
│                                                          │
│                    Welcome to Cursor                     │
│            Your AI coding agent in the terminal          │
│                                                          │
│                                                          │
│  ┌────────────────────────────────────────────────────┐  │
│  │  ◉ Online                                          │  │
│  │  ○ Offline                                         │  │
│  │                                                    │  │
│  │  ↑/↓ navigate · Enter select · q quit              │  │
│  └────────────────────────────────────────────────────┘  │
│                                                          │
│  (this area reserved for model picker when               │
│   Online or Offline is selected)                         │
│                                                          │
├──────────────────────────────────────────────────────────┤
│ ? Help  Ctrl+P palette                                   │
└──────────────────────────────────────────────────────────┘
```

### 3.2 Component Hierarchy

```
StartupScreen
├── WelcomeTitle ("Cursor")
├── WelcomeSubtitle ("Your AI coding agent in the terminal")
├── SelectionPanel
│   ├── RadioGroup (Online / Offline)
│   │   ├── RadioOption (◉ Online)
│   │   └── RadioOption (○ Offline)
│   └── KeybindingHint (↑/↓ navigate · Enter select)
├── ProviderPanel [visible after Online selected]
│   └── RadioGroup (Together AI / Cerebras / Fireworks / ...)
├── ModelPanel [visible after Offline selected or provider chosen]
│   └── RadioGroup (dynamic from Ollama / provider defaults)
└── Footer
    ├── HelpHint (?)
    └── PaletteHint (Ctrl+P)
```

### 3.3 State Transitions

```
start ──→ [mode selection]
              │
        Online │  Offline
              ▼          ▼
      [provider picker]  [model picker (Ollama)]
              │               │
              ▼               ▼
        [model picker]    [done]
              │
              ▼
          [done] ──→ transition to Chat screen
                    (clear screen, set scroll region)
```

The "done" transition prints a confirmation line, clears the screen, and enters the Chat screen.

### 3.4 Keyboard Shortcuts

| Key | Action |
|-----|--------|
| `↑` / `k` | Previous option |
| `↓` / `j` | Next option |
| `Enter` | Confirm selection |
| `Esc` | Back to previous step |
| `q` | Quit |

### 3.5 Example Rendering

```
┌─ StatusLine ─────────────────────────────────────────────┐
│ ◉ Cursor  v1.0                                          │
├──────────────────────────────────────────────────────────┤
│                                                          │
│                    Welcome to Cursor                     │
│            Your AI coding agent in the terminal          │
│                                                          │
│                                                          │
│  Mode                                                     │
│  ◉ Offline                                               │
│  ○ Online                                                │
│                                                          │
│  Model                                                   │
│  ○ llama3.2:3b                                          │
│  ◉ qwen2.5-coder:1.5b                                    │
│  ○ llama3.2:latest                                       │
│  ○ llama3.1:latest                                       │
│                                                          │
│  ↑/↓ · Enter select  Esc back                            │
│                                                          │
├──────────────────────────────────────────────────────────┤
│ ? Help  ⌘P palette                                       │
└──────────────────────────────────────────────────────────┘
```

---

## 4. Screen: Help Overlay

Opened with `?` or `Ctrl+H`. Modal overlay on top of current screen.

### 4.1 Layout

```
┌──────────────────────────────────────────────────────────┐
│                      Help ▼ General                      │
│                                                          │
│  General                                                 │
│  ┌────────────────────────────────────────────────────┐  │
│  │ ? / Ctrl+H         Show this help                  │  │
│  │ Ctrl+P             Command palette                 │  │
│  │ Ctrl+Q             Quit                            │  │
│  │ Ctrl+T             Switch theme                    │  │
│  └────────────────────────────────────────────────────┘  │
│                                                          │
│  Conversation                                             │
│  ┌────────────────────────────────────────────────────┐  │
│  │ Enter               Send message                   │  │
│  │ Shift+Enter         New line                       │  │
│  │ Ctrl+Z              Undo last response             │  │
│  │ Ctrl+L              Clear                          │  │
│  └────────────────────────────────────────────────────┘  │
│                                                          │
│  Navigation                                               │
│  ┌────────────────────────────────────────────────────┐  │
│  │ j/↓ / k/↑         Scroll down/up                  │  │
│  │ Ctrl+D / Ctrl+U    Half page down/up               │  │
│  │ g / G              Top / bottom                    │  │
│  │ /                  Search                          │  │
│  └────────────────────────────────────────────────────┘  │
│                                                          │
│  Sessions                                                 │
│  ┌────────────────────────────────────────────────────┐  │
│  │ Ctrl+S              Save session                   │  │
│  │ Ctrl+R              Resume session                 │  │
│  └────────────────────────────────────────────────────┘  │
│                                                          │
│  [1-4] Change tab  Esc close                             │
└──────────────────────────────────────────────────────────┘
```

### 4.2 Behavior

- Tab-based: `1` General, `2` Conversation, `3` Navigation, `4` Sessions
- `Esc` / `?` / `Ctrl+H` dismisses and returns focus to InputBar
- The overlay is rendered on top of the existing screen without clearing it
- When dismissed, the screen is redrawn by re-entering the scroll region and repainting

---

## 5. Screen: Session Manager

Opened with `Ctrl+S` (save) or `Ctrl+R` (resume).

### 5.1 Layout

```
┌─ StatusLine ─────────────────────────────────────────────┐
│ ◉ Sessions                                               │
├──────────────────────────────────────────────────────────┤
│                                                          │
│  Sessions                                                 │
│  ┌────────────────────────────────────────────────────┐  │
│  │  ○ fix-auth-bug         2h ago    12 messages     │  │
│  │  ○ refactor-api         yesterday  8 messages     │  │
│  │  ○ add-tests            Mar 15    24 messages     │  │
│  │  ○ db-migration         Mar 12     6 messages     │  │
│  │  ○ ci-setup             Mar 10    15 messages     │  │
│  └────────────────────────────────────────────────────┘  │
│                                                          │
│  j/k navigate · Enter resume · d delete · Esc back       │
│                                                          │
├──────────────────────────────────────────────────────────┤
│                                                          │
│  (InputBar hidden during session manager)                 │
│                                                          │
└──────────────────────────────────────────────────────────┘
```

### 5.2 Behavior

- Navigating here from `Ctrl+S` auto-focuses a "Save as:" input
- Navigating here from `Ctrl+R` shows the session list with keyboard navigation
- `d` deletes the selected session (with confirm)
- `Enter` resumes the selected session
- `Esc` returns to Chat screen

---

## 6. Component Library

### 6.1 Message

```
┌─ Avatar ─── Timestamp ──────────────────────────────────┐
│                                                          │
│  Body text, supporting markdown:                         │
│  - **bold**, *italic*, `inline code`                     │
│  - Links: [text](url)                                    │
│  - Lists: ordered and unordered                          │
│                                                          │
│  CodeBlock (if present, see below)                       │
│  FileAttachment (if present, see below)                  │
│                                                          │
└──────────────────────────────────────────────────────────┘
```

**States:**
- `pending`: dimmed, "Sending..." indicator
- `streaming`: trailing cursor animation, partial body
- `complete`: full rendering, all interactions enabled
- `error`: red left border, error banner at top

**Rendering rules:**
- User messages: no left border (or thin gray)
- Assistant messages: colored left border (blue for default agent, green for plan agent)
- Borders are 1 column wide, rendered with box-drawing `│`

### 6.2 CodeBlock

```
┌─ language ─── copy ─── 8 lines ─── [−] ─────────────────┐
│                                                          │
│  1 │ def parse_config(path):                             │
│  2 │     try:                                            │
│  3 │         with open(path) as f:                       │
│  4 │             return json.load(f)                     │
│  5 │     except FileNotFoundError:                       │
│  6 │         return {}                                   │
│  7 │     except json.JSONDecodeError:                    │
│  8 │         return {}                                   │
│                                                          │
└──────────────────────────────────────────────────────────┘
```

**Sub-states:**
- `collapsed`: shows 3-line preview with `+ 5 lines` expand hint
- `expanded`: shows full code
- `copying`: brief "Copied!" flash indicator

**Foldable sections:**
- Long output (>20 lines): collapsed by default
- Toggle with `[−]` / `[+]` indicator
- Center line shows `··· 12 lines hidden ···`

### 6.3 Selection Menu (RadioGroup)

```
┌─ Mode ───────────────────────────────────────────────────┐
│                                                          │
│  ◉ Offline                                               │
│  ○ Online                                                │
│                                                          │
│  ↑/↓ · Enter select                                      │
└──────────────────────────────────────────────────────────┘
```

**Properties:**
- `◉` = selected, `○` = unselected
- Selected item is bold or highlighted with a distinct color
- First item selected by default
- Empty state: "No options available"
- Loading state: spinner icon for async options (e.g., Ollama model fetch)

### 6.4 StatusLine

```
◉ Online    claude-sonnet-4    Anthropic    ~/src/cursor
```

Three rendering modes:
- **Full** (≥120 col): Mode · Model · Provider · Project
- **Compact** (80–119 col): Mode · Model · Project
- **Minimal** (<80 col): Mode · Model

### 6.5 InputBar

```
> Add error handling to the parse_config function_
                    ↑ cursor position
```

**States:**

| State | Appearance | Behavior |
|-------|-----------|----------|
| `empty` | Placeholder text: "Ask anything..." | Dimmed gray text |
| `typing` | Cursor blinking, text visible | Normal rendering |
| `file-attached` | Pill badge: `📄 src/config.py ×` | `×` dismisses attachment |
| `shell-mode` | Prefix changes to `! ` | Enter executes shell cmd |
| `command-mode` | Prefix changes to `/ ` | Enter routes to command handler |
| `disabled` | Dimmed, no cursor | During streaming/loading |

**Multi-line input:**
- `Shift+Enter` inserts newline
- Input area expands up to 5 lines, then scrolls within input
- Character count shown in ContextLine when >80% of limit

### 6.6 Loading / Spinner

```
[·] Processing...
[o] Processing...
[O] Processing...
[o] Processing...
```

A 4-frame spinner rendered inline. Frames cycle every 150ms. Threaded with `std::atomic<bool>` and a background thread. On completion, the spinner line is replaced with the response text.

### 6.7 Error Banner

```
┌─ ⚠ Connection failed ──────────────────────────────────┐
│ Could not reach api.anthropic.com. Check your network   │
│ and API key.                                            │
│                                                         │
│ [r] Retry  [d] Dismiss                                  │
└─────────────────────────────────────────────────────────┘
```

Rendered as an inline message in the MessageList. Red left border.

---

## 7. Navigation Model

### 7.1 Focus Zones

```
  StatusLine     ← always visible, never focusable
  MessageList    ← scrollable, focusable (scroll mode)
  ContextLine    ← always visible, never focusable
  InputBar       ← always focusable (default)
  Overlay        ← captures all input when active
```

The **default focus** is InputBar. When the user presses `j`/`k`, focus shifts to MessageList for scrolling. Pressing `Enter`, `i`, or `Esc` returns focus to InputBar.

### 7.2 Modal vs Modeless

| Mode | Trigger | Indicator | Behavior |
|------|---------|-----------|----------|
| Input (default) | — | `> ` prefix | Keys type into input |
| Scroll | `j`/`k`/`g`/`G` | StatusLine shows scroll position | Arrow keys scroll, `i` or `Enter` returns to input |
| Command | `/ ` prefix at input start | `/ ` prefix | Commands like `/help`, `/save` |
| Shell | `! ` prefix at input start | `! ` prefix | Enter executes shell command |
| Menu | From any command | Overlay | Arrow keys navigate, Enter selects |

### 7.3 Command Palette (Ctrl+P)

Opens a fuzzy-finder overlay listing all available commands:

```
> read file
──────────────────────────────────────────────────────────
  read:path              Read a file
  read:path:start:count  Read file range
  search:query           Search codebase
  /help                  Show help
  /save name [tags]      Save session
```

- Type to filter with fuzzy matching
- `↑`/`↓` to navigate
- Enter to execute
- Esc to dismiss

---

## 8. Theme System

### 8.1 Color Tokens

```
--surface:          terminal background (detected)
--surface-alt:      dimmer variant for panels
--text:             terminal foreground
--text-dim:         \033[2m
--accent:           #00aaff (blue, for assistant messages)
--accent-alt:       #00cc66 (green, for plan agent)
--warning:          #ffaa00
--error:            #ff4444
--success:          #00cc66
--border:           dim variant of text
--selection:        inverted text/background
```

### 8.2 Auto Theme Detection

Read `\033]10;?\007` (foreground) and `\033]11;?\007` (background) terminal queries. If background luminance > 0.5, use dark theme; otherwise light.

`Ctrl+T` cycles: auto → dark → light.

### 8.3 Minimum Viable Palette

The first version uses **4 ANSI colors only** (plus DIM) to guarantee compatibility across all terminals:

| Role | Color |
|------|-------|
| Header text | BOLD |
| Dim text | DIM (default foreground) |
| Accent | CYAN |
| Error | RED |
| Success | GREEN |
| Warning | YELLOW |

This avoids any reliance on 24-bit color or specific terminal capabilities.

---

## 9. State Machine (Full Session)

```
                    ┌─────────────────────────┐
                    │      LAUNCH              │
                    │  (check update, etc.)    │
                    └──────────┬──────────────┘
                               │
                               ▼
                    ┌─────────────────────────┐
                    │     STARTUP              │
                    │  Mode / Model selection  │
                    └──────────┬──────────────┘
                               │
                               ▼
                    ┌─────────────────────────┐
              ┌─────│       CHAT              │◄────┐
              │     │  Conversation loop       │     │
              │     └──┬──────────┬───────────┘     │
              │        │          │                 │
              │    Ctrl+S    Ctrl+R            Esc (quit)
              │        │          │                 │
              ▼        ▼          ▼                 ▼
     ┌──────────┐ ┌────────┐ ┌──────────┐   ┌──────────────┐
     │  HELP    │ │  SAVE  │ │  RESUME  │   │  CONFIRM     │
     │  Overlay │ │  Dialog│ │  Picker  │   │  Quit?       │
     └──────────┘ └────────┘ └──────────┘   └──────┬───────┘
         │             │          │                 │
         └─────────────┴──────────┴─────────────────┘
                               │
                          s / e / c
                               │
                  ┌────────────┴────────────┐
                  ▼                         ▼
            ┌──────────┐            ┌──────────────┐
            │  Save +  │   e │     │  EXIT        │
            │  Exit    │◄────┘     │  (goodbye)   │
            └──────────┘           └──────────────┘
```

---

## 10. Implementation Constraints

| Constraint | Decision |
|-----------|----------|
| No external dependencies | Raw ANSI escape codes + ioctl |
| Cross-platform | `#ifdef _WIN32` for terminal API |
| TTY detection | `isatty()` gating for all ANSI output |
| Terminal resize | `SIGWINCH` handler + repaint |
| Scroll region | `\033[top;bottomr` for fixed chrome |
| Input in raw mode | `tcsetattr(ICANON|ECHO)` toggle per key read |
| Rendering model | Line-based paint, no full-screen buffer |
| Streaming response | Background thread + atomic done flag + periodic flush |

The design deliberately avoids a frame-buffer or cell-grid approach. Each component is stateless and paints by writing lines + cursor-movement sequences to stdout. This keeps the implementation minimal and avoids the complexity of maintaining a diff buffer.

---

## 11. Migration Path (Current → Target)

| Step | Change | Effort |
|------|--------|--------|
| 1 | Add StatusLine + ContextLine regions (outside scroll) | Small |
| 2 | Migrate startup menus to modal RadioGroup overlays | Medium |
| 3 | Add message component with border + avatar rendering | Small |
| 4 | Implement focus zones (input / scroll) | Medium |
| 5 | Add code block folding | Small |
| 6 | Add command palette (Ctrl+P) | Large |
| 7 | Add session save/resume dialog | Medium |
| 8 | Add theme detection + Ctrl+T cycling | Small |
| 9 | Add `SIGWINCH` handler | Small |
| 10 | Add /search overlay | Medium |
