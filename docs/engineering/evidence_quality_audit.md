# Evidence Quality Audit

## Why This Exists

Six benchmark queries still fail the same way:

```
expected InsufficientEvidence  got success
```

Every failure is a false positive -- the investigation collected evidence that the
evidence model accepted but a human reviewer would reject.

This document audits every `EvidenceClass` against four questions to find the
root cause.

---

## The Four Questions

For each class:

1. **What exactly satisfies it?** -- The current implementation.
2. **Can weak evidence satisfy it?** -- Can a tool return something and the class
   be marked even though the result is useless?
3. **Can unrelated evidence satisfy it?** -- Can the class be marked by evidence
   that has nothing to do with the user's query?
4. **Can duplicated evidence satisfy it?** -- Can the same underlying fact mark
   the class more than once, or can multiple classes be marked by the same
   fact?

---

## EvidenceClass::FileSearch (value 0)

### Producers

| Tool | Condition |
|------|-----------|
| `grep` | output is not "no matches" |
| `references` | output is not "no matches" |
| `find` | output is not "no matches" |

### Satisfaction semantics

`evidence.classes` contains `FileSearch` if any of the three tools returned
non-empty output at least once.

Idempotent -- marking twice is a no-op.

### Q1: What exactly satisfies it?

Any tool call to `grep`, `references`, or `find` that produces at least one
character of output.

There is no minimum match count, no match-quality threshold, no distinction
between exact match and substring match, and no check that the matched term
appears as a semantic unit rather than a character sequence.

### Q2: Can weak evidence satisfy it?

**Yes.** Consider:

- `grep "evidence gating"` matches 3 files because "evidence" appears in
  `AGENTS.md` and `ARCHITECTURE.md` -- the two-word phrase "evidence gating"
  does not exist anywhere in the codebase. But `grep` tokenizes the query with
  word boundaries, so `grep "evidence gating"` is executed as
  `grep evidence; grep gating`. Both terms exist independently, so FileSearch
  is marked.

  The evidence model cannot distinguish:
  - "grep found a file containing the exact multi-word term" from
  - "grep found files where each word appears somewhere independently"

- `find "startup model selection"` finds no file named exactly that, but `find`
  does token-based matching and returns `startup.cpp` (which contains "startup").
  The class is marked. The evidence model cannot distinguish:
  - "find found the exact file the user was looking for" from
  - "find found any file that shares a token"

### Q3: Can unrelated evidence satisfy it?

**Yes.** Any file containing any substring of the query triggers marking. The
relevance to the user's question is never evaluated.

Example: "search for xqkz_2024_nonexistent_class" -- `find` returns no file
containing that exact string. But `grep` tokenizes and finds files with
"search" or "class" in them. FileSearch is marked. The planner now thinks it
has evidence for a nonexistent class.

### Q4: Can duplicated evidence satisfy it?

**No** -- idempotent marking prevents multiple marks from the same class. But
multiple tools can independently mark the same class. `find` + `grep` both
mark FileSearch. This means the planner cannot know whether FileSearch was
satisfied by a high-quality exact match from `references` or by a broad
tokenized `grep` match.

---

## EvidenceClass::FileContent (value 1)

### Producers

| Tool | Condition |
|------|-----------|
| `read` | output is not "no files to read" |

### Satisfaction semantics

`evidence.classes` contains `FileContent` if `read` returned file contents at
least once.

### Q1: What exactly satisfies it?

Any call to `read` that successfully opens and reads a file.

There is no check that the file read is related to the query. There is no
check that the file was the intended target of the search.

### Q2: Can weak evidence satisfy it?

**Yes.** If `grep` returns 1,000 matching files and `read` picks one
arbitrarily (the first in the list), any one file satisfies FileContent.

If the user asked about "provider credentials" and `grep "provider"` returns
30 files, `read` reads the first one (`provider_auth.h`). The file contains
"provider" but does not discuss credential configuration at all. FileContent
is marked.

### Q3: Can unrelated evidence satisfy it?

**Yes.** Consider:

