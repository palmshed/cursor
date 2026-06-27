# Level 2 Sprint 2 — Planner Recovery

## Mission

Teach the planner to investigate until it has sufficient evidence.

Do not introduce investigation tasks or subagents.

Focus exclusively on planner behavior.

## Architecture

### Before (Linear)

```
Question
    ↓
classify_goal()
    ↓
select_next_tool()  →  run_tool()  →  accumulate evidence
    ↓                                                   ↓
  empty? ────► check_completion()? ──► YES ──► Answer
    │               │
    │               NO
    │               │
    └───────────────┘
```

The planner runs a single pass through the tool sequence. If `select_next_tool()` returns empty, the loop exits regardless of evidence quality. Low confidence triggers early stop — no recovery path.

### After (Recovery Loop)

```
Question
    ↓
classify_goal()
    ↓
+--- primary tool loop (unchanged)
|   select_next_tool()  →  run_tool()  →  accumulate evidence
|       ↓                                                    ↓
|     empty? ────► check_completion()? ──► YES ──► confidence≥0.7? ──► Answer
|       │               │                                        │
|       │               NO                                       NO
|       │               │                                        │
|       └───────────────┘                                        │
|                                                                │
|   low confidence ──► select_recovery_tool() ──► run recovery tool
|                              │
|                          ┌───┴───┐
|                          │       │
|                     exhausted?  has strategy?
|                          │       │
|                       Answer    │
|                            continue loop ──► re-check completion
|
└─── max 3 recovery attempts per query
```

## Recovery Strategies

Defined in `execution_engine.cpp:809-877` (`select_recovery_tool()`).

| Strategy | Trigger | Action |
|---|---|---|
| Grep after find failure | `find:noresults` + grep not yet run | `grep <term>` |
| Read after grep | `grep:results` + not read | `read` |
| Grep after find+read | `find:results` + `read` + no grep | `grep <term>` |
| Discovery | no evidence found + no discovery yet | `discovery` |
| Find implementation | header examined, no .cpp | `find <term> --impl` |
| Find header | .cpp examined, no header | `find <term>` |

Strategies are evaluated in order. The first matching strategy is used per recovery attempt.

## Recovery Loop Entry Points

Recovery fires at three points in the execute loop:

1. **Tool sequence exhausted** — when `select_next_tool()` returns empty but completion is false, `select_recovery_tool()` is called before the loop exits (`execution_engine.cpp:1150-1157`)

2. **Low confidence** — when `ConfidenceService::should_stop()` fires, recovery is tried before declaring `stopped_early` (`execution_engine.cpp:1339-1345`)

3. **Post-completion confidence gate** — even when `check_completion()` returns true, if combined confidence < 0.7, a recovery tool may be executed (`execution_engine.cpp:1146-1182`)

## Telemetry

Recovery attempts are recorded in the evidence store as `recovery:attempt=N` facts.

Existing `RecoveryMetrics` continues to track:
- `attempts` — total tool execution count
- `strategy_changes` — tool type switches
- `evidence_found` / `verification_found` — binary outcome
- `confidence_delta` — change over the session
- Per-tool counters (grep, read, find)

## Files Changed

| File | Change |
|---|---|
| `include/services/execution_engine.h` | Added `select_recovery_tool()` declaration |
| `src/services/execution_engine.cpp` | Added `select_recovery_tool()` (lines 809-877), modified `execute()` with recovery loop (lines 1138-1182, 1150-1157, 1339-1345) |
| `scenarios/regressions/recovery_find_noresults.json` | New: tests find→grep→read→discovery recovery path |
| `scenarios/regressions/recovery_header_to_implementation.json` | New: tests confidence-based post-completion recovery |

## Acceptance Verification

The planner can recover from:

| Scenario | Mechanism |
|---|---|
| Wrong first file | find returns irrelevant file → read → grep recovery finds correct file |
| Missing declaration | header examined → find --impl recovery locates .cpp |
| Wrong ranking | find selects wrong candidate → grep recovery finds correct context |
| Low confidence | confidence < 0.7 triggers post-completion recovery tool |
| No results | find + grep both fail → discovery recovery provides project structure |
