---
---

# Human Evaluation Package -- Planner Investigation Quality

## Purpose

Evaluate whether the planner investigates like a senior engineer.

The evaluator should be someone unfamiliar with this repository.

## Instructions

1. Run each question through cursor-agent and observe the output.
2. Evaluate each answer against the criteria below.
3. Record scores in the scoring sheet.
4. The planner passes if ≥16/20 questions score ≥3 on correctness.

## How to Run

```bash
# Run a single question
./build/bin/cursor-agent --json "<question>" 2>/dev/null

# Or use the inspect command in interactive mode:
# Type the question, then press 'i' when prompted
```

---

## 20 Architecture Questions

### Q1: Initialization Order
**Question**: Find the initialization order in Agent::run() to see why logging is set up before the main interaction loop

**Expected answer**: `Agent::run()` in `src/agent.cpp:27-33` creates UIManager first, then ReplayService, then CommandRouter, then Session. UIManager is created first because log output infrastructure is needed before any commands can be processed. Session::run() is the last step and enters the interaction loop.

**Evidence to find**: src/agent.cpp:27-33

### Q2: Diagnostics Isolation
**Question**: Find how the diagnostics module is separated from the execution engine in the source code

**Expected answer**: Diagnostics lives in `src/diagnostics/diagnostics.cpp` as a standalone module. It creates its own ExecutionEngine instance and tool runners. It does NOT share state with the main Agent -- it's an independent verification harness that simulates tool calls rather than using the real ones.

**Evidence to find**: src/diagnostics/diagnostics.cpp, the `run_query()` function

### Q3: Lifecycle Shutdown
**Question**: Find the component responsible for lifecycle shutdown in the codebase

**Expected answer**: There is no explicit shutdown sequence. `Session::run()` uses `while(true)` and returns normally when the user types "exit" or "quit". Agent objects are unwound by C++ destructor order. No dedicated shutdown component exists -- the architecture relies on RAII and scope-based cleanup.

### Q4: Command Routing
**Question**: Trace how a user command reaches the execution engine from the terminal prompt

**Expected answer**: The path is: `main()` → `Agent::run()` → `Session::run()` → `read_prompt()` → `CommandRouter::process_user_input()`. Inside process_user_input, the classification ladder runs: shell mode check → `@` injection → `!` shell → `/` meta → direct command → NL mapping → ExecutionEngine::execute(). The engine creates a fresh instance, runs evidence collection, and returns ExecutionResult.

**Evidence to find**: session.cpp, command_router.cpp, execution_engine.cpp

### Q5: Tool Exhaustion
**Question**: Find what happens when the execution engine exhausts all its tool calls but still lacks evidence

**Expected answer**: When `select_next_tool()` returns empty (all tools are exhausted), `select_recovery_tool()` is called if recovery_count < 3. The recovery tool chooses a strategy based on evidence state (e.g., find failed → try grep, grep found results → try read, etc.). If recovery also fails or is exhausted, the loop exits with `stopped_early = true`.

**Evidence to find**: execution_engine.cpp:1150-1157 (recovery on tool exhaustion)

### Q6: ReplayService Separation
**Question**: Find how the ReplayService is separated from the Agent class in the codebase

**Expected answer**: ReplayService is created separately in `Agent::run()` and passed to Session and CommandRouter as a pointer. It's never stored inside Agent. It logs `SessionState` snapshots before and after each command to `~/.cursor/replay/`. The separation means replay can be disabled by passing nullptr.

**Evidence to find**: agent.cpp:27-33, replay_service.h/cpp

### Q7: Confidence Gating
**Question**: Find where the confidence gating prevents the AI from answering without sufficient evidence

**Expected answer**: In `command_router.cpp`, `should_call_ai(result)` checks `result.outcome`. Only `Success` outcome allows AI call. Non-Success outcomes produce deterministic messages directly. Additionally, `execution_engine.cpp` gates on combined confidence < 0.2 triggering early stop and < 0.7 triggering post-completion recovery.

**Evidence to find**: command_router.cpp (should_call_ai), execution_engine.cpp (confidence checks)

### Q8: InvestigationSession
**Question**: Find the InvestigationSession struct and trace how it bridges execution to the user

**Expected answer**: `InvestigationSession` is defined in `include/core/investigation_session.h`. It's created via `from_result()` which maps `ExecutionResult` fields (tools, files, evidence, confidence, outcome) into the struct. It's stored in `SessionState::last_investigation`. The `/inspect` command reads this field and displays it.

**Evidence to find**: investigation_session.h, command_router.cpp (handle_inspect_command)

