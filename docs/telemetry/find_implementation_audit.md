---
---

# Find Implementation Audit

## 1. Locations

| # | File | Lines | Role |
|---|------|-------|------|
| A | `src/app/command_router.cpp` | 176–363 | Production tool runner (lambda inside `process_user_input`) |
| B | `src/diagnostics/diagnostics.cpp` | 363–393 | Diagnostics tool runner (lambda inside `run_query`) |
| C | `tests/validation_runner.cpp` | 185–228 | Validation test tool runner (lambda) |
| D | `tests/benchmark_runner.cpp` | 73–115 | Benchmark test tool runner (lambda) |

All four are lambdas passed as the `ToolRunner` parameter to
`ExecutionEngine::execute()`. Each lives inside its own translation unit,
capturing different local state.

---

## 2. Behavior Differences

### 2.1 Filesystem scan (same across all four)

```
recursive_directory_iterator(current_path())
skip . files, build/_deps (B also skips nothing extra)
              (A,D also skip node_modules)
```

### 2.2 Extension filter

| Extensions | A (prod) | B (diag) | C (val) | D (bench) |
|------------|:--------:|:--------:|:-------:|:---------:|
| `.cpp` `.h` `.hpp` `.c` | yes | yes | yes | yes |
| `.py` `.js` `.json` `.md` | yes | yes | yes | yes |
| `.yml` `.yaml` | yes | **no** | no | no |
| `.cmake` `.txt` | yes | **no** | no | no |
| `CMakeLists.txt` | yes | yes | yes | yes |

B (diagnostics) also skips `p.find("/scenarios/")` and `p.find("/data/")` in
its *grep* handler (lines 336–347) but NOT in its find handler.

C (validation) and D (benchmark) filter out `benchmark_suite.json` and
`tests/` and `scenarios/` in their *grep* handler, not in find.

### 2.3 Input normalization

| Feature | A (prod) | B (diag) | C (val) | D (bench) |
|---------|:--------:|:--------:|:-------:|:---------:|
| `--impl` flag removal | yes | yes | yes | yes |
| `tolower` | yes | yes | yes | yes |
| CamelCase normalization | **yes** | no | no | no |
| Word extraction (`[ _-]?` splitting) | **yes** | no | no | no |

A produces `term_normalized` (e.g., `CommandRouter` → `command_router`) and
`term_words` (e.g., `cursor[ _-]?binary` → `["cursor","binary"]`). The
other three only have `term_lower`.

### 2.4 Matching and scoring

| Step | A (prod) | B (diag) | C (val) | D (bench) |
|------|:--------:|:--------:|:-------:|:---------:|
| exact filename match (score 20) | stem_lower == term_lower **OR** term_normalized | stem == term_lower only | stem == term_lower only | stem == term_lower only |
| partial filename match (score 10) | stem_lower.find(term_lower) **OR** term_normalized | stem.find(term_lower) only | stem.find(term_lower) only | stem.find(term_lower) only |
| directory path match (score 5) | **yes** (rel_str find) | no | no | no |
| word-level match (score 12) | **yes** (term_words all-in-stem) | no | no | no |
| symbol scan (class/struct/fn) | **yes** (opens file, 80 lines) | no | no | no |
| impl query boost (+8) | **yes** (`.cpp` only) | no | no | no |
| `score < 20` gate on symbol scan | **yes** | n/a | n/a | n/a |

### 2.5 Output format

| Line prefix | A (prod) | B (diag) | C (val) | D (bench) |
|-------------|:--------:|:--------:|:-------:|:---------:|
| `CANDIDATE:` | yes (all candidates) | no | yes (all) | yes (all) |
| `SELECTED:` | yes | no | yes | yes |
| `REASON:` | yes | no | yes | yes |
| `FILES:` | yes (all candidates) | no | yes (all) | yes (all) |
| plain paths only | (also) | **yes** (no other output) | (also) | (also) |

