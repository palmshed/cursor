#!/usr/bin/env python3
"""
Level 2 Exit Capacity Review.

Generates 100 architecture questions (80 programmatic + 20 manual),
runs them through cursor-agent --json, and collects capacity metrics.

Usage:
  python3 tests/capacity_review.py [--binary ../build/bin/cursor-agent]
"""

import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
import time
from collections import Counter, defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BINARY = ROOT / "build" / "bin" / "cursor-agent"

# ---------------------------------------------------------------------------
# 1. Codebase introspection helpers
# ---------------------------------------------------------------------------

def list_files(root, pattern=r"\.(cpp|h)$"):
    """List all source files matching pattern."""
    files = []
    for f in root.rglob("*"):
        if f.is_file() and re.search(pattern, f.name) and "_deps" not in f.parts:
            files.append(f)
    return sorted(files)

def extract_symbols(root):
    """Extract class/function names from source files."""
    symbols = {"classes": [], "functions": [], "files": []}
    for f in list_files(root):
        rel = f.relative_to(root)
        symbols["files"].append(str(rel))
        content = f.read_text(errors="replace")
        for m in re.finditer(r"\bclass\s+(\w+)", content):
            symbols["classes"].append((m.group(1), str(rel)))
        for m in re.finditer(r"\bstruct\s+(\w+)", content):
            symbols["classes"].append((m.group(1), str(rel)))
        for m in re.finditer(
            r"\b(bool|void|int|double|std::string|size_t|auto)\s+(\w+)\s*\(",
            content,
        ):
            symbols["functions"].append((m.group(2), str(rel)))
    return symbols


def find_component_ownership(root):
    """Determine which component owns each file."""
    ownership = {}
    for f in sorted(root.rglob("*.cpp")):
        if "_deps" in f.parts:
            continue
        content = f.read_text(errors="replace")
        # Find namespace declarations
        namespaces = re.findall(r"^namespace\s+(\w+)", content, re.MULTILINE)
        rel = f.relative_to(root)
        if "Core::" in content or "namespace Core" in content:
            ownership[str(rel)] = "Core"
        elif "Services::" in content or "namespace Services" in content:
            ownership[str(rel)] = "Services"
        elif namespace := (namespaces or [None])[0]:
            ownership[str(rel)] = namespace
        else:
            ownership[str(rel)] = "unknown"
    return ownership


def find_callers(root, target_function):
    """Find callers of a function."""
    callers = []
    for f in list_files(root):
        content = f.read_text(errors="replace")
        for line in content.split("\n"):
            if target_function in line and not line.strip().startswith("//"):
                callers.append((line.strip(), str(f.relative_to(root))))
    return callers


def find_config_references(root):
    """Find config/environment references."""
    refs = defaultdict(list)
    for f in list_files(root):
        content = f.read_text(errors="replace")
        for m in re.finditer(r'(?:getenv|"([A-Z_]+)"|[.]env[\s("])', content):
            refs[str(f.relative_to(root))].append(m.group(0))
    return refs


def find_lifecycle_order(root):
    """Infer initialization/lifecycle order from main.cpp and agent.cpp."""
    lifecycle = []
    for fname in ["main.cpp", "agent.cpp"]:
        f = root / "src" / fname
        if f.exists():
            content = f.read_text(errors="replace")
            # Extract method calls that look like initialization
            for m in re.finditer(r"(\w+)\.(\w+)\(", content):
                lifecycle.append((fname, m.group(1), m.group(2)))
            for m in re.finditer(r"(\w+)\s+(\w+)\s*\(", content):
                lifecycle.append((fname, "", m.group(1)))
    return lifecycle


def find_dependency_chain(root):
    """Build simple include graph."""
    deps = {}
    for f in list_files(root, r"\.h$"):
        rel = str(f.relative_to(root))
        content = f.read_text(errors="replace")
        includes = re.findall(r'#include\s+[<"]([^>"]+)[>"]', content)
        deps[rel] = includes
    return deps


