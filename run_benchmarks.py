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

ROTATION_TESTS = {
    "quick8": "he_corpus/rotation/rotation_000008.csv",
    "smoke": "he_corpus/rotation/rotation_000008.csv",
    "8": "he_corpus/rotation/rotation_000008.csv",
    "256": "he_corpus/rotation/rotation_000256.csv",
    "4096": "he_corpus/rotation/rotation_004096.csv",
    "8192": "he_corpus/rotation/rotation_008192.csv",
}

LIBRARIES = {
    "exact": {
        "seal": "seal_bfv_exact",
        "openfhe": "openfhe_bfv_exact",
    },
    "rotation": {
        "seal": "seal_bfv_rotation",
        "openfhe": "openfhe_bfv_rotation",
    },
    "serialization": {
        "seal": "seal_bfv_serialization",
        "openfhe": "openfhe_bfv_serialization",
    },
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


def expand_tests(value: str, run_all: bool, available_tests: dict[str, str]) -> list[str]:
    if run_all or value.lower() == "all":
        if "edge" in available_tests:
            return ["quick8", "256", "4096", "8192", "edge"]
        return ["quick8", "256", "4096", "8192"]

    tests = split_names(value)
    unknown = [name for name in tests if name not in available_tests]
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


def write_result_files(
    output_by_suite: dict[str, list[str]],
    suites: list[str],
    out_dir: str,
    kind: str,
) -> None:
    output_dir = ROOT / out_dir
    output_dir.mkdir(parents=True, exist_ok=True)
    for suite_name in suites:
        output_text = "\n".join(output_by_suite[suite_name])
        if output_text:
            output_text += "\n"
        output_name = str(SUITES[suite_name]["output"])
        if kind != "exact":
            output_name = output_name.replace(".csv", f"_{kind}.csv")
        output_path = output_dir / output_name
        output_path.write_text(output_text, encoding="utf-8")
        print(f"wrote {output_path}")


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
        "--kind",
        choices=["exact", "rotation", "serialization"],
        default="exact",
        help="benchmark group to run",
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
        available_tests = ROTATION_TESTS if args.kind == "rotation" else TESTS
        for name, corpus in available_tests.items():
            print(f"{name}: {corpus}")
        return 0

    if args.ring_size <= 0:
        print("--ring-size must be positive", file=sys.stderr)
        return 2

    try:
        available_tests = ROTATION_TESTS if args.kind == "rotation" else TESTS
        tests = expand_tests(args.tests, args.all, available_tests)
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
            corpus = ROOT / available_tests[test_name]
            if not corpus.exists():
                print(f"missing corpus for test '{test_name}': {corpus}", file=sys.stderr)
                return 2

            binary = build_dir / LIBRARIES[args.kind][str(library)]
            command = build_command(binary, corpus, args.ring_size)

            if args.dry_run:
                env_prefix = f"OMP_NUM_THREADS={threads} " if library == "openfhe" else ""
                print(env_prefix + " ".join(command))
                continue

            if not binary.exists():
                print(f"missing binary for {library}: {binary}", file=sys.stderr)
                print("build it with: cmake --build cpp/build --target " + LIBRARIES[args.kind][library], file=sys.stderr)
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
            for line in completed.stdout.splitlines():
                if line.strip():
                    output_by_suite[suite_name].append(
                        f"started_unix={started_unix:.6f},"
                        f"started_utc={started_iso},"
                        f"test={test_name},threads={threads},{line}"
                    )

            if completed.stderr:
                print(completed.stderr, file=sys.stderr, end="")
            if completed.returncode != 0:
                if completed.stdout:
                    print(completed.stdout, file=sys.stderr, end="")
                if not args.stdout:
                    write_result_files(output_by_suite, suites, args.out_dir, args.kind)
                print("command failed: " + " ".join(command), file=sys.stderr)
                return completed.returncode

    if args.dry_run:
        return 0

    if args.stdout:
        for suite_name in suites:
            output_text = "\n".join(output_by_suite[suite_name])
            if output_text:
                print(output_text)
    else:
        write_result_files(output_by_suite, suites, args.out_dir, args.kind)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