B outputs only bare relative paths, one per line, with exact matches
inserted at front (`found.insert(found.begin(), ...)`).

### 2.6 Read-tool coupling

| Mechanism | A (prod) | B (diag) | C (val) | D (bench) |
|-----------|:--------:|:--------:|:-------:|:---------:|
| Files passed to read | `last_find_results` (top 5) | none (no coupling) | `last_find` (top 1) | `last_find` (top 1) |
| State type | `std::vector<std::string>` | none | `std::string` | `std::string` |

A populates a vector of up to 5 paths. The read handler iterates all of them
(line 426). C and D store a single path string; the read handler reads
exactly that one file.

### 2.7 Execution engine metrics consumption

The execution engine at `execution_engine.cpp:1053–1077` parses the find
tool's `stdout` for `CANDIDATE:`, `SELECTED:`, and `REASON:` lines to
populate `retrieval_metrics`. This means:

- **A, C, D**: output parsed correctly → metrics populated.
- **B**: bare paths only → `CANDIDATE:` lines not found → `trace_candidates`
  stays empty, `filename_hits`/`symbol_hits`/etc. not incremented correctly.

---

## 3. Reuse Feasibility

### 3.1 What can be shared

The **filesystem-scan + scoring + ranking** logic is pure computation:

```
input:  term, impl_flag
output: vector<{path, score, reason}>
```

It has no dependencies on:
- the local `last_find_results` / `last_find` state
- the output format (CANDIDATE vs bare paths)
- the read handler

This can be extracted to a free function in a shared header:

```cpp
// include/services/find_service.h
struct FindCandidate { std::string path; int score; std::string reason; };
std::vector<FindCandidate> directory_aware_find(const std::string &term,
                                                  bool impl_query);
```

### 3.2 What cannot be shared

| Aspect | Reason |
|--------|--------|
| Output formatting | A/C/D need CANDIDATE/SELECTED/FILES; B needs bare paths |
| Read-tool coupling | A needs top-5 vector; C/D need top-1 string; B has none |
| Extension filter | A includes `.yml .yaml .cmake .txt`; B/C/D do not |
| Exclusions | A skips `node_modules`; B/C/D each have different skip logic in grep (not find) |

### 3.3 Breaking the coupling

The read-tool coupling is the real obstacle. A shares a captured vector
between find and read. To share the find function, the tool runner lambda
must still own the vector. This is fine -- the shared function returns
candidates, the lambda selects top-N and stores them locally.

The output format difference means each caller would wrap the shared function
with its own formatting loop. That is a 3–5 line wrapper per caller.

---

## 4. Recommendation

**Share the find logic as a free function; keep the tool runners independent.**

### Why

1. **Single source of truth for ranking.** The scoring rules (exact=20,
   partial=10, directory=5, word-level=12, symbol up to 18, impl boost=8)
   and the ranking logic (descending score, ascending path for ties) would
   live in one place. Currently each copy can drift independently --
   they already have different extension filters and A has features the
   other three lack.

2. **The lambdas are thin wrappers.** Each tool runner's remaining work is:
   - call the shared function
   - output format (6 lines for A/C/D, 2 lines for B)
   - write to state vector (1 line for A, 1 line for C/D, 0 for B)
   Compared to the current 60–90 lines of duplicated scan logic.

3. **The read-tool coupling is not a problem.** The lambda still owns the
   state (vector or string). The shared function doesn't touch it.

### Alternative: keep separate

If the four extension filters genuinely need to stay different (e.g.,
diagnostics truly never needs `.yml` files), then keeping them separate is
acceptable, but B and C/D should at minimum adopt A's CamelCase
normalization, symbol scanning, and word-level matching -- since those
features are bug fixes, not configuration preferences.

### Proposed scope for sharing

```
FindService::directory_aware_find(term, impl_flag)
         ↓
   vector<FindCandidate>   ← pure, testable
         ↓
   caller formats output + manages state
```

No code changes yet.