def find_service_boundaries(root):
    """Find which files define service classes."""
    services = {}
    svc_dir = root / "include" / "services"
    if svc_dir.exists():
        for f in sorted(svc_dir.glob("*.h")):
            content = f.read_text(errors="replace")
            for m in re.finditer(r"class\s+(\w+)", content):
                services[m.group(1)] = str(f.relative_to(root))
    return services


def find_enum_types(root):
    """Find all enum types and their values."""
    enums = {}
    for f in list_files(root):
        content = f.read_text(errors="replace")
        for m in re.finditer(r"enum\s+(?:class\s+)?(\w+)\s*:\s*\w+\s*\{([^}]+)\}", content):
            name = m.group(1)
            values = [v.strip() for v in m.group(2).split(",") if v.strip()]
            enums[name] = {"values": values, "file": str(f.relative_to(root))}
        for m in re.finditer(r"enum\s+(?:class\s+)?(\w+)\s*\{([^}]+)\}", content):
            name = m.group(1)
            values = [v.strip() for v in m.group(2).split(",") if v.strip()]
            if name not in enums:
                enums[name] = {"values": values, "file": str(f.relative_to(root))}
    return enums


# ---------------------------------------------------------------------------
# 2. Question generation
# ---------------------------------------------------------------------------

def generate_programmatic_questions(root):
    """Generate ~80 architecture questions by analyzing the codebase."""
    questions = []
    symbols = extract_symbols(root)
    services = find_service_boundaries(root)
    enums = find_enum_types(root)
    deps = find_dependency_chain(root)

    # --- Symbol-based questions (15 triples) ---
    seen_classes = set()
    for cls, fname in symbols["classes"][:20]:
        if cls in seen_classes or len(cls) < 3:
            continue
        seen_classes.add(cls)
        questions.append(f"Locate the definition of {cls} in the source files")
        questions.append(f"Find all references to {cls} across the codebase")
        questions.append(f"Determine how {cls} is constructed and initialized")

    # --- Service questions (2 per service, 15 services = 30, pick ~20) ---
    svc_names = list(services.keys())[:15]
    for svc_name in svc_names:
        questions.append(f"Search for all callers of {svc_name} in the repository")
        questions.append(f"Find the header file where {svc_name} is declared")
        # Mix up: some get "find references", some get "locate definition"
    # Remove duplicates (if class == service name)
    seen_svc_dup = set()
    svc_unique = []
    for q in questions[-len(svc_names)*2:]:
        n = q.lower().strip()
        if n not in seen_svc_dup:
            seen_svc_dup.add(n)
            svc_unique.append(q)

    # --- Enum/constant questions (8) ---
    for ename, einfo in list(enums.items())[:8]:
        questions.append(f"Find where the enum {ename} is defined and list its values")
        questions.append(f"Search for all usages of {ename} in the codebase")

    # --- File ownership questions (8) ---
    ownership = find_component_ownership(root)
    for fname, owner in list(ownership.items())[:8]:
        questions.append(f"Determine which namespace owns the file {fname}")
        if owner not in ("unknown",):
            questions.append(f"Find all files in the {owner} component that depend on {fname}")

    # --- Include/dependency questions (8) ---
    for header, included in list(deps.items())[:8]:
        for inc in included[:2]:
            # Truncate long include paths
            short_inc = inc.split("/")[-1].replace(".h", "")
            short_head = header.split("/")[-1].replace(".h", "")
            questions.append(f"Trace why {short_head} includes {short_inc}")

    # --- Lifecycle questions (6) ---
    lifecycle = find_lifecycle_order(root)
    for src, obj, method in lifecycle[:6]:
        if obj:
            questions.append(f"Find the implementation of {obj}.{method}() in the source tree")
        elif method:
            questions.append(f"Locate where {method}() is defined and how it is called during startup")

    # --- Configuration questions (6) ---
    config_refs = find_config_references(root)
    for fname, refs in list(config_refs.items())[:6]:
        for ref in refs[:1]:
            clean = ref.strip("\"'")
            questions.append(f"Search for references to {clean} configuration in the codebase")

    # --- Cross-boundary questions (fill to ~80) ---
    xbound = [
        "Find where ExecutionEngine calls the AI service in its execution loop",
        "Locate the gating logic where CommandRouter decides between AI and deterministic output",
        "Find how the Session loop logs interactions to the replay service",
        "Search for where discovery results are passed to the planning service",
        "Trace the data flow from the terminal prompt to ExecutionEngine::execute()",
        "Find where the confidence service is called to gate the final outcome",
        "Locate how the inspect command retrieves the last investigation session",
        "Find how the recovery loop prevents duplicate tool calls in select_recovery_tool()",
        "Search for where RecoveryMetrics are populated after engine execution",
        "Find how EvidenceCollector is used in the task pipeline",
    ]
    questions.extend(xbound)

    # Deduplicate and trim to ~80
    seen = set()
    unique = []
    for q in questions:
        norm = q.lower().strip()
        if norm not in seen:
            seen.add(norm)
            unique.append(q)
    return unique[:80]