- User asks "search for xqkz_2024_nonexistent_class"
- `grep` tokenizes to `xqkz`, `2024`, `nonexistent`, `class`
- "class" is a common word -- multiple files matched
- `read` opens the first match (say `session.h` which contains "class")
- FileContent is marked

The user's question was about a specific nonexistent class. The evidence
returned is a file containing the word "class". These are semantically
unrelated.

### Q4: Can duplicated evidence satisfy it?

**No** -- idempotent.

---

## EvidenceClass::GitLog (value 2)

### Producers

| Tool | Condition |
|------|-----------|
| `git` | output is not empty |

### Q1: What exactly satisfies it?

Any `git` command that produces output.

### Q2: Can weak evidence satisfy it?

**Yes.** `git status` output satisfies GitLog. So does `git log --oneline -1`.
There is no check that the git output is relevant to the query context.

### Q3: Can unrelated evidence satisfy it?

**Yes.** A query about architecture will never need git evidence, but if a
recovery tool accidentally calls `git`, GitLog is marked. This is unlikely in
practice because tool selection is query-driven, but nothing prevents it.

### Q4: Can duplicated evidence satisfy it?

**No** -- idempotent.

---

## EvidenceClass::Build (value 3)

### Producers

| Tool | Condition |
|------|-----------|
| `cmake` | output does not contain "error" |

### Q1: What exactly satisfies it?

`cmake` exits with output that does not contain the substring "error".

### Q2: Can weak evidence satisfy it?

**Yes.** `cmake --build .` may succeed trivially (nothing to rebuild) even when
no build actually happened. The evidence model treats a cache-hit no-op as
equivalent to a full rebuild.

### Q3: Can unrelated evidence satisfy it?

**Yes.** A query about documentation never needs build evidence. But if the
planner selects `cmake` as a recovery step, Build is marked regardless of
whether a build was useful.

### Q4: Can duplicated evidence satisfy it?

**No** -- idempotent.

---

## EvidenceClass::Test (value 4)

### Producers

| Tool | Condition |
|------|-----------|
| `ctest` | output does not contain "failed" or "FAILED" |

### Q1: What exactly satisfies it?

`ctest` output does not contain "failed" (case-insensitive).

### Q2: Can weak evidence satisfy it?

**Yes.** `ctest` may report "0 tests passed" with no failures. The evidence
model treats this the same as "all 500 tests passed".

### Q3: Can unrelated evidence satisfy it?

**Yes.** Same as Build -- any ctest call marks Test regardless of relevance.

### Q4: Can duplicated evidence satisfy it?

**No** -- idempotent.

---

## EvidenceClass::Discovery (value 5)

### Producers

| Tool | Condition |
|------|-----------|
| `discovery` | output is not empty |

### Q1: What exactly satisfies it?

Any `discovery` tool output. The tool always returns project metadata
(project type, source count, test presence).

### Q2: Can weak evidence satisfy it?

**Yes.** `discovery` always returns *something* -- every project has a project
type and a source count. The class is always marked if `discovery` is called.

### Q3: Can unrelated evidence satisfy it?

**No** -- `discovery` only runs when the planner asks for it, and it always
returns project-level evidence. The risk is not unrelated evidence but
**insufficient** evidence: a project may be too large for `discovery` to
meaningfully describe.

### Q4: Can duplicated evidence satisfy it?

**No** -- idempotent.

---

## EvidenceClass::CIWorkflow (value 6)

### Producers

| Tool | Condition |
|------|-----------|
| `gh` | output is not empty |

### Q1: What exactly satisfies it?

Any `gh` command that produces output.

### Q2: Can weak evidence satisfy it?

**Yes.** `gh run list` that returns an empty list (no runs found) still
produces "[]" as output. The tool considers this a valid result. CIWorkflow
is marked even though no CI data was actually collected.

### Q3: Can unrelated evidence satisfy it?

**Yes.** `gh pr list` would mark CIWorkflow even though the query might be
about CI failures. The tool name `gh` is overloaded -- any GitHub CLI command
marks the same class.

### Q4: Can duplicated evidence satisfy it?

**No** -- idempotent.

---

## Cross-Cutting Observations