### Q9: Deterministic vs LLM Classification
**Question**: Find where the planner supports both deterministic and LLM-based classification paths

**Expected answer**: `ExecutionEngine` has two classification modes: `Deterministic` and `LLM`. The mode is controlled by `classifier_mode_`. `select_next_tool()` dispatches to `select_next_tool()` or `select_next_tool_llm()` based on mode, and `classify_goal()` similarly dispatches to `classify_goal()` or `classify_goal_llm()`. The deterministic mode uses regex/heuristic matching; the LLM mode calls AIService.

**Evidence to find**: execution_engine.h, execution_engine.cpp (classify_goal, select_next_tool)

### Q10: Recovery Loop Prevention
**Question**: Find how the recovery loop in select_recovery_tool() prevents infinite loops

**Expected answer**: Three mechanisms: (1) MAX_RECOVERY = 3 limit in execute(), (2) `seen_tool_calls` deduplication prevents running the same tool+args twice, (3) recovery strategies are evidence-driven -- they only fire when specific evidence facts are missing, so they naturally terminate when all evidence gaps are filled.

**Evidence to find**: execution_engine.cpp

### Q11: Classification Ladder
**Question**: Find how the CommandRouter determines whether a query goes to the engine vs a direct handler

**Expected answer**: The classification ladder in `process_user_input()` is a strict priority chain: (1) shell mode active → shell handler, (2) contains `@` → file injection, (3) starts with `!` → shell escape, (4) starts with `/` → meta command, (5) direct command match → direct handler, (6) NL-to-direct mapping → direct handler, (7) falls through to ExecutionEngine. First match wins.

**Evidence to find**: command_router.cpp (process_user_input)

### Q12: Discovery-Planning Relationship
**Question**: Find the relationship between discovery, planning, and the task pipeline in the source code

**Expected answer**: `DiscoveryService::scan()` detects project type, CI, relevant files. `PlanningService::generate_plan()` takes the discovery result and user query to create a structured task list. `handle_task_with_planning()` in command_router.cpp orchestrates: discovery → plan generation → user approval → execution → evidence collection. Discovery feeds the plan; the plan feeds the task pipeline.

**Evidence to find**: discovery_service.cpp, planning_service.cpp, command_router.cpp

### Q13: Metrics Population
**Question**: Trace how RecoveryMetrics, TrustMetrics, and RetrievalMetrics get populated after execution

**Expected answer**: `RecoveryMetrics` is computed in `ExecutionEngine::execute()` lines ~1500-1520 (tool attempt counts, evidence found, confidence delta). `TrustMetrics` is populated in `CommandRouter::process_user_input()` (plan_approved, diff_approved from user interaction). `RetrievalMetrics` is populated in `CommandRouter::handle_codebase_query()` (filename/symbol/directory/grep hits). All are stored in `SessionState` after each command.

**Evidence to find**: execution_engine.cpp, command_router.cpp, metrics.h

### Q14: Inspect Command
**Question**: Find how the inspect command (/inspect) retrieves and displays the last investigation session

**Expected answer**: `handle_inspect_command()` in `command_router.cpp` reads `agent_.state_.last_investigation` (an `optional<InvestigationSession>`). If absent, prints "No investigation data". If present, formats the struct fields (goal, conclusion, confidence, tools_used, files_examined, symbols_found, evidence_summary) into a human-readable report. Also, after each answer in Session::run(), a 3-second window waits for 'i' key to trigger inspect.

**Evidence to find**: command_router.cpp (handle_inspect_command), session.cpp (i key handler)

### Q15: EvidenceCollector
**Question**: Find the role of EvidenceCollector and when it is triggered in the codebase

**Expected answer**: `EvidenceCollector` is a private class inside `command_router.cpp`. It's used in `handle_task_with_planning()` after AI execution. It lazily collects per-task evidence: runs cmake build → ctest → git diff. Results are cached per file path. It provides `has_collected()`, `build_evidence()`, `test_evidence()`, and `diff_evidence()` accessors. It's only triggered in the task pipeline path, not during regular engine execution.

**Evidence to find**: command_router.cpp (EvidenceCollector class, handle_task_with_planning)

### Q16: Recovery Strategies
**Question**: Find the recovery strategies in select_recovery_tool() and the order they are evaluated

**Expected answer**: Five strategies evaluated in order: (1) find:noresults + no grep → grep, (2) grep:results + no read → read, (3) find:results + read + no grep → grep, (4) no evidence + no discovery → discovery, (5) header examined + no impl → find --impl / .cpp examined + no header → find header.

