---
---

# Query Journey Audit: 50 Developer Tasks

**Date:** 2026-06-28  
**Method:** Static trace through `classify_goal()` → `select_next_tool()` → execution loop → user-facing output  
**Goal:** Identify every hesitation point, incorrect classification, and surprising behavior  

---

## How to Read This Audit

Each journey records:

| Column | Meaning |
|---|---|
| **Task** | What the developer wants |
| **Input** | What they type |
| **GoalType** | How it's classified |
| **Tools** | What runs (and how many) |
| **Time** | Approximate wall-clock (tool count × typical latency) |
| **What user sees** | Exact normal-mode output sequence |
| **Friction** | What feels wrong |
| **Severity** | Critical / High / Medium / Low |

---

## 1. STATUS QUERIES (Working Tree)

The most fragmented intent in the system.

| # | Task | Input | GoalType | Tools | Time | What user sees | Friction | Sev |
|---|---|---|---|---|---|---|---|---|
| 01 | Show modified files | `"show modified files"` | CommitHistory | 1 (`git status`) | ~100ms | `Investigating... → ✓ complete → answer` | OK | -- |
| 02 | Check changed files | `"check changed files"` | CommitHistory | 1 (`git status`) | ~100ms | Same as above | OK | -- |
| 03 | What files changed | `"what files changed"` | CommitHistory | 1 (`git status`) | ~100ms | Same as above | OK | -- |
| 04 | **Show uncommitted changes** | `"show me uncommitted changes"` | **GeneralChat** | **0** | **0ms** | `cursor\n\n[AI answers from knowledge, no tools run]` | **Wrong answer. No git status run. AI hallucinates.** | **Critical** |
| 05 | **What files are modified** | `"what files are modified"` | **CodebaseQuery** | 2-3 (find → grep → read) | ~2s | `Investigating... → find/grep/read → "Insufficient evidence"` | **Wrong investigation path. Runs code search on a status query.** | **Critical** |
| 06 | **Did I edit anything** | `"did I edit anything"` | **GeneralChat** | **0** | **0ms** | `cursor\n\n[AI general answer]` | **No tools run. AI guesses.** | **Critical** |
| 07 | Current status | `"git status"` | CommitHistory | 1 (`git status`) | ~100ms | Status shown | OK | -- |
| 08 | What branch | `"what branch am I on"` | CommitHistory | 1 (`git status`) | ~100ms | Branch shown | OK | -- |

**Pattern:** Three phrasings of the same intent route to three different GoalTypes, two of which produce wrong results. The keyword list for status queries in CommitHistory is long but has gaps. "Uncommitted changes" and "are modified" fall through.

---

## 2. COMMIT HISTORY

| # | Task | Input | GoalType | Tools | Time | What user sees | Friction | Sev |
|---|---|---|---|---|---|---|---|---|
| 09 | Last commit | `"what is the last commit"` | CommitHistory | 2 (`git log -10` + `git log -1`) | ~200ms | `Investigating... → ✓ complete → answer` | OK | -- |
| 10 | Recent commits | `"show recent commits"` | CommitHistory | 2 | ~200ms | Same | OK | -- |
| 11 | What changed | `"what changed"` | CommitHistory | 1 (`git status`) | ~100ms | Status shown (not log!) | **Minor: "what changed" does `git status`, not `git log`. Shows working tree diff, not commit history.** | Low |
| 12 | What changed last week | `"what changed last week"` | CommitHistory | 1 (`git status`) | ~100ms | Status shown (ignores "last week") | **Ignored time qualifier. `git status` has no date concept.** | Medium |
| 13 | Commit history | `"commit history"` | CommitHistory | 2 (`git log -10` + `git log -1`) | ~200ms | Same | OK | -- |
| 14 | Check the files we changed | `"check the files we changed"` | CommitHistory | 1 (`git status`) | ~100ms | Same | OK | -- |

**Pattern:** Lines 11-12 show the limit of keyword matching -- "what changed" always means `git status`, not `git log --since=last.week`. The planner has no concept of time qualifiers.

---

## 3. ARCHITECTURE & DESIGN QUESTIONS