# ---------------------------------------------------------------------------
# 3. Manual "senior engineer" questions (20)
# ---------------------------------------------------------------------------

MANUAL_QUESTIONS = [
    # Architecture reasoning
    "Find the initialization order in Agent::run() to see why logging is set up before the main interaction loop",
    "Find how the diagnostics module is separated from the execution engine in the source code",
    "Find the component responsible for lifecycle shutdown in the codebase",
    "Trace how a user command reaches the execution engine from the terminal prompt",
    "Find what happens when the execution engine exhausts all its tool calls but still lacks evidence",

    # Design decisions
    "Find how the ReplayService is separated from the Agent class in the codebase",
    "Find where the confidence gating prevents the AI from answering without sufficient evidence",
    "Find the InvestigationSession struct and trace how it bridges execution to the user",
    "Find where the planner supports both deterministic and LLM-based classification paths",
    "Find how the recovery loop in select_recovery_tool() prevents infinite loops",

    # Cross-cutting concerns
    "Find how the CommandRouter determines whether a query goes to the engine vs a direct handler",
    "Find the relationship between discovery, planning, and the task pipeline in the source code",
    "Trace how RecoveryMetrics, TrustMetrics, and RetrievalMetrics get populated after execution",
    "Find how the inspect command (/inspect) retrieves and displays the last investigation session",
    "Find the role of EvidenceCollector and when it is triggered in the codebase",

    # Recovery/investigation
    "Find the recovery strategies in select_recovery_tool() and the order they are evaluated",
    "Find how the confidence service computes combined confidence across multiple tools",
    "Find what happens when both find and grep return no results for a codebase query",
    "Find how the ArchitectureReview goal type differs from CodebaseQuery in tool selection",
    "Find how tool deduplication works in the execution engine and when it triggers recovery",
]


# ---------------------------------------------------------------------------
# 4. Runner
# ---------------------------------------------------------------------------

def run_query(binary, query, timeout=60):
    """Run a single query through cursor-agent --json and return parsed result."""
    try:
        start = time.time()
        result = subprocess.run(
            [str(binary), "--json", query],
            capture_output=True, text=True, timeout=timeout,
            cwd=str(ROOT)
        )
        elapsed = time.time() - start
        stdout = result.stdout.strip()
        stderr = result.stderr.strip()

        # Extract JSON from output (agent may print status lines before JSON)
        json_match = re.search(r"({.*})", stdout, re.DOTALL)
        if json_match:
            parsed = json.loads(json_match.group(1))
        else:
            parsed = {"error": "no JSON found", "stdout": stdout[:500], "stderr": stderr[:500]}

        parsed["_elapsed"] = round(elapsed, 3)
        parsed["_query"] = query
        return parsed

    except subprocess.TimeoutExpired:
        return {"_query": query, "error": "timeout", "_elapsed": timeout}
    except Exception as e:
        return {"_query": query, "error": str(e), "_elapsed": 0}


