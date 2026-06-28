---
---

# Evidence Quality Audit

## Why This Exists

Six benchmark queries still fail the same way:

```
expected InsufficientEvidence  got success
```

Every failure is a false positive -- the investigation collected evidence that the
evidence model accepted but a human reviewer would reject.

The root cause: **evidence is currently binary. Knowledge isn't.**

The planner asks "do I have FileSearch?" when it should ask "how good is my
FileSearch evidence?"

This document defines the evidence quality model that replaces binary evidence
checking with quality-aware completion.

---

## Part I: The Evidence Model

### EvidenceQuality vs EvidenceStrength

These are separate concepts that are often conflated.

**EvidenceQuality** measures how precisely the evidence matches the user's
question. It answers: "did we find the right thing?"

**EvidenceStrength** measures how much supporting data was collected. It
answers: "how much of it did we find?"

Example -- searching for "evidence gating":

```
FileSearch
  Quality: Weak      (no file contains the exact phrase)
  Strength: High     (grep matched "evidence" in many files)
```

The match is imprecise (Weak) but there is a lot of it (High).

Example -- searching for `ExecutionEngine`:

```
FileSearch
  Quality: Strong    (exact symbol match)
  Strength: Medium   (found in a moderate number of files)
```

The match is precise (Strong) with a moderate volume (Medium).

The distinction matters because completion depends on **quality**, not
strength. Many weak matches are still weak. A single strong match is enough.

### Vocabulary

Five levels. No numeric scores.

| Level | Meaning |
|-------|---------|
| `None` | No evidence of this class was collected |
| `Weak` | Evidence exists but is imprecise or unreliable |
| `Moderate` | Evidence is plausible but not definitive |
| `Strong` | Evidence is precise and well-supported |
| `Verified` | Evidence is independently corroborated or authoritative |

No 0.61, 0.84, 0.33. If a value is not one of these five, it is not a quality
value.

### Quality per Evidence Class

| Class | Weak | Moderate | Strong | Verified |
|-------|------|----------|--------|----------|
| FileSearch | Tokenized grep match, >10 results | Substring match, ≤10 results | Exact phrase or symbol match | Exact match + confirmed by read |
| FileContent | First result from grep list | Read a ranked candidate | Read the highest-confidence candidate | Read + reader agreed |
| GitLog | `git status` for history query | `git log` without query terms | `git log` with matching commits | Git (authoritative for history) |
| Build | cmake no-op (nothing to build) | cmake builds but not the target | cmake builds the specific target | Build + test both pass |
| Test | ctest reports 0 tests run | ctest passes some tests | ctest passes all relevant tests | Build + test both pass |
| Discovery | Any scan output | Project type correctly identified | Full metadata (type, source count, test presence) | Same result from two scans |
| CIWorkflow | Empty response "[]" | Run list returned | Run list with matching workflow | Run log confirms failure/success |

### The Verified Level

**Verified is not a quality threshold. It is a relationship.**

A tool cannot assign Verified. It is derived by the planner when two
independent evidence producers agree on the same conclusion.

> Verified evidence has been independently corroborated by at least two
> compatible sources, or by one deterministic source whose correctness is
> authoritative for the question being asked.

Definition:

| Condition | Level |
|-----------|-------|
| Single tool, exact match | Strong |
| Two independent tools agree on the same target | Verified |
| Authoritative source (git log, gh run view) | Verified |

Examples:

- `directory-aware find` + `read` both identify the same implementation
  file → **Verified** (two tools, same result)
- `git log` for "what is the last commit" → **Verified** (Git is
  authoritative for commit history)
- `grep` alone → never higher than **Strong** (no corroboration)
- `find` + `grep` both match the same file → **Strong**, not Verified
  (both are search tools sharing the same false-positive signal)
- `gh run view` → **Verified** (GitHub is authoritative for CI results)

The planner computes Verified after tool selection. It is the completion
gate's strongest signal: if every required class is either Strong from a
single tool or Verified from corroboration, the planner can stop.

---

### Evidence Gap Abstraction

Before tool planning can be evidence-driven, the planner needs one more
abstraction:

```
Goal

↓

Evidence Requirements

↓

Evidence Gap    ← missing

↓

Planner

↓

Tool
```

The Evidence Gap is the set of requirements not yet satisfied at the
required quality. The planner does not choose a tool directly from the
Goal. It first computes:

> "I need FileSearch at Strong+"
> "I have FileSearch at Moderate"
> "Gap: need Strong"
> "Tool: directory-aware find (produces Strong)"

This separation ensures tool planning becomes an implementation detail
of evidence acquisition, not the planner's primary logic.

---

## Part II: Current Evidence Audit

### The Four Questions

For each class:

1. **What exactly satisfies it?** -- Today's implementation.
2. **Can weak evidence satisfy it?** -- Can a tool return something and the
   class be marked even though the result is useless?
3. **Can unrelated evidence satisfy it?** -- Can the class be marked by
   evidence that has nothing to do with the user's query?
4. **Can duplicated evidence satisfy it?** -- Can the same underlying fact
   mark the class more than once, or can multiple classes be marked by the
   same fact?

---

### EvidenceClass::FileSearch

#### Producers

| Tool | Condition |
|------|-----------|
| `grep` | output is not "no matches" |
| `references` | output is not "no matches" |
| `find` | output is not "no matches" |

#### Current satisfaction semantics

`evidence.classes` contains `FileSearch` if any of the three tools returned
non-empty output at least once. Idempotent.

#### Q1: What exactly satisfies it?

Any tool call that produces at least one character of output. No minimum match
count, no match-quality threshold, no distinction between exact match and
substring match.

#### Q2: Can weak evidence satisfy it?

**Yes.** `grep "evidence gating"` executes as two independent searches.
"evidence" exists in the codebase; "gating" does not. FileSearch is marked
because the first search succeeded. The model cannot distinguish:
- "found a file containing the exact multi-word term"
- "found files where each word appears somewhere independently"

#### Q3: Can unrelated evidence satisfy it?

**Yes.** Any file containing any substring of the query triggers marking.
"search for xqkz_2024_nonexistent_class" -- grep tokenizes and finds files
containing "search" or "class". FileSearch is marked for a nonexistent class.

#### Q4: Can duplicated evidence satisfy it?

**No** -- idempotent. But multiple tools can independently mark the same
class. The planner cannot know whether FileSearch was satisfied by an exact
`references` match or a broad tokenized `grep`.

---

### EvidenceClass::FileContent

#### Producers

| Tool | Condition |
|------|-----------|
| `read` | output is not "no files to read" |

#### Current satisfaction semantics

`evidence.classes` contains `FileContent` if `read` returned file contents at
least once.

#### Q1: What exactly satisfies it?

Any call to `read` that successfully opens and reads a file. No check that
the file is related to the query or was the intended target.

#### Q2: Can weak evidence satisfy it?

**Yes.** If grep returns 1,000 files and read picks the first one arbitrarily,
any one file satisfies FileContent. "provider credentials" + `grep "provider"`
returns 30 files; `read` opens `provider_auth.h` which contains "provider" but
does not discuss credential configuration.

#### Q3: Can unrelated evidence satisfy it?

**Yes.** For "search for xqkz_2024_nonexistent_class" -- grep tokenizes to
`xqkz`, `2024`, `nonexistent`, `class` -- "class" matches `session.h` which
contains "class". FileContent is marked for an unrelated file.

#### Q4: Can duplicated evidence satisfy it?

**No** -- idempotent.

---

### EvidenceClass::GitLog

#### Producers

| Tool | Condition |
|------|-----------|
| `git` | output is not empty |

#### Current satisfaction semantics

Any `git` command that produces output.

#### Q1: What exactly satisfies it?

Any `git` command with output. `git status` satisfies GitLog. So does
`git log --oneline -1`.

#### Q2: Can weak evidence satisfy it?

**Yes.** `git status` output for a "what is the last commit" query is weak --
it shows the branch and dirty state but not the commit history.

#### Q3: Can unrelated evidence satisfy it?

Rare. Tool selection is query-driven, so git is only called for git-related
queries. But nothing prevents it.