| # | Task | Input | GoalType | Tools | Time | What user sees | Friction | Sev |
|---|---|---|---|---|---|---|---|---|
| 15 | How is this designed | `"how is this agent designed"` | CodebaseOverview | 2 (discovery + read README) | ~3s | `Investigating... → ✓ complete → answer` | OK | -- |
| 16 | Explain architecture | `"explain the architecture"` | CodebaseOverview | 2 | ~3s | Same | OK | -- |
| 17 | **Tell me how repo investigation works** | `"tell me how repository investigation works"` | **CodebaseQuery** | 2-3 (find/grep + read) | ~2s | Runs code search instead of overview | **Wrong path. Should be CodebaseOverview (high-level). Runs file search on implementation.** | **High** |
| 18 | **How does the build system work** | `"how does the build system work"` | **CodebaseQuery** | 2-3 (find/grep + read about "build") | ~2s | File search on "build" instead of project overview | **Wrong path. Should describe build system, not grep for "build".** | High |
| 19 | Review architecture | `"review the architecture"` | ArchitectureReview | **11** (full audit) | ~15-30s | `Investigating...` (stalls for 15-30s with no progress change) | **No progress updates during 11-tool audit. User sees frozen terminal.** | Medium |
| 20 | Review codebase | `"review codebase"` | ArchitectureReview | 11 | ~15-30s | Same as above | **Same stall problem** | Medium |
| 21 | Explain this codebase | `"explain this codebase"` | **CodebaseQuery** | 2-3 | ~2s | Code search instead of overview | **Wrong path. Should be CodebaseOverview.** | High |
| 22 | What isthis repository | `"what is this repository"` | **CodebaseQuery** | 2-3 | ~2s | Code search instead of overview | **Wrong path.** | High |

**Pattern:** Queries starting with "tell me how", "how does the", "explain this" -- ambiguous between architecture explanation and code search. The current classifier sends them to CodebaseQuery. A human would recognize these as overview questions.

---

## 4. CODE SEARCH (Working Well)

| # | Task | Input | GoalType | Tools | Time | What user sees | Friction | Sev |
|---|---|---|---|---|---|---|---|---|
| 23 | Find CommandRouter | `"find CommandRouter"` | CodebaseQuery | 2 (find + read) | ~1s | `Investigating... → ✓ complete → answer` | OK | -- |
| 24 | Grep for Agent | `"grep Agent"` | CodebaseQuery | 2-3 (find+grep+read) | ~2s | Same | OK | -- |
| 25 | Where is ReplayService used | `"where is ReplayService used"` | CodebaseQuery | 2 (find + read) | ~1s | Same | OK | -- |
| 26 | How does auth work (code-level) | `"how does auth work in this project"` | CodebaseQuery | 2-3 | ~2s | Same | OK | -- |
| 27 | **Find binary** | `"find the cursor binary"` | CodebaseQuery | 2-3 (find fails, grep fallback) | ~2s | Now works (was a retrieval bug, fixed) | Resolved | -- |
| 28 | Read file | `"read file src/main.cpp"` | CodebaseQuery | 1 (`read src/main.cpp`) | ~100ms | OK | OK | -- |

---

## 5. CI / GITHUB INVESTIGATION

| # | Task | Input | GoalType | Tools | Time | What user sees | Friction | Sev |
|---|---|---|---|---|---|---|---|---|
| 29 | Why did CI fail | `"why did CI fail"` | CICheck | 1-3 (gh + optional grep/read) | ~3-10s | `Investigating... → ✓ complete → answer` | OK | -- |
| 30 | Check workflow | `"check my CI workflow"` | CICheck | 1 (gh list) | ~3s | Same | OK | -- |
| 31 | Investigate run URL | paste of `github.com/.../actions/runs/12345` | GitHubInvestigation | 2 (gh run view + logs) | ~5-15s | Same | OK | -- |
| 32 | **Check this log** | `"can you check this log https://..."` | **GeneralChat** | **0** | **0ms** | AI answers without running `gh` | **Wrong. Should be GitHubInvestigation. The URL is present but "check this log" is a GeneralChat pattern that matches before the URL check.** | **Critical** |