def compute_metrics(results, source_label):
    """Compute capacity metrics from a list of results."""
    total = len(results)
    if total == 0:
        return {"source": source_label, "total": 0, "error": "no results"}

    outcomes = Counter(r.get("outcome", "error") for r in results)
    tools_used = Counter()
    files_read_total = 0
    recovery_count = 0
    grep_fallback_count = 0
    confidence_values = []
    latencies = []
    recovery_per_query = []

    for r in results:
        raw_tools = r.get("tools", r.get("tools_used"))
        tools = raw_tools if isinstance(raw_tools, list) else []
        for t in tools:
            tools_used[t] += 1

        ev = r.get("evidence", [])
        for e in ev:
            if "recovery:" in e:
                recovery_count += 1

        recovery_attempts = sum(1 for e in ev if "recovery:" in e)
        recovery_per_query.append(recovery_attempts)

        if "grep" in tools:
            grep_fallback_count += 1

        files = r.get("files_examined")
        if isinstance(files, list):
            files_read_total += len(files)

        conf = r.get("confidence", 0)
        if isinstance(conf, (int, float)):
            confidence_values.append(conf)

        lat = r.get("_elapsed", 0)
        if isinstance(lat, (int, float)):
            latencies.append(lat)

    first_pass = outcomes.get("success", 0)
    recovery_success = sum(1 for r in results if
                           r.get("outcome") == "success" and
                           any("recovery:" in e for e in r.get("evidence", [])))

    avg_recovery = sum(recovery_per_query) / total if total else 0
    avg_files = files_read_total / total if total else 0
    avg_latency = sum(latencies) / len(latencies) if latencies else 0
    avg_confidence = sum(confidence_values) / len(confidence_values) if confidence_values else 0
    grep_rate = grep_fallback_count / total if total else 0

    return {
        "source": source_label,
        "total": total,
        "first_pass_success": first_pass,
        "first_pass_rate": round(first_pass / total * 100, 1) if total else 0,
        "recovery_success": recovery_success,
        "recovery_rate": round(recovery_success / max(recovery_count, 1) * 100, 1) if recovery_count else 100.0,
        "avg_recoveries_per_query": round(avg_recovery, 2),
        "avg_files_read": round(avg_files, 2),
        "grep_fallback_count": grep_fallback_count,
        "grep_fallback_rate": round(grep_rate * 100, 1),
        "avg_latency_seconds": round(avg_latency, 2),
        "avg_confidence": round(avg_confidence, 3),
        "outcome_distribution": dict(outcomes),
        "tool_distribution": dict(tools_used.most_common()),
        "failed_queries": [
            {"query": r.get("_query", "?"), "outcome": r.get("outcome", "error"),
             "confidence": r.get("confidence", 0)}
            for r in results if r.get("outcome", "error") not in ("success", "insufficient_evidence")
        ][:10],
    }


def print_report(metrics):
    """Print formatted capacity review table."""
    sep = "=" * 60
    print(f"\n{sep}")
    print(f"  Capacity Review: {metrics['source']}")
    print(sep)
    print(f"  Total queries:        {metrics['total']}")
    print(f"  First-pass success:   {metrics['first_pass_success']} / {metrics['total']}  ({metrics['first_pass_rate']}%)")
    print(f"  Recovery success:     {metrics['recovery_rate']}%")
    print(f"  Avg recoveries/query: {metrics['avg_recoveries_per_query']}")
    print(f"  Avg files read:       {metrics['avg_files_read']}")
    print(f"  Grep fallback rate:   {metrics['grep_fallback_rate']}%")
    print(f"  Avg latency:          {metrics['avg_latency_seconds']}s")
    print(f"  Avg confidence:       {metrics['avg_confidence']}")
    print()
    print("  Outcome distribution:")
    for oc, cnt in sorted(metrics["outcome_distribution"].items()):
        bar = "#" * cnt if cnt < 60 else "#" * 60
        print(f"    {oc:30s}: {cnt:3d} {bar}")
    print()
    print("  Tool distribution:")
    for tool, cnt in sorted(metrics["tool_distribution"].items(), key=lambda x: -x[1]):
        bar = "#" * cnt if cnt < 60 else "#" * 60
        print(f"    {tool:15s}: {cnt:3d} {bar}")
    if metrics.get("failed_queries"):
        print("\n  Failures (top 10):")
        for fq in metrics["failed_queries"]:
            print(f"    [{fq['outcome']}] {fq['query'][:80]}")
    print(sep)


