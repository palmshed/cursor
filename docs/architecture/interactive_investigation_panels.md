---
---

# Interactive Investigation Panels -- Feasibility Report

## 1. Feasibility

### Mouse-driven collapsible panels: NOT FEASIBLE

The codebase has no mouse event handling infrastructure. Adding it would require:

- Enabling DEC private mode SGR mouse tracking (`\033[?1000h` + `\033[?1006h`)
- Parsing 6+ byte mouse report sequences from stdin
- Mapping click coordinates to semantic sections of rendered output
- Managing terminal scroll regions to prevent content from scrolling off-screen
- Testing across 4+ terminal emulators with varying SGR support

This is a new subsystem, not a feature addition. The effort-to-value ratio is poor for a CLI tool.

### Keyboard-driven inspection: FEASIBLE

No libraries needed. The terminal infrastructure already supports this:

- **Raw keyboard input**: Three separate `termios`-based input systems exist (`session.cpp`, `menu.cpp`, `dashboard_service.cpp`)
- **ANSI cursor control**: `\033[N A` (up), `\033[N B` (down), `\033[2K\r` (clear line) already in use
- **Alt-screen switching**: `dashboard_service.cpp` already uses `\033[?1049h`/`l` for interactive views
- **Inline printing**: `std::cout` with `\r` overwrite is the established pattern
- **Investigation metadata**: `UIManager::investigation_details()` + `InvestigationDetail` struct already exist

### Command-based `/inspect`: FEASIBLE (zero-effort fallback)

Works on all platforms including non-TTY (piped) mode. No interactive state management needed.

---

## 2. UX Design

### Primary flow: Keyboard-driven planner inspection

After investigation completes and the AI has answered:

```
✓ Investigation complete

▶ Ready to explain
Press I to inspect evidence
```

If the user presses `i` (within a short timeout), print planner-centric output:

```
▼ Investigation

Goal
Find the last commit

Reasoning
Git history is the authoritative source. The last commit is the most recent
entry visible with git log.

Evidence
✓ git log --oneline -10

Files examined
--

Confidence
0.99

Duration
0.18s
```

This is **planner output**, not tool output. The user sees why the planner chose that evidence, not which args were passed to which tool.

After printing, return to the normal prompt.

#### Alternative: Full answer context

```
▼ Investigation

Goal
Find the last commit

Reasoning
Git history is the authoritative source. The last commit is the most recent
entry visible with git log.

Evidence
✓ git log --oneline -10

Files examined
--

Confidence
0.99

Duration
0.18s

Answer
The last commit was abc1234 "fix: handle edge case in foo()".
```

This reuses the AI synthesis text so the user sees everything in one place.

### Hint line variants

Preferred -- minimal, no extra visual weight:

```
✓ Investigation complete

▶ Ready to explain
Press I to inspect evidence
```

Alternative -- more compact for experienced users:

```
✓ Investigation complete   [press I for evidence]
```

### Fallback: `/inspect` command

```
> /inspect
```

Prints the same planner-centric output for the last completed query. Works in non-TTY (piped) mode and for users who prefer typing.

---

## 3. InvestigationSession Model

The central artifact of Level 2. Every completed query produces one.

```cpp
struct ToolInvocation {
  std::string tool;     // "grep", "read", "git", "find", "discovery"
  std::string query;    // what the planner was looking for
  std::string result;   // human-readable: "10 commits", "3 matches in 2 files"
};

struct SymbolReference {
  std::filesystem::path file;
  std::string symbol;   // class / function name, or empty for file refs
};

struct InvestigationSession {
  std::string goal;                           // original user question
  std::string conclusion;                     // what the planner determined
  double confidence{0.0};
  std::chrono::milliseconds duration{0};
  Core::Outcome outcome{Core::Outcome::InsufficientEvidence};

  std::vector<ToolInvocation> tools_used;
  std::vector<std::filesystem::path> files_examined;
  std::vector<SymbolReference> symbols_found;
  std::vector<std::string> reasoning_steps;   // why each tool was chosen
  std::vector<std::string> evidence_summary;  // what was found

  bool sufficient_evidence{false};
  bool investigation_complete{false};
};
```

