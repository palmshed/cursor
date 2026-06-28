---
---

# UI Audit: Backend Capability → User Visibility

**Date:** 2026-06-25
**Scope:** Verify that every backend feature added during the Directory-Aware Find cycle is actually visible to users.

---

## 1. Timeline UI

### Backend emits
```
Locating files...
Checking symbols...
Reading implementation...
Analyzing project structure...
Checking git history...
Fetching CI data...
Building...
Running tests...
Collecting evidence...
Preparing answer...
```

### Visibility audit

| Event | Emitted by | Visible in normal mode? | How it renders |
|-------|-----------|------------------------|----------------|
| `Analyzing project structure...` | `section_for_tool("discovery")` | ✅ YES | `show_progress_section` → prints header, deduplicates consecutive same-section |
| `Locating files...` | `section_for_tool("find")` | ✅ YES | `→ find <args>` under header |
| `Checking symbols...` | `section_for_tool("grep")` | ✅ YES | `→ grep <args>` under header |
| `Reading implementation...` | `section_for_tool("read")` | ✅ YES | `→ read` under header |
| `Checking git history...` | `section_for_tool("git")` | ✅ YES | `→ git log --oneline -10` |
| `Collecting evidence...` | `execution_engine.cpp:1172` | ✅ YES | `✓ N tool results, X grep, Y find, Z read` |
| `Preparing answer...` | `command_router.cpp:690` | ✅ YES | Printed before answer (not shown for ArchitectureReview or error states) |
| Goal type pipeline header | `execution_engine.cpp:988` | ❌ NO | Verbose-only (`show_pipeline_section` returns early in normal mode) |

### Problems

**1. "Collecting evidence..." appears at the end, not during tool execution.**
Users see it after all tools finish, not as a live status. The section name implies ongoing collection but it's actually a post-hoc summary.

**2. No real-time tool progress.**
Users see:
```
Locating files...
→ find replay --impl
```
...then a pause (tools run), then:
```
  ✓ 4 candidates (replay_service.h)
```
The pause during execution has no spinner or "working..." indicator. Users see a blank terminal while the tool runs.

**3. "Preparing answer..." hidden from ArchitectureReview and error states.**
The section header only appears for CodebaseQuery success and AI chat paths. ArchitectureReview returns before reaching line 690.

---

## 2. Tool Output UI

### Backend produces
```
✓ N matches found              (grep)
✓ N candidates (top_file)      (find)
✓ last_file                    (read)
✓ N commits found              (git)
✓ N lines                      (gh)
```

### Visibility audit

| Tool | Render check | Count visible? | Failure state visible? |
|------|-------------|---------------|----------------------|
| `grep` | ✅ `✓ N matches found` | ✅ Line count (excluding CANDIDATE/SELECTED/REASON/FILES lines) | ❌ "no results" for empty output; no exit code shown |
| `find` | ✅ `✓ N candidates (selected)` | ✅ Candidate count + selected filename | ❌ "no results" for empty; no exit code shown |
| `read` | ✅ `✓ last_file` or `✓ N files` | ✅ Shows last file read (not total count) | ❌ "no results" for empty; no error stderr shown |
| `git` | ✅ `✓ N commits found` | ✅ Line count | ❌ No error handling visible |
| `gh` | ✅ `✓ N lines` | ✅ Line count | ❌ No error handling visible |

### Problems

**1. Read output shows only the last file, not all files read.**
If the engine reads 4 files, the user sees only the last one:
```
→ read
  ✓ include/services/replay_service.h
```
They don't know 4 files were read total. Line 881-898 counts `--- ` delimiters but only renders the last file name.

**2. File content truncation is invisible to users.**
Files are truncated to 500 chars (line 400 in command_router.cpp), but there's no `... (truncated)` indicator.

**3. No error/success distinction in tool output rendering.**
Failed tool output (stderr, exit code) is not displayed in normal mode. Line 852-854:
```
if (output.empty() || output == "no matches" || output == "no files to read") {
    std::cout << "  → no results\n";
```
This swallows stderr and error information.

---

## 3. Architecture Review UI

### Backend produces
```
## Finding
Risk: Medium
Dead code: legacy AgentMode enum remains
Location: ...
Recommendation: ...
```

### Visibility audit

| Aspect | Status | Detail |
|--------|--------|--------|
| Structured report exists | ✅ YES | `build_review_report` at execution_engine.cpp:841-975 |
| Rendered with formatting | ❌ **RAW TEXT** | `std::cout << engine_result.summary;` (line 482) -- markdown-like `##` headers printed as plain text |
| Section/card layout | ❌ NO | Plain monospace dump, no visual hierarchy |
| Risk coloring | ❌ NO | "Medium"/"Low"/"High" risk labels are plain text strings |
| Finding count | ✅ YES | `N findings total` printed at end |

### Problem
Architecture Review output is a raw text dump. The data is structured (findings with location, evidence, risk, recommendation), but it's rendered as:
```
Architecture Review Report (read-only)

...

## Finding
Risk: Medium
...
```