#### Q4: Can duplicated evidence satisfy it?

**No** -- idempotent.

---

### EvidenceClass::Build

#### Producers

| Tool | Condition |
|------|-----------|
| `cmake` | output does not contain "error" |

#### Current satisfaction semantics

`cmake` exits with output that does not contain the substring "error".

#### Q1: What exactly satisfies it?

A cmake invocation that does not print "error". A cache-hit no-op (nothing to
build) is treated identically to a full rebuild.

#### Q2: Can weak evidence satisfy it?

**Yes.** `cmake --build .` may succeed trivially when nothing changed. The
model treats a no-op as equivalent to evidence that the build works.

#### Q3: Can unrelated evidence satisfy it?

Rare. cmake is only called when the planner needs build context.

#### Q4: Can duplicated evidence satisfy it?

**No** -- idempotent.

---

### EvidenceClass::Test

#### Producers

| Tool | Condition |
|------|-----------|
| `ctest` | output does not contain "failed" or "FAILED" |

#### Current satisfaction semantics

`ctest` output does not contain "failed".

#### Q1: What exactly satisfies it?

A ctest invocation that does not print "failed". `ctest` reporting "0 tests
passed" with no failures is marked the same as "all 500 tests passed".

#### Q2: Can weak evidence satisfy it?

**Yes.** Zero tests run with no failures is not meaningful evidence.

#### Q3: Can unrelated evidence satisfy it?

Rare. Same as Build.

#### Q4: Can duplicated evidence satisfy it?

**No** -- idempotent.

---

### EvidenceClass::Discovery

#### Producers

| Tool | Condition |
|------|-----------|
| `discovery` | output is not empty |

#### Current satisfaction semantics

Any `discovery` tool output. The tool always returns project metadata.

#### Q1: What exactly satisfies it?

Any scan output. The tool always returns something (project type, source
count, test presence).

#### Q2: Can weak evidence satisfy it?

**Yes.** discovery always returns something. The class is always marked if
discovery is called. The risk is not weak evidence but **insufficient**
detail -- a project may be too large for discovery to describe meaningfully.

#### Q3: Can unrelated evidence satisfy it?

**No** -- discovery only runs when the planner asks for it.

#### Q4: Can duplicated evidence satisfy it?

**No** -- idempotent.

---

### EvidenceClass::CIWorkflow

#### Producers

| Tool | Condition |
|------|-----------|
| `gh` | output is not empty |

#### Current satisfaction semantics

Any `gh` command that produces output.

#### Q1: What exactly satisfies it?

Any GitHub CLI output. `gh run list` returning an empty array "[]" produces
output and CIWorkflow is marked even though no runs exist.

#### Q2: Can weak evidence satisfy it?

**Yes.** Empty response produces the same class as a detailed run log.

#### Q3: Can unrelated evidence satisfy it?

**Yes.** `gh pr list` marks CIWorkflow even for non-CI queries. The tool name
`gh` is overloaded.

#### Q4: Can duplicated evidence satisfy it?

**No** -- idempotent.

---

## Part III: Cross-Cutting Observations

### 1. Evidence is purely syntactic

Every producer checks one thing: "was there output?" Not "was the output
relevant to the query?" The model cannot distinguish an exact function name
match from a broad English word match. Both mark `FileSearch`.

### 2. Tokenization leaks

Grep and find tokenize queries into space-separated terms. "evidence gating"
becomes two independent searches. If either term exists anywhere, FileSearch
is marked. Require **phrase-level matching** for multi-word queries.

### 3. No evidence provenance

`evidence.classes` is a flat `std::vector<EvidenceClass>`. No information
about which tool produced it, how many matches, match quality, or exact vs
partial. EvidenceStore has separate `facts` and `modified_files` fields, but
nothing ties a class back to its source.

### 4. The six failing benchmarks share one pattern

```
multi-word query term
    ↓
tool tokenizes into independent words
    ↓
tool finds matches for individual words (not the phrase)
    ↓
FileSearch is marked (any output suffices)
    ↓
FileContent is marked (first match read)
    ↓
both classes satisfied → completion = true
    ↓
expected InsufficientEvidence but got success
```