### Lifecycle

```
Planner creates InvestigationSession
        │
        ▼
   ┌──────────────┐
   │  Session     │ ── stores as `last_investigation_`
   │  (app state) │
   └──────┬───────┘
          │
    ┌─────┼────────────┐
    ▼     ▼            ▼
 Answer   i key      /inspect
 (uses   (displays  (prints
  goal)   session)   session)
          │
          ▼
      Telemetry
      (records session for replay)
```

### What it replaces

| Current artifact | Replaced by |
|---|---|
| `Result` struct in `execution_engine.cpp` | `InvestigationSession` (richer, planner-aware) |
| ~~`InvestigationDetail` in `UIManager`~~ | ~~removed~~ -- `show_investigation_complete()` now reads `InvestigationSession::tools_used` direct from agent state |
| Ad-hoc tool history in JSON output | Structured evidence list |
| Inline checkmarks in `show_tool_output` | Planner reasoning in session |

### Why it matters

- **Single source of truth**: The planner creates it once; everyone reads it
- **Planner-centric**: Stores *why* not just *what*
- **Replay-native**: Serialize to JSON for telemetry and debugging
- **Extensible**: Future planner capabilities (plan revisions, hypotheses) add fields here
- **No duplication**: UI, telemetry, `/inspect`, and the answer all read the same struct

---

## 4. Implementation

### Completed (Level 2, Sprint 1)

#### InvestigationSession model (day 1)

- Defined `InvestigationSession`, `ToolInvocation`, `SymbolReference` in `include/core/investigation_session.h`
- Uses `Core::Outcome` enum: `Success`, `InsufficientEvidence`, `Cancelled`, `Error`
- Stores reasoning steps (not yet populated), evidence summaries, timing, confidence

#### Population and storage (day 1-2)

- `InvestigationSession::from_result()` factory in `execution_engine.cpp:1369` -- populates from `ExecutionEngine::Result`
- Extracts tool summaries (match counts, commit counts) and file paths from tool results
- Stored as `std::optional<InvestigationSession> last_investigation` on `SessionState`
- Timing: `std::chrono::steady_clock` wraps `engine.execute()` in both `command_router.cpp` and `diagnostics.cpp`

#### JSON telemetry integration (day 2-3, reordered from Phase 3)

- `QueryResult` carries `std::optional<InvestigationSession>`
- `JsonConsumer::end_session()` reads session when available, emitting: `duration_ms`, `evidence`, `reasoning`, `investigation_complete`, `sufficient_evidence`
- Falls back to legacy `ExecutionResult` derivation when session absent (backward compat)
- Verified with `cursor-agent --json "what is 2+2"`

### Remaining

#### Phase A: `/inspect` command handler (0.5 day)

1. Add `/inspect` route in `command_router.cpp` -- reads `agent_.state_.last_investigation`
2. Format: multi-line print with goal, conclusion, files, tools, evidence, reasoning steps
3. Works in both TTY and non-TTY (piped) modes

#### Phase B: Keyboard `i`-key interaction (1-2 days)

1. Add short-lived `WAITING_INSPECT` state in `Session::run()` after the answer is printed
2. Watch for `i` key via existing termios raw input
3. On keypress: print `InvestigationSession` inline
4. On timeout (3-5s): transition to normal prompt
5. Show hint line "Press I to inspect evidence" in the ready message

#### Phase C: Replay consumer refactor (0.5 day)

1. `ReplayConsumer` in `diagnostics.cpp` currently reads `ExecutionResult` directly
2. Update to read `InvestigationSession` when available

#### Phase D: UI consumer refactor (0.5 day) ✅ Done

1. Removed `InvestigationDetail` struct, `details_` member, `investigation_details()` method
2. `show_tool_output()` simplified: no accumulation, only verbose-mode passthrough
3. `show_investigation_complete()` reads `Agent::state_.last_investigation` direct

---

## 5. Required Libraries

**None.**

All required capabilities already exist in the codebase:

| Capability | Existing implementation |
|---|---|
| Raw keyboard input | `termios` in `session.cpp:35-164`, `menu.cpp:40-106` |
| ANSI cursor movement | `\033[N A`, `\033[N B` in `menu.cpp:88,202` |
| Alt-screen buffer | `\033[?1049h`/`l` in `dashboard_service.cpp:378,473,535` |
| Structured result storage | `ExecutionEngine::Result` in `execution_engine.cpp` |
| Command dispatch | `command_router.cpp` with 20+ existing slash commands |
| Spinner/waiting state | `std::atomic<bool>` + thread pattern in `command_router.cpp:952-953` |

---

## 6. Estimated Implementation Effort

| Component | Effort | Complexity |
|---|---|---|
| `InvestigationSession` model + population | 1 day | Low |
| `/inspect` command handler | 0.5 days | Low |
| Session storage in `Session` state | 0.5 days | Low |
| Waiting state + single-key listener | 1-2 days | Low |
| Investigation display formatting | 0.5 days | Low |
| Telemetry serialization | 1 day | Low |
| Testing (TTY + non-TTY + cross-platform) | 1-2 days | Medium |
| Alt-screen detail view (optional) | 2-3 days | Medium |

**Actual** (model + telemetry): **2 days** (model + telemetry done; `/inspect` + keyboard pending ~2 days)

---

## 7. Status

| Component | Status | Detail |
|---|---|---|
| `InvestigationSession` model | **Done** | `include/core/investigation_session.h` |
| `from_result()` factory | **Done** | `execution_engine.cpp:1369` |
| Session storage in `SessionState` | **Done** | `std::optional<InvestigationSession>` |
| Timing (duration_ms) | **Done** | `steady_clock` wrappers in 2 call sites |
| JSON telemetry integration | **Done** | `JsonConsumer::end_session()` reads session |
| `/inspect` command | Pending | Simple route, 0.5 day |
| Keyboard `i`-key | Pending | `WAITING_INSPECT` state, 1-2 days |
| Replay refactor | Pending | Replace `ExecutionResult` directly, 0.5 day |
| UI refactor | **Done** | Removed `InvestigationDetail`, `details_`, `investigation_details()` -- reads `last_investigation` |

---

## 8. Risks

| Risk | Impact | Mitigation |
|---|---|---|
| Key-listening state captures keys meant for next prompt | Medium | Timeout + clean state transition; only first keystrike after ready message |
| Non-TTY (piped) users can't use keyboard expansion | Low | `/inspect` command works everywhere |
| Windows terminal lacks ANSI support | Medium | Windows already falls back to non-TTY mode in `session.cpp`; `/inspect` command works |
| Terminal scrollback makes inline output hard to see | Low | Alt-screen view as secondary option; `/inspect` reprints fresh |
| Investigation data lost if user switches context | Low | Store last session in `Session` state, not UI state; survives screen changes |
| Users won't discover the `i` key | Medium | Show hint text "Press I to inspect evidence" within the ready message; mention in `/help` |
| `InvestigationSession` duplicates existing `Result` struct | Medium | Migrate incrementally -- `Result` fields are a subset of session; deprecate Result after Phase 3 |

---

## 9. Key Design Decisions

| Decision | Rationale |
|---|---|
| No mouse support | Subsystem cost exceeds value for a CLI tool |
| Planner-centric output | User sees planner reasoning, not tool internals |
| `InvestigationSession` single model | Eliminates 4 representations of the same investigation |
| `i` key (not Tab or D) | Mnemonic for "investigation" or "inspect" |
| Short timeout, not persistent | Avoids capturing input meant for the next command |
| `/inspect` not `/details` | Renamed to match inspect-investigation terminology |
| Inline print, not alt-screen by default | Lower complexity; alt-screen is a Phase 4 option |
| `from_result()` in `execution_engine.cpp` not `investigation_session.h` | Avoids `Services → Core` include dependency from the header |
| JSON backwards-compatible fallback | `JsonConsumer` reads session when available, else derives from `EvidenceStore::facts` |