def write_json_report(all_metrics, path):
    """Write full metrics as JSON."""
    with open(path, "w") as f:
        json.dump(all_metrics, f, indent=2)
    print(f"\nFull report written to {path}")


# ---------------------------------------------------------------------------
# 5. Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description="Level 2 Exit Capacity Review")
    parser.add_argument("--binary", default=str(BINARY), help="Path to cursor-agent binary")
    parser.add_argument("--max", type=int, default=100, help="Max queries to run")
    parser.add_argument("--output", default="capacity_review_results.json", help="Output JSON path")
    parser.add_argument("--skip-programmatic", action="store_true", help="Skip programmatic questions")
    parser.add_argument("--skip-manual", action="store_true", help="Skip manual questions")
    parser.add_argument("--dry-run", action="store_true", help="Generate questions without running")
    args = parser.parse_args()

    binary = Path(args.binary)
    if not binary.exists() and not args.dry_run:
        print(f"Binary not found: {binary}")
        print("Build first: cd build && cmake --build . --target cursor-agent")
        sys.exit(1)

    print("Generating questions...")

    prog_questions = []
    if not args.skip_programmatic:
        prog_questions = generate_programmatic_questions(ROOT)
        print(f"  Generated {len(prog_questions)} programmatic questions")

    manual = []
    if not args.skip_manual:
        manual = MANUAL_QUESTIONS
        print(f"  Loaded {len(manual)} manual questions")

    all_questions = (prog_questions + manual)[:args.max]
    print(f"  Total: {len(all_questions)} questions")

    if args.dry_run:
        print("\n--- Sample questions ---")
        for q in all_questions[:10]:
            print(f"  - {q}")
        print("  ...")
        # Write questions to file
        outpath = "capacity_review_questions.json"
        with open(outpath, "w") as f:
            json.dump({
                "programmatic": prog_questions,
                "manual": manual,
                "total": len(all_questions),
            }, f, indent=2)
        print(f"\nQuestions written to {outpath}")
        return

    print(f"\nRunning {len(all_questions)} queries through {binary}...")

    results = {"programmatic": [], "manual": [], "all": []}
    prog_count = len(prog_questions) if not args.skip_programmatic else 0

    for i, query in enumerate(all_questions):
        source = "programmatic" if i < prog_count else "manual"
        print(f"  [{i+1}/{len(all_questions)}] {query[:70]}...", end=" ", flush=True)
        parsed = run_query(binary, query)
        outcome = parsed.get("outcome", "error")
        conf = parsed.get("confidence", 0)
        print(f"{outcome} (conf={conf}, {parsed.get('_elapsed', 0)}s)")
        results[source].append(parsed)
        results["all"].append(parsed)

    # Compute metrics
    all_metrics = {}
    for source in ["programmatic", "manual", "all"]:
        if results[source]:
            all_metrics[source] = compute_metrics(results[source], source)
            print_report(all_metrics[source])

    # Compare against targets
    targets = {
        "first_pass_rate": 90.0,
        "recovery_rate": 95.0,
        "avg_recoveries_per_query": ("<", 1.0),
        "avg_files_read": ("<=", 4.0),
        "grep_fallback_rate": 15.0,
        "avg_confidence": (">=", 0.7),
    }

    am = all_metrics.get("all", {})
    print("\n" + "=" * 60)
    print("  Target Comparison")
    print("=" * 60)
    all_pass = True
    for metric, target in targets.items():
        actual = am.get(metric, 0)
        if isinstance(target, tuple):
            op, val = target
            if op == "<":
                ok = actual < val
            elif op == "<=":
                ok = actual <= val
            else:
                ok = actual >= val
            label = f"{op} {val}"
        else:
            ok = actual >= target
            label = f">= {target}"
        status = "PASS" if ok else "FAIL"
        if not ok:
            all_pass = False
        print(f"  {metric:30s}: {actual:8.2f}  (target {label:10s})  [{status}]")

    print()
    print(f"  Overall: {'PASS' if all_pass else 'FAIL'}")
    print("=" * 60)

    write_json_report(all_metrics, args.output)


if __name__ == "__main__":
    main()
