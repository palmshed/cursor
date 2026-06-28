---
---

# Intent Taxonomy

Derived from 114 prompts in the corpus (see `prompt_corpus.csv`).

## 1. EXPLAIN -- "how does this work", "explain the architecture"

The user wants a conceptual understanding of how something works.

**Variants:**
- architecture / design questions: *"explain the architecture"*, *"how is this agent designed"*
- mechanism questions: *"how does the build system work"*, *"how does session run work"*
- overview questions: *"tell me about this project"*, *"what is this repository"*

**Current GoalType mapping:** CodebaseOverview, sometimes CodebaseQuery

**Failure pattern:** Some EXPLAIN queries leak to CodebaseQuery (tools run but high-level overview is what the user wanted). The planner finds files but produces a narrow answer.

---

## 2. LOCATE -- "where is X", "find X defined"

The user wants to find a specific file, symbol, definition, or usage.

**Variants:**
- symbol declaration: *"where is DiscoveryService defined"*, *"find the Outcome enum"*
- symbol usage: *"where is ReplayService used"*
- code search: *"grep Agent"*, *"search for confidence scoring logic"*
- file read: *"read file include/app/command_router.h"*

**Current GoalType mapping:** CodebaseQuery

**Failure pattern:** Low. LOCATE is the best-handled intent.

---

## 3. REVIEW -- "review architecture", "audit technical debt"

The user wants a structured analysis, not just an explanation.

**Variants:**
- architecture audit: *"review architecture"*, *"audit technical debt"*
- codebase audit: *"review codebase"*, *"review recent changes"*

**Current GoalType mapping:** ArchitectureReview

**Failure pattern:** ArchitectureReview runs a special pipeline with predefined grep queries. If the planner classifies into CodebaseOverview instead, the user gets a less structured answer.

---

## 4. STATUS -- "last commit", "git status", "what changed"

The user wants to know the current state of the repository.

**Variants:**
- commit history: *"what is the last commit"*, *"show recent commits"*
- working tree: *"what changed"*, *"show changed files"*, *"modified files"*
- branch info: *"current branch"*, *"what branch am I on"*
- git log: *"git history"*, *"commit history"*
- session state: *"what provider am I using"*, *"what model am I on"*

**Current GoalType mapping:** CommitHistory, SessionState

**Failure pattern:** The most brittle classifier. Every phrasing had to be added as a keyword. Before the catch-all fix, queries like *"check if any files changed"* leaked to GeneralChat (zero evidence collected). *"what files are modified"* and *"show me uncommitted changes"* still fail -- no keyword matches.

---

## 5. DIAGNOSE -- "why did CI fail", "find why X is triggered"

The user wants to identify the root cause of a failure.

**Variants:**
- CI failure: *"why did CI fail"*, *"which test failed"*
- code problem: *"find why InsufficientEvidence is triggered"*
- GitHub Actions: *"can you check this log https://github.com/..."*

**Current GoalType mapping:** CICheck, GitHubInvestigation, CodebaseQuery

**Failure pattern:** CICheck needs to distinguish between *"why did CI fail"* (generic diagnostic) and *"check this specific GitHub Actions run"* (URL investigation). The classifier does this, but the LLM mode can hallucinate the category.

---

## 6. COMPARE -- "what is the difference between X and Y"

The user wants a comparison.

**Current GoalType mapping:** GeneralChat (excluded from codebase queries)

**Failure pattern:** Correctly classified as GeneralChat, but this means NO investigation runs. If the comparison involves code in the repo (e.g., *"what's the difference between Service A and Service B"*), the user gets a generic AI answer with no evidence.

---

## 7. NAVIGATE -- "read file X", "show memory"

The user wants to directly access a resource.

**Variants:**
- file read: *"read file include/app/command_router.h"*
- memory inspection: *"show memory"*
- todo list: *"show todos"*

**Current GoalType mapping:** CodebaseQuery, or direct command routing

**Failure pattern:** Low. These are often caught early by direct command routing (`map_nl_to_direct_command`).

---

## 8. MODIFY -- "add X", "refactor Y", "fix Z"

The user wants to change code.

**Variants:**
- add: *"add a new CLI command"*, *"create a new file"*
- modify: *"refactor the authentication service"*, *"update the fmt dependency"*
- fix: *"fix the failing unit test"*, *"fix the broken CMakeLists.txt"*

**Current GoalType mapping:** CodeChange

**Failure pattern:** CodeChange queries that include "how" or "where" can be misclassified: *"how do I add a new command"* → GeneralChat (because of GeneralChat exclusion patterns).

---

## 9. EXECUTE -- "run make test", "build the project"

The user wants to run a shell command or build.

**Current GoalType mapping:** CodeChange (via direct command routing)

**Failure pattern:** *"build the project"* → CodeChange but no direct command match, goes through the full investigation loop. This is fine but slower than direct execution.

---

## 10. CHAT -- "how are you", "hello", "what can you do"

The user is having a conversation, not asking about the repo.

**Current GoalType mapping:** GeneralChat

**Failure pattern:** Low -- these are handled well.

---

# Clusters of the Same Semantic Goal

These groups of prompts should all map to the same Goal:

**Group A: Working tree status**
```
show changed files
what changed
modified files
what files changed
what files are modified
show modified files
check the files changed
check changed files
did I edit anything
what are the current changes
check if any files changed
show me uncommitted changes
```
→ Intent: STATUS, Entity: working tree, Artifact: modified files

**Group B: Git history**
```
what is the last commit
can you check the last commit
show me the latest commit
what was the last commit
show recent commits
check commit
previous commit
get the latest commit
recent commits
```
→ Intent: STATUS, Entity: git history, Artifact: recent commits

**Group C: Architecture understanding**
```
explain the architecture
how is this agent designed
walk me through the design
how does this work
how does the build system work
tell me how repository investigation works
```
→ Intent: EXPLAIN, Entity: architecture/design, Artifact: structural overview

**Group D: Code location**
```
find CommandRouter
where is DiscoveryService defined
grep Agent
search for confidence scoring logic
where do we call gh run view
```
→ Intent: LOCATE, Entity: code symbol, Artifact: definition/usage

**Group E: CI diagnosis**
```
why did CI fail
which test failed
check my CI workflow
what is the status of my github action
```
→ Intent: DIAGNOSE, Entity: CI pipeline, Artifact: failure reason

---

# Observations

1. **STATUS is the most fragmented intent** -- 5 different GoalTypes (CommitHistory, SessionState, CodebaseQuery, CodebaseOverview, GeneralChat) depending on phrasing. A single `STATUS` intent with different entities would be cleaner.

2. **LOCATE is the most mature intent** -- consistently maps to CodebaseQuery, tools work well, confidence is high.

3. **EXPLAIN and REVIEW overlap** -- *"review architecture"* is EXPLAIN + structured output. The difference is in the expected answer format, not the investigation.

4. **COMPARE is orphaned** -- classified as GeneralChat, so it collects zero evidence. It should be a real intent that triggers evidence collection.

5. **The entity dimension is missing** -- *"explain the architecture"* vs *"explain how auth works"* are the same intent (EXPLAIN) with different entities (architecture vs auth system). The planner should differentiate by entity, not by changing the intent classification.