No amount of tool tuning can fix this. The fix is in how completion evaluates
evidence, not in how tools collect it.

### 5. Fix requires more than better grep

The six failures involve four different tool behaviors:
- **Fuzzy grep** ("evidence gating" → matches "evidence")
- **Tokenized find** ("startup model selection" → returns "startup.cpp")
- **Broad grep** ("provider credentials configured" → matches "provider")
- **Nonexistent terms** (xqkz_* → partial token matches)

A single threshold will not fix all six. The quality model handles each
differently because each has a different quality profile.

---

## Part IV: The New Architecture

### Evidence Provenance Schema

The current `std::vector<EvidenceClass> classes` is replaced with a
provenance-bearing structure:

```cpp
struct EvidenceEntry {
  EvidenceClass type;

  Tool tool;            // which tool produced this ("grep", "find", "git", etc.)
  std::string query;    // the original query text passed to the tool
  std::string target;   // what was searched (e.g., a filename, symbol, or phrase)

  EvidenceQuality quality;
  EvidenceStrength strength;

  std::vector<std::string> sources;  // files or resources examined

  // Metadata
  int match_count;
  int exact_match_count;
  bool phrase_match;
};
```

### Quality and Strength

```cpp
enum EvidenceQuality { QualNone, Weak, Moderate, Strong, Verified };
enum EvidenceStrength { StrNone, Low, Medium, High };
```

Quality and strength are set independently by each tool after execution.

For grep:
- If query matched as exact phrase → quality = Strong
- If query matched as identifier (code symbol) → quality = Strong
- If query matched as case-sensitive substring, ≤10 results → quality = Moderate
- If query matched via tokenized grep, >10 results → quality = Weak
- No matches → quality = None
- match_count > 50 → strength = High; > 10 → Medium; > 0 → Low

> **Note:** Match-count-based quality (≤10→Moderate, >10→Weak) is a
> temporary heuristic. Quality should eventually depend on match semantics:
> - exact phrase → Strong
> - identifier → Strong
> - substring, ≤10 results → Moderate
> - substring, >10 results → Weak
> - token overlap → Weak
>
> Ten imprecise matches are not better than one exact match.
> This refinement belongs in Phase 4 once tools can report match
> semantics alongside output.

For find:
- Exact filename match → quality = Strong
- Partial filename match → quality = Moderate
- Token-based match → quality = Weak
- No matches → quality = None

For references:
- Symbol has > 5 references → quality = Strong; > 0 → Moderate; none → None

### Completion Depends on Minimum Quality

`evidence_for_goal()` returns both the required class AND the minimum quality:

```cpp
struct EvidenceRequirement {
  EvidenceClass ec;
  EvidenceQuality min_quality;
};

std::vector<EvidenceRequirement> evidence_for_goal(const Goal &goal);
```

| Intent | FileSearch min | FileContent min | Discovery min | Other |
|--------|---------------|-----------------|---------------|-------|
| Locate | Strong | Moderate | -- | -- |
| Navigate | Strong | Moderate | -- | -- |
| Explain | Strong | Moderate | Moderate | -- |
| Review | Strong | Moderate | Moderate | -- |
| Diagnose | Strong | Moderate | -- | CIWorkflow: Strong |
| Compare | Strong | Moderate | -- | -- |
| Status | -- | -- | -- | GitLog: Moderate |
| Execute | -- | -- | -- | Build: Strong, Test: Strong |
| Modify | Strong | Strong | Moderate | Build: Verified, Test: Verified |
| Chat | -- | -- | -- | -- |

When checking completion:

```cpp
bool check_completion_goal(const Goal &goal, EvidenceStore &evidence) {
  auto requirements = evidence_for_goal(goal);
  for (auto &req : requirements) {
    if (!evidence.has_quality(req.ec, req.min_quality))
      return false;
  }
  return true;
}
```

### Confidence Derivation

Confidence is no longer computed from tool execution metrics. It is derived
from three properties of the collected evidence:

```
Confidence
    ↓
evidence quality      -- the minimum quality across all required classes
    ↓
evidence coverage     -- what fraction of required classes are satisfied
    ↓
evidence agreement    -- do independent evidence sources agree?
```

Derivation rules:

| Condition | Confidence Level |
|-----------|-----------------|
| All required classes at Verified | Complete |
| All required at Strong or Verified | High |
| All required at Moderate or better | Medium |
| Some required at Weak | Low |
| One or more required at None | None |

Confidence is explainable: "confidence is Medium because Codebase evidence
is only Moderate (grep found partial matches, not exact)."

No formula. No floats. No category-weighted combine with convergence bonus.

### Planner Impact

`select_next_tool()` no longer asks "which tool should I use?" It asks:

```
Which evidence class is still missing?
```

Then maps:

```
Missing FileSearch at Strong quality
    ↓
directory-aware find (highest quality) or references (if symbol)
```

```
Missing GitLog at Moderate quality
    ↓
git log --oneline -10 (already works)
```

```
Missing Discovery at Moderate quality
    ↓
discovery tool (already works)
```

Tools become implementations of evidence acquisition, not components of a
goal type switch.

---

## Part V: Audit Summary

| Class | Weak evidence possible? | Unrelated possible? | Duplicated possible? | Fix required? |
|-------|------------------------|---------------------|----------------------|---------------|
| FileSearch | Yes (tokenized grep) | Yes (substring match) | No | **Yes** -- quality levels |
| FileContent | Yes (wrong file read) | Yes (arbitrary file) | No | **Yes** -- provenance tracking |
| GitLog | No | Rare | No | No |
| Build | Yes (no-op cmake) | Rare | No | **Yes** -- check real build |
| Test | Yes (0 tests run) | Rare | No | **Yes** -- check test count > 0 |
| Discovery | Yes (always succeeds) | No | No | No |
| CIWorkflow | Yes (empty response) | Rare | No | **Yes** -- check non-empty data |

The six benchmark failures are all fixed by the same change: FileSearch
completion requires at least Moderate quality, and none of the six failing
queries can produce FileSearch at Moderate or above (the terms they search
for do not exist as exact or near-exact matches in the codebase).

---

## Part VI: Migration Path

### Step 1: Data model

Replace `std::vector<EvidenceClass> classes` with
`std::vector<EvidenceEntry> entries` in `EvidenceStore`. Add
`EvidenceQuality` and `EvidenceStrength` enums. Add
`evidence.has_quality(ec, min_quality)`.

### Step 2: Producers

Each tool call site (grep, find, references, read, etc.) sets quality and
strength based on its output characteristics. Provenance data (tool, query,
target, match counts) is recorded in the `EvidenceEntry`.

### Step 3: Evidence requirements

Extend `evidence_for_goal()` to return `EvidenceRequirement` with
`min_quality` instead of bare `EvidenceClass`. Update
`check_completion_goal()` to use `has_quality()`.

### Step 4: Remove old confidence

Delete the `ConfidenceService` class. Replace the confidence derivation
with the three-property model (quality, coverage, agreement).

### Step 5: Planner reroute

Change `select_next_tool()` to iterate over missing evidence requirements
and pick the tool that best satisfies each.

### Step 6: Remove GoalType

Once evidence-driven tool planning matches or exceeds GoalType-based routing,
delete the keyword routing and `required_evidence()`.

---

## Part VII: Architectural Invariants

1. **Evidence quality is set by tools, evaluated by the planner.** Tools
   report what they found and how well. The planner decides whether it is
   enough.

2. **Quality and strength are always separated.** No tool sets a single
   "score." Every piece of evidence has two independent dimensions.

3. **The vocabulary is fixed.** If a value is not one of the five quality
   levels or four strength levels, it is not a valid evidence rating.

4. **Confidence is derived, not computed.** No formulas. No numeric
   combination. Explainable from three named properties.

5. **Verified requires corroboration.** The highest quality level requires
   either two independent sources agreeing or one authoritative source.
   Single-tool grep can never reach Verified.

6. **Tools implement evidence acquisition.** The planner does not ask
   "what GoalType is this?" It asks "which evidence class is still
   missing?"
