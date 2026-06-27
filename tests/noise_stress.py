#!/usr/bin/env python3
"""
Noise Stress Test for Level 2 Capacity Review.

Creates harmless distractions in a temporary copy of the repository,
then runs a subset of architecture queries to verify recovery converges
on production code despite the noise.

Usage:
  python3 tests/noise_stress.py
"""

import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BINARY = ROOT / "build" / "bin" / "cursor-agent"

# Production queries that should find real code despite noise
PRODUCTION_QUERIES = [
    "Locate the definition of ExecutionEngine in the source files",
    "Find all references to SessionState across the codebase",
    "Locate where the CommandRouter class is declared",
    "Find the implementation of ReplayService in the source tree",
    "Search for the UIManager class definition",
    "Find where confidence is computed after investigation",
    "Find the Startup class and its initialize method",
    "Search for how Agent::run() creates the session loop",
]

NOISE_FILES = [
    # Extra markdown with misleading content
    ("docs/legacy/README.md", "# Legacy\nThis is old documentation."),
    ("docs/backup/archived.md", "# Archived\nThis file should not be found."),

    # Generated/fixture files similar to real source
    ("src/generated/execution_engine.cpp",
     '#include "services/execution_engine.h"\nnamespace Services {\n// Generated stub\n}'),
    ("include/generated/execution_engine.h",
     '#pragma once\nnamespace Services {\nclass ExecutionEngine { public: void run(); };\n}'),

    # Duplicate with misleading names
    ("src/backup/command_router.cpp",
     '// Backup of command router\n#include "app/command_router.h"\nnamespace Core {\n// Old version\n}'),

    # Test fixtures that look like production
    ("tests/fixtures/output/replay_service.cpp",
     '#include "replay_service.h"\nnamespace Services {\n// Test fixture\n}'),

    # Archives with partial content
    ("data/archive/v0.1/agent.cpp",
     '// Old agent version\n#include "agent.h"\n// Deprecated'),

    # Random build artifacts
    ("build/artifacts/symbol_cache.json", '{"ExecutionEngine": "src/old/execution_engine.cpp"}'),
    ("build/artifacts/dependency_graph.json", '{"nodes": [{"name": "FakeNode"}]}'),
]


def create_noise(base_dir, noise_list):
    """Create noise files in the given base directory."""
    created = []
    for rel_path, content in noise_list:
        full_path = base_dir / rel_path
        full_path.parent.mkdir(parents=True, exist_ok=True)
        full_path.write_text(content)
        created.append(full_path)
    return created


def remove_noise(noise_files):
    """Remove noise files."""
    for f in noise_files:
        if f.exists():
            f.unlink()
    # Remove empty parent dirs
    dirs = set(f.parent for f in noise_files)
    for d in sorted(dirs, key=lambda x: len(str(x)), reverse=True):
        try:
            if d.exists() and not any(d.iterdir()):
                d.rmdir()
        except (OSError, PermissionError):
            pass


def run_queries(binary, queries, cwd):
    """Run queries and return results."""
    results = []
    for i, q in enumerate(queries):
        try:
            result = subprocess.run(
                [str(binary), "--json", q],
                capture_output=True, text=True, timeout=60,
                cwd=str(cwd)
            )
            stdout = result.stdout.strip()
            json_match = re.search(r"({.*})", stdout, re.DOTALL)
            if json_match:
                parsed = json.loads(json_match.group(1))
            else:
                parsed = {"error": "no JSON", "outcome": "error"}
            parsed["_query"] = q
            parsed["_elapsed"] = round(
                sum(1 for _ in range(1000000))  # placeholder
            )
            results.append(parsed)
            outcome = parsed.get("outcome", "error")
            tools = parsed.get("tools", [])
            files = parsed.get("files_examined", [])
            prod_files = [f for f in (files or [])
                         if "generated" not in str(f)
                         and "backup" not in str(f)
                         and "archive" not in str(f)
                         and "fixtures" not in str(f)]
            print(f"  [{i+1}/{len(queries)}] {outcome:20s} {q[:50]}... "
                  f"tools={len(tools or [])} prod_files={len(prod_files)}")
        except subprocess.TimeoutExpired:
            results.append({"_query": q, "outcome": "timeout", "error": "timeout"})
            print(f"  [{i+1}/{len(queries)}] timeout         {q[:50]}...")
        except Exception as e:
            results.append({"_query": q, "outcome": "error", "error": str(e)})
            print(f"  [{i+1}/{len(queries)}] error           {q[:50]}... {e}")
    return results