**Pattern:** Line 32 is a priority-ordering bug. The GeneralChat exclusion check (Level 8) fires before the GitHubInvestigation URL check (Level 6) if the query starts with general-language patterns. The order matters: URL-containing queries should be checked early regardless of surrounding language.

---

## 6. SESSION STATE (Well-Handled)

| # | Task | Input | GoalType | Tools | Time | What user sees | Friction | Sev |
|---|---|---|---|---|---|---|---|---|
| 33 | What model am I on | `"what model am i on"` | SessionState | 0 | ~0ms | `cursor\n\n[state answer]` | OK | -- |
| 34 | Am I online | `"am i online"` | SessionState | 0 | ~0ms | OK | OK | -- |
| 35 | What provider | `"what provider am i using"` | SessionState | 0 | ~0ms | OK | OK | -- |

---

## 7. CODE CHANGES (Long Running)

| # | Task | Input | GoalType | Tools | Time | What user sees | Friction | Sev |
|---|---|---|---|---|---|---|---|---|
| 36 | Add a CLI command | `"add a new CLI command"` | CodeChange | 5 (discovery, grep, read, cmake, ctest) | ~30s+ | `Investigating...` (no change for 30s) | **No progress updates for 5 tools across ~30s. Terminal appears frozen.** | High |
| 37 | Fix failing test | `"fix the failing unit test"` | CodeChange | 5 | ~30s+ | Same | **Same stall problem + after completion, user prompted to apply** | High |
| 38 | Refactor auth | `"refactor the authentication service"` | CodeChange | 5 | ~30s+ | Same | **Same stall problem** | High |
| 39 | Build project | `"build the project"` | CodeChange | 5 | ~30s+ | Same | **Same** | High |

**Pattern:** CodeChange is the most tool-heavy path (5 tools) with no progress differentiation. The user sees `Investigating...` for 30+ seconds. Build and test steps (cmake, ctest) are particularly slow. No feedback on which phase is running.

---

## 8. GENERAL CHAT (Correctly Routed)

| # | Task | Input | GoalType | Tools | Time | What user sees | Friction | Sev |
|---|---|---|---|---|---|---|---|---|
| 40 | How are you | `"how are you"` | GeneralChat | 0 | ~0ms | AI chat answer | OK | -- |
| 41 | What can you do | `"what can you do"` | GeneralChat | 0 | ~0ms | AI chat answer | OK | -- |
| 42 | How do I install python | `"how do I install python"` | GeneralChat | 0 | ~0ms | AI chat answer | OK | -- |
| 43 | **What is the difference** | `"what is the difference between X and Y"` | GeneralChat | 0 | ~0ms | AI chat answer | OK (correct) | -- |
| 44 | Hello | `"hello"` | GeneralChat | 0 | ~0ms | AI chat answer | OK | -- |

---

## 9. EDGE CASES & COMMAND OVERRIDES