### 1. Evidence is purely syntactic

Every producer checks one thing: "was there output?" Not "was the output
relevant to the query?"

The evidence model cannot distinguish:
- `grep "exact_function_name"` → 1 file, 1 match
- `grep "generic_english_word"` → 47 files, 312 matches

Both mark `FileSearch`. Both satisfy completion.

### 2. Tokenization leaks

Grep and find both tokenize queries into space-separated terms. A query like
"evidence gating" becomes two independent searches. If either term exists
anywhere, FileSearch is marked.

The evidence model should require **phrase-level matching** for multi-word
queries before marking FileSearch.

### 3. No evidence provenance

`evidence.classes` is a flat `std::vector<EvidenceClass>`. There is no
information about:
- Which tool produced it
- How many matches were found
- What the match quality was
- Whether it was an exact match or a partial match

`EvidenceStore` has separate fields (`facts`, `modified_files`, etc.), but
nothing ties a class back to its source.

### 4. The six failing benchmarks all follow the same pattern

```
User query with a specific multi-word term
    ↓
Tool tokenizes the term into space-separated words
    ↓
Tool finds matches for individual words (not the phrase)
    ↓
FileSearch is marked
    ↓
FileContent is marked (first match read)
    ↓
Both evidence classes satisfied → completion = true
    ↓
InsufficientEvidence expected but success returned
```