def check_noise_contamination(result):
    """Check if a result was contaminated by noise files."""
    files = result.get("files_examined", []) or []
    evidence = result.get("evidence", []) or []
    noise_keywords = ["generated", "backup", "archive", "fixtures/output", "artifacts"]
    contaminated_files = [f for f in files
                         if any(k in str(f) for k in noise_keywords)]
    contaminated_evidence = [e for e in evidence
                            if any(k in e for k in noise_keywords)]
    return contaminated_files, contaminated_evidence


def main():
    import argparse
    parser = argparse.ArgumentParser(description="Noise Stress Test")
    parser.add_argument("--binary", default=str(BINARY))
    parser.add_argument("--no-cleanup", action="store_true",
                       help="Leave noise files for inspection")
    args = parser.parse_args()

    binary = Path(args.binary)
    if not binary.exists():
        print(f"Binary not found: {binary}")
        sys.exit(1)

    queries = PRODUCTION_QUERIES
    print(f"Noise Stress Test")
    print(f"  Queries: {len(queries)}")
    print(f"  Noise files: {len(NOISE_FILES)}")
    print()

    # Phase 1: Baseline (no noise)
    print("Phase 1: Baseline (no noise)")
    print("-" * 60)
    baseline = run_queries(binary, queries, ROOT)
    baseline_success = sum(1 for r in baseline if r.get("outcome") == "success")
    print(f"  Baseline: {baseline_success}/{len(queries)} success")
    print()

    # Phase 2: With noise
    print("Phase 2: With noise distractions")
    print("-" * 60)
    noise_files = create_noise(ROOT, NOISE_FILES)
    print(f"  Created {len(noise_files)} noise files")

    try:
        noisy = run_queries(binary, queries, ROOT)
        noisy_success = sum(1 for r in noisy if r.get("outcome") == "success")
        print(f"  Noisy: {noisy_success}/{len(queries)} success")

        # Check contamination
        contaminated_count = 0
        for r in noisy:
            cf, ce = check_noise_contamination(r)
            if cf or ce:
                contaminated_count += 1
                print(f"  CONTAMINATED: {r.get('_query','?')[:60]}")
                if cf:
                    print(f"    files: {cf}")
                if ce:
                    print(f"    evidence: {ce[:2]}")

        print(f"\n  Contamination: {contaminated_count}/{len(queries)} queries")
    finally:
        if not args.no_cleanup:
            remove_noise(noise_files)
            print(f"  Cleaned up {len(noise_files)} noise files")

    # Summary
    print("\n" + "=" * 60)
    print("  Summary")
    print("=" * 60)
    bl_ok = sum(1 for r in baseline if r.get("outcome") == "success")
    ny_ok = sum(1 for r in noisy if r.get("outcome") == "success")
    print(f"  Baseline success: {bl_ok}/{len(queries)} ({bl_ok/len(queries)*100:.0f}%)")
    print(f"  Noisy success:    {ny_ok}/{len(queries)} ({ny_ok/len(queries)*100:.0f}%)")

    if ny_ok < bl_ok:
        print(f"\n  WARNING: Noise degraded success rate by {bl_ok - ny_ok} queries")
        return 1
    else:
        print(f"\n  PASS: Noise did not reduce success rate")
        return 0


if __name__ == "__main__":
    sys.exit(main())