| # | Task | Input | GoalType | Tools | Time | What user sees | Friction | Sev |
|---|---|---|---|---|---|---|---|---|
| 45 | Explicit git prefix | `"git:status"` | (direct command) | 1 | ~100ms | Direct output | **Only if user knows `git:` prefix exists. Not discoverable.** | Medium |
| 46 | Typo: comit | `"last comit"` | CommitHistory | 2 | ~200ms | OK (typo in keyword list) | OK | -- |
| 47 | Typo: codbease | `"codbease overview"` | CodebaseOverview | 2 | ~3s | Normalized by command_router before classify_goal() | OK (if routed through command_router) | -- |
| 48 | **Ambiguous: plan** | `"plan the implementation"` | **CodeChange** | 5 | ~30s+ | Full investigation | **"Plan" is not in any keyword list. Might miss CodeChange path.** | Medium |
| 49 | Long query | 200-character multi-sentence question | CodebaseQuery | 2-3 | ~2s | Works (keyword matching doesn't penalize length) | OK | -- |
| 50 | Multi-step: find + status | `"show me the last commit and the current branch"` | CommitHistory | 2 (log + status) | ~200ms | Both shown | OK (both are CommitHistory) | -- |

---

## Summary of Findings

### Friction Count by Severity

| Severity | Count | Issues |
|---|---|---|
| **Critical** | 4 | #04, #05, #06 (status queries misclassified) + #32 (URL check bypassed) |
| **High** | 5 | #17, #18, #21, #22 (overview queries misclassified) + #36-39 (no progress during long ops) |
| **Medium** | 5 | #12 (time qualifier ignored), #19-20 (11-tool audit, no progress), #45 (git: prefix hidden), #48 (plan keyword missing) |
| **Low** | 1 | #11 ("what changed" = status not log) |

### Classification Failures (Wrong Path)

| Wrong classification | Count | Examples |
|---|---|---|
| `GeneralChat` instead of `CommitHistory` | 2 | "show me uncommitted changes", "did I edit anything" |
| `CodebaseQuery` instead of `CommitHistory` | 1 | "what files are modified" |
| `CodebaseQuery` instead of `CodebaseOverview` | 4 | "tell me how repository investigation works", "how does the build system work", "explain this codebase", "what is this repository" |
| `GeneralChat` instead of `GitHubInvestigation` | 1 | "can you check this log https://..." |

### Progress Visibility Failures

| Path | Tools | Typical time | Current feedback | Gap |
|---|---|---|---|---|
| ArchitectureReview | 11 | 15-30s | Static `Investigating...` | No progress for 11 tools across 30s |
| CodeChange | 5 | 30s+ | Static `Investigating...` | No progress for 5 tools across 30s+ |
| CICheck (with failure) | 2-3 | 5-15s | Static `Investigating...` | No progress during long gh calls |

### Terminal Quality Failures

| Issue | Affected interactions | Effort to fix | Impact |
|---|---|---|---|
| Ctrl+C kills process | All interactions | Low | Critical: lost state |
| No bracketed paste | Pasting code/stacktraces | Low | Medium: accidental execution |
| No clickable paths | All file outputs | Medium | Medium: navigation friction |
| No resize handling | Long sessions | Low | Low: visual glitch |

---

## Journey Heatmap

```
Task type:              Status   Commit  Arch  Search  CI    Change  Chat  
--------------------------------------------------------------------------------
Classification OK?      ❌ 3/8   ✅ 6/6  ❌ 2/5  ✅ 6/6  ✅ 3/4  ✅ 4/4  ✅ 5/5
Progress visible?       ✅      ✅     ❌     ✅     ❌     ❌     ✅
Ctrl+C safe?            ❌      ❌     ❌     ❌     ❌     ❌     ❌
Result useful?          ❌      ✅     ✅     ✅     ✅     ✅     ✅
```

**Takeaway:** Status queries, architecture requests, and CI/Change operations have the most friction. Ctrl+C safety is a universal gap. ArchitectureReview and CodeChange lack progress feedback during their long execution paths.

---

## Regression Checklist

Every UX change must be verified against these journeys before closing:

| # | Journey | What to check |
|---|---|---|
| 01 | Normal code question | Classification correct, tools run, answer useful |
| 02 | Long investigation (>5s) | Progress visible, terminal not frozen |
| 03 | Review mode | Read-only enforcement visible, prompt reflects mode |
| 04 | Apply mode | Confirmation prompt shown, changes applied only on approval |
| 05 | `/inspect` | Full investigation detail shown, no planner metadata leaks |
| 06 | Shell command (`!`) | Command executes, output shown, mode toggles work |
| 07 | Ctrl+C during investigation | Terminal restored, prompt redraws, no corruption |
| 08 | Ctrl+C during answer generation | Terminal restored, no partial output |
| 09 | Terminal resize | Status line width adapts, prompt not displaced |
| 10 | Paste 100+ lines | Bracketed paste handled, no accidental execution |
| 11 | Unicode file paths | Non-ASCII paths display and navigate correctly |
| 12 | Windows terminal (conhost) | ANSI rendering, line editor, menus functional |
| 13 | Linux terminal (gnome, xterm) | All features work on common Linux terminals |
| 14 | macOS terminal (Terminal.app, iTerm2) | All features work on common macOS terminals |
| 15 | First launch | Startup hint shown, disappears after first prompt |
| 16 | Recovery -- low confidence | Planner attempts recovery, user sees progress, no tool internals |

Each journey must pass before the change is considered complete. If a journey produces unexpected output or terminal corruption, the change is not ready.