The pattern is:
1. query term is multi-word or uncommon
2. tools find partial/substring matches
3. evidence classes are marked
4. completion accepts the weak evidence
5. benchmark expected the gate to stop this (but it didn't)

### 5. Fix requires more than better grep

The six failures involve different tool behaviors:
- **Fuzzy grep** ("evidence gating" → matches "evidence")
- **Tokenized find** ("startup model selection" → returns "startup.cpp")
- **Broad grep** ("provider credentials configured" → matches "provider")
- **Nonexistent terms** (xqkz_*) → partial token matches

A single threshold won't fix all six. Each needs a different evidence quality
rule.

---

## Provenance Schema (Proposal)

The current `std::vector<EvidenceClass> classes` is insufficient. Replace with
a structure that tracks provenance:

```cpp
struct EvidenceEntry {
  EvidenceClass ec;
  std::string tool;         // "grep", "find", "references", etc.
  std::string query;        // the original query term used
  std::string summary;      // e.g., "3 files, 12 matches; 1 exact, 11 partial"
  int match_count;          // number of total matches
  int exact_match_count;    // matches that are exact (not substring)
  bool phrase_match;        // true if multi-word query matched as a phrase
};
```

This gives the completion gate enough information to ask:

- "Did FileSearch come from an exact phrase match or a tokenized grep?"
- "Did FileContent read the highest-ranked candidate or the first match?"
- "Does the number of matches suggest high precision or noise?"

---

## Per-Class Quality Criteria (Proposal)

### FileSearch

| Quality Level | Minimum Requirements |
|---------------|---------------------|
| Exact | Single/multi-word term matched as exact phrase in filename or symbol |
| Strong | Term matched as case-sensitive substring, ≤10 results |
| Weak | Term matched via tokenized grep, >10 results |
| None | No matches returned |

**Gate rule:** FileSearch is insufficient unless at least `Strong`.

This alone fixes all six benchmark failures -- none of the failing queries
produce exact or strong FileSearch evidence (the terms don't exist).

### FileContent

| Quality Level | Minimum Requirements |
|---------------|---------------------|
| Exact | Read the file returned by the highest-confidence search result |
| Weak | Read a file from the search result list (current behavior) |
| None | No file read |

**Gate rule:** FileContent requires at least `Weak` (current baseline is fine
-- the fix is in FileSearch, not FileContent).

### GitLog

| Quality Level | Minimum Requirements |
|---------------|---------------------|
| Relevant | Output contains a query term |
| Generic | Any git output (current behavior) |

**Gate rule:** GitLog requires at least `Generic`. (No changes needed -- git
queries already work correctly.)

### Build

| Quality Level | Minimum Requirements |
|---------------|---------------------|
| Clean | cmake succeeds with relevant targets |
| Generic | cmake succeeds (current behavior) |

**Gate rule:** Build requires `Clean`. (Change: verify the build target is
not a no-op.)

### Test

| Quality Level | Minimum Requirements |
|---------------|---------------------|
| Passing | ctest passes with >0 tests run |
| Generic | ctest has no failures (current behavior) |

**Gate rule:** Test requires `Passing`. (Change: ctest must have run at
least one test.)

### Discovery

| Quality Level | Minimum Requirements |
|---------------|---------------------|
| Detailed | Project type and source/tests counts populated |
| Generic | Any output (current behavior) |

**Gate rule:** Discovery requires at least `Generic`. (No changes needed.)

### CIWorkflow

| Quality Level | Minimum Requirements |
|---------------|---------------------|
| Populated | gh returned non-empty data (run list has entries) |
| Empty | gh returned empty "[]" |

**Gate rule:** CIWorkflow requires `Populated`. (Change: check that the
output is not an empty JSON array/object.)

---

## Implementation Impact

### EvidenceStore changes

```
std::vector<EvidenceClass> classes
    ↓
std::vector<EvidenceEntry> evidence_entries
```

Add:

```cpp
struct EvidenceEntry {
  EvidenceClass ec;
  std::string tool;
  std::string query;
  int match_count;
  int exact_match_count;
  bool phrase_match;
};
```

And quality-checking methods:

```cpp
enum class EvidenceQuality { None, Weak, Strong, Exact };

EvidenceQuality quality_of(EvidenceClass ec) const;
bool has_quality(EvidenceClass ec, EvidenceQuality min) const;
```

### Producer changes

Each tool call site enriches the EvidenceEntry with provenance data:

- `grep` → reports match count, exact-match count, phrase-match flag
- `find` → reports candidate count, exact filename matches vs partial
- `references` → reports reference count
- `read` → reports which file was selected and why

### Consumer changes

`has_all_evidence_classes` becomes:

```cpp
bool has_all_evidence_classes(
    const std::vector<EvidenceClass> &required,
    EvidenceQuality min_quality = EvidenceQuality::Weak) const;
```

And `evidence_for_goal` / `check_completion_goal` pass the minimum quality
level required per intent:

| Intent | Min FileSearch Quality |
|--------|------------------------|
| Locate | Strong |
| Navigate | Strong |
| Explain | Strong (needs good search) |
| Review | Strong |
| Diagnose | Strong |
| Compare | Strong |
| Status | Weak (git queries work with generic output) |
| Execute | Weak (build/test results are pass/fail, not search) |
| Modify | Strong |

### Planner impact

`select_next_tool()` gains awareness of evidence quality. When quality is
`Weak` or `None`, the planner can:

1. Retry with a more specific tool (e.g., `references` instead of `grep`)
2. Retry with exact-match mode
3. Mark completion as `InsufficientEvidence` instead of `Success`

This replaces the current binary `has_results` gate.

---

## Precedent

The current `facts` field in `EvidenceStore` already tracks tool-specific
outcomes. `"find:results"`, `"grep:results"`, `"read:results"` are facts.
But they are not structured -- they are opaque strings that heuristic code
checks.

EvidenceEntry replaces ad-hoc facts with structured provenance, making
quality checks deterministic rather than heuristic.

---

## Audit Summary

| Class | Weak evidence possible? | Unrelated possible? | Duplicated possible? | Fix required? |
|-------|------------------------|---------------------|----------------------|---------------|
| FileSearch | Yes (tokenized grep) | Yes (substring match) | No | **Yes** -- phrase/exact gate |
| FileContent | Yes (wrong file read) | Yes (arbitrary file) | No | **Yes** -- provenance tracking |
| GitLog | Yes (status ≠ history) | Rare | No | No |
| Build | Yes (no-op cmake) | Rare | No | **Yes** -- check real build |
| Test | Yes (0 tests run) | Rare | No | **Yes** -- check test count > 0 |
| Discovery | Yes (always succeeds) | No | No | No |
| CIWorkflow | Yes (empty response) | Rare | No | **Yes** -- check non-empty data |