No visual hierarchy (bold/color/spacing), no cards, no collapsible sections. This is the poorest UI experience of any backend feature.

---

## 4. Find Results UI

### Backend produces
```
CANDIDATE: include/services/replay_service.h 22 exact filename match + implementation file
CANDIDATE: src/services/replay_service.h 14 partial filename match + implementation file
CANDIDATE: src/services/replay_service.cpp 22 exact filename match + implementation file
CANDIDATE: include/services/replay_service.cpp 30 exact filename match + symbol match + implementation file
SELECTED: include/services/replay_service.cpp
REASON: exact filename match + symbol match + implementation file
FILES:
include/services/replay_service.h
...
```

### Visibility audit

| Aspect | Status | Detail |
|--------|--------|--------|
| Section header | ✅ YES | `Locating files...` |
| Tool invocation | ✅ YES | `→ find replay --impl` |
| Candidate count | ✅ YES | `✓ 4 candidates (replay_service.cpp)` |
| Selected file visible | ✅ YES | Top candidate name in parentheses |
| Score/reason visible | ❌ NO | Only the filename, not the ranking reason |
| All candidates visible | ❌ NO | Only the count + top candidate |

### Problems

**1. CANDIDATE/SELECTED/REASON lines are filtered out.**
The find output contains rich ranking information (scores, match types), but `show_tool_output` only renders:
```
  ✓ N candidates (top_file)
```
This is appropriate for normal mode (users don't need ranking internals), but there's no way to see the full ranking in normal mode.

**2. Evidence summary duplicates file listing.**
After all tools, the "Files examined:" section lists files from evidence facts. This is redundant with the per-tool output.

---

## 5. Telemetry Visibility

### Backend produces
```
Goal type: Repository Investigation
Tools executed: 2
  Tool: find replay --impl
  Result: SUCCESS
  Output:
    ...

Outcome: success
Iterations: 2
Duration: 204.5ms
```

### Visibility audit

| Aspect | Status | Detail |
|--------|--------|--------|
| Summary in normal mode | ❌ NO | `execution_engine.cpp:1191-1214` builds a summary string but it's only used by ArchitectureReview path (line 1222-1224) |
| `show_context_state()` | ❌ VERBOSE-ONLY | `command_router.cpp:766` -- renders goal, tasks, params but only in verbose mode |
| `show_pipeline_section` | ❌ VERBOSE-ONLY | Goal type header hidden from normal users (line 181) |
| Inspect mode | ✅ EXISTS | `agent_.state_.inspect_mode_` gates richer output, but `show_context_state` already handles this via is_verbose() |

### Problems

**1. Normal users see zero telemetry data.**
No duration, no iteration count, no tool count, no outcome status. The only feedback is the timeline section headers + tool invocations + tool output lines.

**2. No debug/diagnostic view accessible.**
There's no `--debug` flag, no keyboard shortcut to toggle telemetry, no way for normal users to see why a query failed.

**3. The telemetry data EXISTS in the result** but is only rendered in the validation runner and benchmark. Real users never see:
```
GoalType: Repository Investigation
Outcome: success
Iterations: 2
Tools: find replay --impl; read ;
```

---

## Summary

| Feature | Backend | Visible to users? | Gap severity |
|---------|---------|-------------------|-------------|
| Timeline section headers | ✅ EXISTS | ✅ YES | None |
| Collecting evidence summary | ✅ EXISTS | ✅ YES (after loop) | Minor -- appears at end, not live |
| Preparing answer header | ✅ EXISTS | ✅ YES | Minor -- hidden for error states |
| Tool checkmarks + counts | ✅ EXISTS | ✅ YES | None |
| Find selected file | ✅ EXISTS | ✅ YES | None |
| Find ranking detail | ✅ EXISTS | ❌ NO | Intentional -- normal users don't need it |
| Architecture Review formatting | ✅ EXISTS | ❌ RAW TEXT | **Gap** -- structured data rendered as plain text |
| Telemetry (duration, iter, outcome) | ✅ EXISTS | ❌ HIDDEN | Intentional -- but no debug view exists |
| Error state (stderr, exit code) | ✅ EXISTS | ❌ SWALLOWED | **Gap** -- error info hidden in normal mode |
| Read tool shows all files | ✅ EXISTS | ❌ LAST-ONLY | **Gap** -- only last file visible |

### Recommended improvements (foundation for next cycle, not implementation now)

1. **Architecture Review**: Wrap the report in section headers, bold risk labels, color-code severity (red=High, yellow=Medium, green=Low).

2. **Error visibility**: Show exit code + first line of stderr in normal mode when a tool fails. Currently `"no results"` swallows failures.

3. **Read file count**: Change line 895-898 to show count alongside last file name:
   ```
   ✓ last_file (+ N more files)
   ```

4. **Debug view**: A `--debug` flag that exposes telemetry summary (duration, iterations, outcome) at the end of each query.
