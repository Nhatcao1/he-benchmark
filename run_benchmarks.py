#!/usr/bin/env python3
"""
Run benchmark binaries by test name instead of forcing callers to remember CSVs.

The corpus generator already owns deterministic input files. This runner keeps
that file mapping in one place so adding new tests later does not multiply setup
documentation or shell commands.
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path


ROOT = Path(__file__).resolve().parent

# Test names intentionally match the human workflow, not the on-disk filenames.
# Add new corpus-backed tests here and the CLI/docs can stay stable.
TESTS = {
    "quick8": "he_corpus/exact/exact_safe_000008.csv",
    "smoke": "he_corpus/exact/exact_safe_000008.csv",
    "8": "he_corpus/exact/exact_safe_000008.csv",
    "256": "he_corpus/exact/exact_safe_000256.csv",
    "4096": "he_corpus/exact/exact_safe_004096.csv",
    "8192": "he_corpus/exact/exact_safe_008192.csv",
    "edge": "he_corpus/exact/exact_edge_cases.csv",
}

LIBRARIES = {
    "seal": "seal_bfv_exact",
    "openfhe": "openfhe_bfv_exact",
}

# The benchmark's normal comparison is fixed on purpose:
#   1. SEAL baseline
#   2. OpenFHE baseline pinned to one OpenMP thread
#   3. OpenFHE using six OpenMP threads
# If a future comparison needs a different OpenFHE thread count, change the
# openfhe_threads6 entry here and the docs in one place.
SUITES = {
    "seal": {
        "library": "seal",
        "threads": 1,
        "output": "seal.csv",
    },
    "openfhe": {
        "library": "openfhe",
        "threads": 1,
        "output": "openfhe.csv",
    },
    "openfhe6": {
        "library": "openfhe",
        "threads": 6,
        "output": "openfhe_threads6.csv",
    },
}


def split_names(value: str) -> list[str]:
    return [item.strip().lower() for item in value.split(",") if item.strip()]


def expand_tests(value: str, run_all: bool) -> list[str]:
    if run_all or value.lower() == "all":
        return ["quick8", "256", "4096", "8192", "edge"]

    tests = split_names(value)
    unknown = [name for name in tests if name not in TESTS]
    if unknown:
        raise ValueError("unknown test(s): " + ", ".join(unknown))
    return tests


def expand_suites(value: str) -> list[str]:
    if value.lower() == "all":
        return list(SUITES)

    suites = split_names(value)
    unknown = [name for name in suites if name not in SUITES]
    if unknown:
        raise ValueError("unknown suite(s): " + ", ".join(unknown))
    return suites


def build_command(binary: Path, corpus: Path, ring_size: int) -> list[str]:
    return [
        str(binary),
        "--corpus",
        str(corpus),
        "--ring-size",
        str(ring_size),
    ]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run SEAL/OpenFHE BFV exact benchmarks from named corpus tests.",
    )
    parser.add_argument(
        "--all",
        action="store_true",
        help="run all known exact tests",
    )
    parser.add_argument(
        "--tests",
        default="quick8",
        help="comma-separated tests: quick8,edge,8,256,4096,8192,all",
    )
    parser.add_argument(
        "--only",
        default="all",
        help="debug filter: seal,openfhe,openfhe6,all",
    )
    parser.add_argument(
        "--ring-size",
        type=int,
        default=8192,
        help="BFV ring/poly modulus degree passed to benchmark binaries",
    )
    parser.add_argument(
        "--build-dir",
        default="cpp/build",
        help="directory containing benchmark binaries",
    )
    parser.add_argument(
        "--out-dir",
        default="cpp/results",
        help="directory for separate suite result files",
    )
    parser.add_argument(
        "--stdout",
        action="store_true",
        help="print all suite output instead of writing result files",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="print commands without running them",
    )
    parser.add_argument(
        "--list-tests",
        action="store_true",
        help="print known test names and corpus files",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    if args.list_tests:
        for name, corpus in TESTS.items():
            print(f"{name}: {corpus}")
        return 0

    if args.ring_size <= 0:
        print("--ring-size must be positive", file=sys.stderr)
        return 2

    try:
        tests = expand_tests(args.tests, args.all)
        suites = expand_suites(args.only)
    except ValueError as error:
        print(error, file=sys.stderr)
        return 2

    build_dir = ROOT / args.build_dir
    output_by_suite: dict[str, list[str]] = {suite_name: [] for suite_name in suites}

    for suite_name in suites:
        suite = SUITES[suite_name]
        library = suite["library"]
        threads = int(suite["threads"])

        for test_name in tests:
            corpus = ROOT / TESTS[test_name]
            if not corpus.exists():
                print(f"missing corpus for test '{test_name}': {corpus}", file=sys.stderr)
                return 2

            binary = build_dir / LIBRARIES[str(library)]
            command = build_command(binary, corpus, args.ring_size)

            if args.dry_run:
                env_prefix = f"OMP_NUM_THREADS={threads} " if library == "openfhe" else ""
                print(env_prefix + " ".join(command))
                continue

            if not binary.exists():
                print(f"missing binary for {library}: {binary}", file=sys.stderr)
                print("build it with: cmake --build cpp/build --target " + LIBRARIES[library], file=sys.stderr)
                return 2

            # OpenFHE parallelism is controlled per child process so the 1-thread
            # and 6-thread reports do not share runtime state.
            env = os.environ.copy()
            if library == "openfhe":
                env["OMP_NUM_THREADS"] = str(threads)

            # The C++ executable emits one CSV-style line per BFV operation.
            # Preserve those lines and annotate the selected corpus test and
            # thread count in the same format for all reports.
            started_unix = time.time()
            started_iso = datetime.fromtimestamp(started_unix, tz=timezone.utc).isoformat()
            completed = subprocess.run(
                command,
                cwd=ROOT,
                env=env,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )
            if completed.stderr:
                print(completed.stderr, file=sys.stderr, end="")
            if completed.returncode != 0:
                print("command failed: " + " ".join(command), file=sys.stderr)
                return completed.returncode

            for line in completed.stdout.splitlines():
                if line.strip():
                    output_by_suite[suite_name].append(
                        f"started_unix={started_unix:.6f},"
                        f"started_utc={started_iso},"
                        f"test={test_name},threads={threads},{line}"
                    )

    if args.dry_run:
        return 0

    if args.stdout:
        for suite_name in suites:
            output_text = "\n".join(output_by_suite[suite_name])
            if output_text:
                print(output_text)
    else:
        output_dir = ROOT / args.out_dir
        output_dir.mkdir(parents=True, exist_ok=True)
        for suite_name in suites:
            output_text = "\n".join(output_by_suite[suite_name])
            if output_text:
                output_text += "\n"
            output_path = output_dir / str(SUITES[suite_name]["output"])
            output_path.write_text(output_text, encoding="utf-8")
            print(f"wrote {output_path}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