**Evidence to find**: execution_engine.cpp (select_recovery_tool)

### Q17: Confidence Combination
**Question**: Find how the confidence service computes combined confidence across multiple tools

**Expected answer**: Each tool call produces a `ConfidenceResult` with score and reason. `ConfidenceService::combine()` in `confidence_service.cpp` aggregates these: takes the average of all tool scores, then applies a dampening factor (0.9) for each tool beyond the first to prevent runaway confidence from many weak tools.

**Evidence to find**: confidence_service.cpp

### Q18: Zero Results Recovery
**Question**: Find what happens when both find and grep return no results for a codebase query

**Expected answer**: Strategy 1 fires: find returns noresults → grep is attempted. If grep also returns noresults → strategy 4 fires: discovery is attempted (provides project structure, source counts, CI info). If all recovery strategies are exhausted, the loop exits with `stopped_early = true` and outcome = `InsufficientEvidence`. The confidence is low (< 0.2) so `should_call_ai()` returns false.

**Evidence to find**: execution_engine.cpp (select_recovery_tool, should_stop check)

### Q19: ArchitectureReview vs CodebaseQuery
**Question**: Find how the ArchitectureReview goal type differs from CodebaseQuery in tool selection

**Expected answer**: `CodebaseQuery` uses: find (or references for callers) → read → grep. `ArchitectureReview` uses: discovery → git log → multi-step grep (AgentMode, MODE_, AuthProvider, strategy_changes) → multi-file read (session_state.h, metrics.h, execution_engine.cpp) → read tests/validation_runner.cpp. ArchitectureReview is a fixed script; CodebaseQuery adapts to the query.

**Evidence to find**: execution_engine.cpp (select_next_tool for each goal type)

### Q20: Tool Deduplication
**Question**: Find how tool deduplication works in the execution engine and when it triggers recovery

**Expected answer**: Each tool+args pair is serialized to a `tc_signature` string. Before executing a tool, the engine checks `seen_tool_calls` set. If the signature exists, the tool is skipped and recovery is attempted (up to 3 times). If recovery_count >= 3 and deduplication fires, `stopped_early = true` and the loop exits. This prevents the engine from repeating the same failed search.

**Evidence to find**: execution_engine.cpp (seen_tool_calls logic)

---

## Evaluation Rubric

Each question is scored 1-5:

| Score | Label | Description |
|-------|-------|-------------|
| 5 | Excellent | Answer is correct AND cites specific files/lines. Investigation path is clear. |
| 4 | Good | Answer is correct. May be slightly vague on exact file/line references. |
| 3 | Adequate | Answer is directionally correct. Investigation found relevant code but missed nuances. |
| 2 | Poor | Answer is partially wrong or misses key evidence. Investigation path was confused. |
| 1 | Failing | Answer is wrong. Investigation failed or never ran tools. |

**Pass threshold**: ≥16/20 questions must score ≥3.

---

## Scoring Sheet

| Q# | Question | Correctness (1-5) | Investigation Clear? (Y/N) | /inspect Useful? (Y/N) | Recovery Visible? (Y/N) | Notes |
|----|----------|-------------------|---------------------------|----------------------|----------------------|-------|
| 1 | Initialization Order | | | | | |
| 2 | Diagnostics Isolation | | | | | |
| 3 | Lifecycle Shutdown | | | | | |
| 4 | Command Routing | | | | | |
| 5 | Tool Exhaustion | | | | | |
| 6 | ReplayService Separation | | | | | |
| 7 | Confidence Gating | | | | | |
| 8 | InvestigationSession | | | | | |
| 9 | Deterministic vs LLM | | | | | |
| 10 | Recovery Loop Prevention | | | | | |
| 11 | Classification Ladder | | | | | |
| 12 | Discovery-Planning | | | | | |
| 13 | Metrics Population | | | | | |
| 14 | Inspect Command | | | | | |
| 15 | EvidenceCollector | | | | | |
| 16 | Recovery Strategies | | | | | |
| 17 | Confidence Combination | | | | | |
| 18 | Zero Results Recovery | | | | | |
| 19 | ArchitectureReview | | | | | |
| 20 | Tool Deduplication | | | | | |

**Summary**:
- Total questions scoring ≥3: ___ / 20
- Average correctness: ___
- Investigation clear: ___ / 20
- /inspect useful: ___ / 20
- Recovery visible: ___ / 20

**Recommendation** (circle one): PASS / FAIL / CONDITIONAL

**Evaluator notes**:
_____________________________________________________________________
_____________________________________________________________________
_____________________________________________________________________
