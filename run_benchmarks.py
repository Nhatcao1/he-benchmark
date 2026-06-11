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

CKKS_TESTS = {
    "quick8": "he_corpus/ckks_normal/ckks_normal_000008.csv",
    "smoke": "he_corpus/ckks_normal/ckks_normal_000008.csv",
    "normal8": "he_corpus/ckks_normal/ckks_normal_000008.csv",
    "normal256": "he_corpus/ckks_normal/ckks_normal_000256.csv",
    "normal4096": "he_corpus/ckks_normal/ckks_normal_004096.csv",
    "normal8192": "he_corpus/ckks_normal/ckks_normal_008192.csv",
    "small8": "he_corpus/ckks_small/ckks_small_000008.csv",
    "small256": "he_corpus/ckks_small/ckks_small_000256.csv",
    "small4096": "he_corpus/ckks_small/ckks_small_004096.csv",
    "small8192": "he_corpus/ckks_small/ckks_small_008192.csv",
    "nearzero8": "he_corpus/ckks_near_zero/ckks_near_zero_000008.csv",
    "nearzero256": "he_corpus/ckks_near_zero/ckks_near_zero_000256.csv",
    "nearzero4096": "he_corpus/ckks_near_zero/ckks_near_zero_004096.csv",
    "nearzero8192": "he_corpus/ckks_near_zero/ckks_near_zero_008192.csv",
    "mixed8": "he_corpus/ckks_mixed_scale/ckks_mixed_scale_000008.csv",
    "mixed256": "he_corpus/ckks_mixed_scale/ckks_mixed_scale_000256.csv",
    "mixed4096": "he_corpus/ckks_mixed_scale/ckks_mixed_scale_004096.csv",
    "mixed8192": "he_corpus/ckks_mixed_scale/ckks_mixed_scale_008192.csv",
}

DEPTH_TESTS = {
    "quick8": "he_corpus/depth/exact_depth_000008.csv",
    "smoke": "he_corpus/depth/exact_depth_000008.csv",
    "8": "he_corpus/depth/exact_depth_000008.csv",
    "256": "he_corpus/depth/exact_depth_000256.csv",
    "4096": "he_corpus/depth/exact_depth_004096.csv",
    "8192": "he_corpus/depth/exact_depth_008192.csv",
}

CKKS_DEPTH_TESTS = {
    "quick8": "he_corpus/depth/ckks_depth_000008.csv",
    "smoke": "he_corpus/depth/ckks_depth_000008.csv",
    "8": "he_corpus/depth/ckks_depth_000008.csv",
    "256": "he_corpus/depth/ckks_depth_000256.csv",
    "4096": "he_corpus/depth/ckks_depth_004096.csv",
    "8192": "he_corpus/depth/ckks_depth_008192.csv",
}

WORKLOAD_TESTS = {
    "quick8": "he_corpus/exact/exact_safe_000008.csv",
    "smoke": "he_corpus/exact/exact_safe_000008.csv",
    "8": "he_corpus/exact/exact_safe_000008.csv",
    "256": "he_corpus/exact/exact_safe_000256.csv",
    "4096": "he_corpus/exact/exact_safe_004096.csv",
    "8192": "he_corpus/exact/exact_safe_008192.csv",
}

CKKS_WORKLOAD_TESTS = {
    "quick8": "he_corpus/ckks_normal/ckks_normal_000008.csv",
    "smoke": "he_corpus/ckks_normal/ckks_normal_000008.csv",
    "normal8": "he_corpus/ckks_normal/ckks_normal_000008.csv",
    "normal256": "he_corpus/ckks_normal/ckks_normal_000256.csv",
    "normal4096": "he_corpus/ckks_normal/ckks_normal_004096.csv",
    "normal8192": "he_corpus/ckks_normal/ckks_normal_008192.csv",
    "small8": "he_corpus/ckks_small/ckks_small_000008.csv",
    "small256": "he_corpus/ckks_small/ckks_small_000256.csv",
    "small4096": "he_corpus/ckks_small/ckks_small_004096.csv",
    "small8192": "he_corpus/ckks_small/ckks_small_008192.csv",
    "nearzero8": "he_corpus/ckks_near_zero/ckks_near_zero_000008.csv",
    "nearzero256": "he_corpus/ckks_near_zero/ckks_near_zero_000256.csv",
    "nearzero4096": "he_corpus/ckks_near_zero/ckks_near_zero_004096.csv",
    "nearzero8192": "he_corpus/ckks_near_zero/ckks_near_zero_008192.csv",
    "mixed8": "he_corpus/ckks_mixed_scale/ckks_mixed_scale_000008.csv",
    "mixed256": "he_corpus/ckks_mixed_scale/ckks_mixed_scale_000256.csv",
    "mixed4096": "he_corpus/ckks_mixed_scale/ckks_mixed_scale_004096.csv",
    "mixed8192": "he_corpus/ckks_mixed_scale/ckks_mixed_scale_008192.csv",
}

THROUGHPUT_EXACT_TESTS = {
    "quick8": "he_corpus/exact/exact_safe_000008.csv",
    "smoke": "he_corpus/exact/exact_safe_000008.csv",
    "256": "he_corpus/exact/exact_safe_000256.csv",
    "medium": "he_corpus/exact/exact_safe_004096.csv",
    "4096": "he_corpus/exact/exact_safe_004096.csv",
    "full8192": "he_corpus/exact/exact_safe_008192.csv",
    "full16384": "he_corpus/exact/exact_safe_016384.csv",
}

THROUGHPUT_CKKS_TESTS = {
    "quick8": "he_corpus/ckks_normal/ckks_normal_000008.csv",
    "smoke": "he_corpus/ckks_normal/ckks_normal_000008.csv",
    "256": "he_corpus/ckks_normal/ckks_normal_000256.csv",
    "normal256": "he_corpus/ckks_normal/ckks_normal_000256.csv",
    "medium": "he_corpus/ckks_normal/ckks_normal_004096.csv",
    "4096": "he_corpus/ckks_normal/ckks_normal_004096.csv",
    "normal4096": "he_corpus/ckks_normal/ckks_normal_004096.csv",
    "full8192": "he_corpus/ckks_normal/ckks_normal_004096.csv",
    "full16384": "he_corpus/ckks_normal/ckks_normal_008192.csv",
}

MATRIX_TESTS = {
    "matrix64": "he_corpus/matrices/matrix_a_0064x0064.csv",
    "64x64": "he_corpus/matrices/matrix_a_0064x0064.csv",
}

LIBRARIES = {
    "exact": {
        "bfv": {
            "seal": "seal_bfv_exact",
            "openfhe": "openfhe_bfv_exact",
        },
        "bgv": {
            "seal": "seal_bgv_exact",
            "openfhe": "openfhe_bgv_exact",
        },
        "ckks": {
            "seal": "seal_ckks",
            "openfhe": "openfhe_ckks",
        },
    },
    "rotation": {
        "bfv": {
            "seal": "seal_bfv_rotation",
            "openfhe": "openfhe_bfv_rotation",
        },
        "bgv": {
            "seal": "seal_bgv_rotation",
            "openfhe": "openfhe_bgv_rotation",
        },
        "ckks": {
            "seal": "seal_ckks_rotation",
            "openfhe": "openfhe_ckks_rotation",
        },
    },
    "serialization": {
        "bfv": {
            "seal": "seal_bfv_serialization",
            "openfhe": "openfhe_bfv_serialization",
        },
        "bgv": {
            "seal": "seal_bgv_serialization",
            "openfhe": "openfhe_bgv_serialization",
        },
        "ckks": {
            "seal": "seal_ckks_serialization",
            "openfhe": "openfhe_ckks_serialization",
        },
    },
    "depth": {
        "bfv": {
            "seal": "seal_bfv_depth",
            "openfhe": "openfhe_bfv_depth",
        },
        "bgv": {
            "seal": "seal_bgv_depth",
            "openfhe": "openfhe_bgv_depth",
        },
        "ckks": {
            "seal": "seal_ckks_depth",
            "openfhe": "openfhe_ckks_depth",
        },
    },
    "workload": {
        "bfv": {
            "seal": "seal_bfv_dot",
            "openfhe": "openfhe_bfv_dot",
        },
        "bgv": {
            "seal": "seal_bgv_dot",
            "openfhe": "openfhe_bgv_dot",
        },
        "ckks": {
            "seal": "seal_ckks_dot",
            "openfhe": "openfhe_ckks_dot",
        },
    },
    "e2e": {
        "bfv": {
            "seal": "seal_bfv_e2e",
            "openfhe": "openfhe_bfv_e2e",
        },
        "bgv": {
            "seal": "seal_bgv_e2e",
            "openfhe": "openfhe_bgv_e2e",
        },
        "ckks": {
            "seal": "seal_ckks_e2e",
            "openfhe": "openfhe_ckks_e2e",
        },
    },
    "memory": {
        "bfv": {
            "seal": "seal_bfv_memory",
            "openfhe": "openfhe_bfv_memory",
        },
        "bgv": {
            "seal": "seal_bgv_memory",
            "openfhe": "openfhe_bgv_memory",
        },
        "ckks": {
            "seal": "seal_ckks_memory",
            "openfhe": "openfhe_ckks_memory",
        },
    },
    "ntt": {
        "lowlevel": {
            "seal": "seal_lowlevel_ntt",
            "openfhe": "openfhe_lowlevel_ntt",
        },
    },
    "poly": {
        "lowlevel": {
            "seal": "seal_lowlevel_poly",
            "openfhe": "openfhe_lowlevel_poly",
        },
    },
    "keyswitch": {
        "bfv": {
            "seal": "seal_bfv_keyswitch",
            "openfhe": "openfhe_bfv_keyswitch",
        },
        "bgv": {
            "seal": "seal_bgv_keyswitch",
            "openfhe": "openfhe_bgv_keyswitch",
        },
        "ckks": {
            "seal": "seal_ckks_keyswitch",
            "openfhe": "openfhe_ckks_keyswitch",
        },
    },
    "matrix": {
        "bfv": {
            "seal": "seal_bfv_matrix",
            "openfhe": "openfhe_bfv_matrix",
        },
        "bgv": {
            "seal": "seal_bgv_matrix",
            "openfhe": "openfhe_bgv_matrix",
        },
        "ckks": {
            "seal": "seal_ckks_matrix",
            "openfhe": "openfhe_ckks_matrix",
        },
    },
    "throughput": {
        "bfv": {
            "seal": "seal_bfv_throughput",
            "openfhe": "openfhe_bfv_throughput",
        },
        "bgv": {
            "seal": "seal_bgv_throughput",
            "openfhe": "openfhe_bgv_throughput",
        },
        "ckks": {
            "seal": "seal_ckks_throughput",
            "openfhe": "openfhe_ckks_throughput",
        },
    },
    "heap": {
        "bfv": {
            "seal": "seal_bfv_heap",
            "openfhe": "openfhe_bfv_heap",
        },
        "bgv": {
            "seal": "seal_bgv_heap",
            "openfhe": "openfhe_bgv_heap",
        },
    },
    "footprint": {
        "bfv": {
            "seal": "seal_bfv_footprint",
            "openfhe": "openfhe_bfv_footprint",
        },
        "bgv": {
            "seal": "seal_bgv_footprint",
            "openfhe": "openfhe_bgv_footprint",
        },
    },
    "corpus-memory": {
        "bfv": {
            "seal": "seal_bfv_corpus_memory",
            "openfhe": "openfhe_bfv_corpus_memory",
        },
        "bgv": {
            "seal": "seal_bgv_corpus_memory",
            "openfhe": "openfhe_bgv_corpus_memory",
        },
    },
    "thread-memory": {
        "bfv": {
            "seal": "seal_bfv_thread_memory",
            "openfhe": "openfhe_bfv_thread_memory",
        },
        "bgv": {
            "seal": "seal_bgv_thread_memory",
            "openfhe": "openfhe_bgv_thread_memory",
        },
    },
    "cpu": {
        "bfv": {
            "seal": "seal_bfv_cpu",
            "openfhe": "openfhe_bfv_cpu",
        },
        "bgv": {
            "seal": "seal_bgv_cpu",
            "openfhe": "openfhe_bgv_cpu",
        },
    },
}

# The benchmark's normal comparison is fixed on purpose:
#   1. SEAL baseline
#   2. OpenFHE baseline pinned to one OpenMP thread
#   3. OpenFHE using four and six OpenMP threads
# Keep this separate from thread scaling so routine SEAL-vs-OpenFHE reports do
# not expand into many OpenFHE-only files.
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
    "openfhe4": {
        "library": "openfhe",
        "threads": 4,
        "output": "openfhe_threads4.csv",
    },
    "openfhe6": {
        "library": "openfhe",
        "threads": 6,
        "output": "openfhe_threads6.csv",
    },
}

THREAD_SCALING_SUITES = {
    "openfhe1": {
        "library": "openfhe",
        "threads": 1,
        "output": "openfhe_threads1_scaling.csv",
    },
    "openfhe2": {
        "library": "openfhe",
        "threads": 2,
        "output": "openfhe_threads2_scaling.csv",
    },
    "openfhe4": {
        "library": "openfhe",
        "threads": 4,
        "output": "openfhe_threads4_scaling.csv",
    },
    "openfhe6": {
        "library": "openfhe",
        "threads": 6,
        "output": "openfhe_threads6_scaling.csv",
    },
    "openfhe8": {
        "library": "openfhe",
        "threads": 8,
        "output": "openfhe_threads8_scaling.csv",
    },
}


def split_names(value: str) -> list[str]:
    return [item.strip().lower() for item in value.split(",") if item.strip()]


def expand_tests(
    value: str,
    run_all: bool,
    available_tests: dict[str, str],
    ckks_slot_limited: bool = False,
) -> list[str]:
    if run_all or value.lower() == "all":
        if "matrix64" in available_tests:
            return ["matrix64"]
        if "full8192" in available_tests:
            return ["full8192", "full16384"]
        if "normal8" in available_tests:
            return [
                "quick8",
                "normal256",
                "normal4096",
                "small8",
                "small256",
                "small4096",
                "nearzero8",
                "nearzero256",
                "nearzero4096",
                "mixed8",
                "mixed256",
                "mixed4096",
            ]
        if ckks_slot_limited:
            return ["quick8", "256", "4096"]
        if "4096" in available_tests and "8192" not in available_tests:
            return ["quick8", "256", "4096"]
        if "edge" in available_tests:
            return ["quick8", "256", "4096", "8192", "edge"]
        return ["quick8", "256", "4096", "8192"]

    tests = split_names(value)
    unknown = [name for name in tests if name not in available_tests]
    if unknown:
        raise ValueError("unknown test(s): " + ", ".join(unknown))
    return tests


def expand_suites(value: str, suite_map: dict[str, dict[str, object]]) -> list[str]:
    if value.lower() == "all":
        return list(suite_map)

    suites = split_names(value)
    unknown = [name for name in suites if name not in suite_map]
    if unknown:
        raise ValueError("unknown suite(s): " + ", ".join(unknown))
    return suites


def filter_suites_for_kind(suites: list[str], kind: str) -> list[str]:
    if kind == "serialization":
        return [suite for suite in suites if suite not in {"openfhe4", "openfhe6"}]
    return suites


def suites_for_run(thread_scaling: bool) -> dict[str, dict[str, object]]:
    return THREAD_SCALING_SUITES if thread_scaling else SUITES


def available_schemes_for_kind(kind: str) -> list[str]:
    return list(LIBRARIES[kind])


def available_tests_for(kind: str, scheme: str) -> dict[str, str]:
    if kind == "matrix":
        return MATRIX_TESTS
    if kind == "throughput" and scheme == "ckks":
        return THROUGHPUT_CKKS_TESTS
    if kind == "throughput":
        return THROUGHPUT_EXACT_TESTS
    if kind in {"ntt", "poly"}:
        return TESTS
    if kind in {"workload", "e2e"} and scheme == "ckks":
        return CKKS_WORKLOAD_TESTS
    if kind in {"workload", "e2e"}:
        return WORKLOAD_TESTS
    if kind == "depth" and scheme == "ckks":
        return CKKS_DEPTH_TESTS
    if kind == "depth":
        return DEPTH_TESTS
    if kind == "rotation" and scheme == "ckks":
        return CKKS_WORKLOAD_TESTS
    if scheme == "ckks":
        return CKKS_TESTS
    if kind == "rotation":
        return ROTATION_TESTS
    return TESTS


def parse_positive_int_list(value: str, option_name: str) -> list[int]:
    parsed: list[int] = []
    for raw_item in value.split(","):
        item = raw_item.strip()
        if not item:
            continue
        try:
            number = int(item)
        except ValueError as error:
            raise ValueError(f"invalid value for {option_name}: {item}") from error
        if number <= 0:
            raise ValueError(f"{option_name} values must be positive: {item}")
        parsed.append(number)
    if not parsed:
        raise ValueError(f"{option_name} must contain at least one value")
    return parsed


def build_command(
    binary: Path,
    corpus: Path,
    ring_size: int,
    max_depth: int,
    scheme: str,
    ckks_config: str,
    ckks_depth: int | None,
    ckks_scale_bits: int | None,
    ckks_first_mod_bits: int | None,
    duration_ms: int,
) -> list[str]:
    command = [
        str(binary),
        "--corpus",
        str(corpus),
        "--ring-size",
        str(ring_size),
        "--max-depth",
        str(max_depth),
        "--duration-ms",
        str(duration_ms),
    ]
    if scheme == "ckks":
        command.extend(["--ckks-config", ckks_config])
        if ckks_depth is not None:
            command.extend(["--ckks-depth", str(ckks_depth)])
        if ckks_scale_bits is not None:
            command.extend(["--ckks-scale-bits", str(ckks_scale_bits)])
        if ckks_first_mod_bits is not None:
            command.extend(["--ckks-first-mod-bits", str(ckks_first_mod_bits)])
    return command


def build_env(library: str, threads: int) -> dict[str, str]:
    env = os.environ.copy()
    if library == "openfhe":
        env["OMP_NUM_THREADS"] = str(threads)
        env["OMP_DYNAMIC"] = "FALSE"
        env["OMP_PROC_BIND"] = "close"
        env["OMP_PLACES"] = "cores"
    return env


def expected_throughput_ring(test_name: str) -> int | None:
    if test_name == "full8192":
        return 8192
    if test_name == "full16384":
        return 16384
    return None


def workload_active_slots(test_name: str) -> int | None:
    suffixes = (
        ("8192", 8192),
        ("4096", 4096),
        ("256", 256),
        ("8", 8),
    )
    if test_name in {"quick8", "smoke"}:
        return 8
    for suffix, slots in suffixes:
        if test_name.endswith(suffix):
            return slots
    return None


def env_prefix(library: str, threads: int) -> str:
    if library != "openfhe":
        return ""
    return (
        f"OMP_NUM_THREADS={threads} "
        "OMP_DYNAMIC=FALSE "
        "OMP_PROC_BIND=close "
        "OMP_PLACES=cores "
    )


def write_result_files(
    output_by_suite: dict[str, list[str]],
    suites: list[str],
    suite_map: dict[str, dict[str, object]],
    out_dir: str,
    kind: str,
    scheme: str,
) -> None:
    output_dir = ROOT / out_dir
    output_dir.mkdir(parents=True, exist_ok=True)
    for suite_name in suites:
        output_text = "\n".join(output_by_suite[suite_name])
        if output_text:
            output_text += "\n"
        output_name = str(suite_map[suite_name]["output"])
        if scheme != "bfv":
            output_name = output_name.replace(".csv", f"_{scheme}.csv")
        if kind != "exact":
            output_name = output_name.replace(".csv", f"_{kind}.csv")
        output_path = output_dir / output_name
        output_path.write_text(output_text, encoding="utf-8")
        print(f"wrote {output_path}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run SEAL/OpenFHE benchmark binaries from named corpus tests.",
    )
    parser.add_argument(
        "--all",
        action="store_true",
        help="run all known exact tests",
    )
    parser.add_argument(
        "--tests",
        default="quick8",
        help="comma-separated tests, e.g. quick8,edge,8,256,4096,8192,all or CKKS names from --list-tests --scheme ckks",
    )
    parser.add_argument(
        "--kind",
        choices=[
            "exact",
            "rotation",
            "serialization",
            "depth",
            "workload",
            "e2e",
            "memory",
            "ntt",
            "poly",
            "keyswitch",
            "matrix",
            "throughput",
            "heap",
            "footprint",
            "corpus-memory",
            "thread-memory",
            "cpu",
        ],
        default="exact",
        help="benchmark group to run",
    )
    parser.add_argument(
        "--scheme",
        choices=["bfv", "bgv", "ckks", "lowlevel"],
        default="bfv",
        help="scheme to run; use --scheme lowlevel for --kind ntt or --kind poly",
    )
    parser.add_argument(
        "--only",
        default="all",
        help="debug filter: seal,openfhe,openfhe4,openfhe6,all; with --thread-scaling: openfhe1,openfhe2,openfhe4,openfhe6,openfhe8,all",
    )
    parser.add_argument(
        "--thread-scaling",
        action="store_true",
        help="run OpenFHE-only thread scaling suites with OMP_NUM_THREADS=1,2,4,6,8",
    )
    parser.add_argument(
        "--ring-size",
        type=int,
        default=8192,
        help="ring/poly modulus degree passed to benchmark binaries",
    )
    parser.add_argument(
        "--ring-sizes",
        default="",
        help="comma-separated ring/poly modulus degrees to run, e.g. 2048,4096,8192,16384",
    )
    parser.add_argument(
        "--max-depth",
        type=int,
        default=4,
        help="maximum multiplicative depth for --kind depth",
    )
    parser.add_argument(
        "--duration-ms",
        type=int,
        default=5000,
        help="sustained-run duration for --kind throughput binaries",
    )
    parser.add_argument(
        "--ckks-config",
        choices=["default", "ring-sweep"],
        default="default",
        help="CKKS parameter profile; ring-sweep uses explicit low-depth params for ring-size sweeps",
    )
    parser.add_argument(
        "--ckks-depth",
        type=int,
        default=None,
        help="override CKKS multiplicative depth for CKKS binaries",
    )
    parser.add_argument(
        "--ckks-scale-bits",
        type=int,
        default=None,
        help="override CKKS scale/modulus limb bits for CKKS binaries",
    )
    parser.add_argument(
        "--ckks-first-mod-bits",
        type=int,
        default=None,
        help="override CKKS first/special modulus bits for CKKS binaries",
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
        "--warmups",
        type=int,
        default=0,
        help="number of full benchmark process warmup runs to discard before recording rows",
    )
    parser.add_argument(
        "--repetitions",
        type=int,
        default=1,
        help="number of recorded full benchmark process repetitions per suite/test",
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
        if args.scheme not in available_schemes_for_kind(args.kind):
            supported = ", ".join(available_schemes_for_kind(args.kind))
            print(f"scheme '{args.scheme}' is not available for kind '{args.kind}'; supported: {supported}", file=sys.stderr)
            return 2
        available_tests = available_tests_for(args.kind, args.scheme)
        for name, corpus in available_tests.items():
            print(f"{name}: {corpus}")
        return 0

    try:
        ring_sizes = parse_positive_int_list(args.ring_sizes, "--ring-sizes") if args.ring_sizes else [args.ring_size]
    except ValueError as error:
        print(error, file=sys.stderr)
        return 2
    if args.ring_size <= 0:
        print("--ring-size must be positive", file=sys.stderr)
        return 2
    if args.max_depth <= 0:
        print("--max-depth must be positive", file=sys.stderr)
        return 2
    if args.duration_ms <= 0:
        print("--duration-ms must be positive", file=sys.stderr)
        return 2
    for option_name in ("ckks_depth", "ckks_scale_bits", "ckks_first_mod_bits"):
        value = getattr(args, option_name)
        if value is not None and value <= 0:
            print(f"--{option_name.replace('_', '-')} must be positive", file=sys.stderr)
            return 2
    if args.scheme != "ckks" and args.ckks_config != "default":
        print("--ckks-config can only be used with --scheme ckks", file=sys.stderr)
        return 2
    if args.scheme != "ckks" and any(getattr(args, name) is not None for name in ("ckks_depth", "ckks_scale_bits", "ckks_first_mod_bits")):
        print("CKKS parameter overrides can only be used with --scheme ckks", file=sys.stderr)
        return 2
    if args.warmups < 0:
        print("--warmups must be zero or positive", file=sys.stderr)
        return 2
    if args.repetitions <= 0:
        print("--repetitions must be positive", file=sys.stderr)
        return 2
    if args.thread_scaling and args.kind == "serialization":
        print("--thread-scaling is not supported for serialization", file=sys.stderr)
        return 2

    try:
        suite_map = suites_for_run(args.thread_scaling)
        available_tests = available_tests_for(args.kind, args.scheme)
        if args.scheme not in available_schemes_for_kind(args.kind):
            supported = ", ".join(available_schemes_for_kind(args.kind))
            raise ValueError(f"scheme '{args.scheme}' is not available for kind '{args.kind}'; supported: {supported}")
        slot_limited_all = args.scheme == "ckks" or args.kind in {"workload", "e2e"}
        tests = expand_tests(args.tests, args.all, available_tests, slot_limited_all)
        suites = filter_suites_for_kind(expand_suites(args.only, suite_map), args.kind)
    except ValueError as error:
        print(error, file=sys.stderr)
        return 2

    if args.kind == "throughput":
        for test_name in tests:
            expected_ring = expected_throughput_ring(test_name)
            if expected_ring is None:
                continue
            for ring_size in ring_sizes:
                if ring_size != expected_ring:
                    print(
                        f"throughput test '{test_name}' must be paired with --ring-size {expected_ring}; "
                        f"got {ring_size}",
                        file=sys.stderr,
                    )
                    return 2

    if args.kind in {"workload", "e2e"}:
        for test_name in tests:
            active_slots = workload_active_slots(test_name)
            if active_slots is None:
                continue
            for ring_size in ring_sizes:
                slot_budget = ring_size // 2
                if active_slots > slot_budget:
                    print(
                        f"{args.kind} test '{test_name}' uses {active_slots} active slots, "
                        f"but --ring-size {ring_size} only supports {slot_budget} for this workload",
                        file=sys.stderr,
                    )
                    return 2

    build_dir = ROOT / args.build_dir
    output_by_suite: dict[str, list[str]] = {suite_name: [] for suite_name in suites}

    for suite_name in suites:
        suite = suite_map[suite_name]
        library = suite["library"]
        threads = int(suite["threads"])

        for test_name in tests:
            corpus = ROOT / available_tests[test_name]
            if not corpus.exists():
                print(f"missing corpus for test '{test_name}': {corpus}", file=sys.stderr)
                return 2

            binary_name = LIBRARIES[args.kind][args.scheme][str(library)]
            binary = build_dir / binary_name

            if not binary.exists() and not args.dry_run:
                print(f"missing binary for {library}: {binary}", file=sys.stderr)
                print("build it with: cmake --build cpp/build --target " + binary_name, file=sys.stderr)
                return 2

            # OpenFHE parallelism is controlled per child process so reports
            # with different thread counts do not share runtime state.
            env = build_env(str(library), threads)

            for ring_size in ring_sizes:
                command = build_command(
                    binary,
                    corpus,
                    ring_size,
                    args.max_depth,
                    args.scheme,
                    args.ckks_config,
                    args.ckks_depth,
                    args.ckks_scale_bits,
                    args.ckks_first_mod_bits,
                    args.duration_ms,
                )

                if args.dry_run:
                    print(env_prefix(str(library), threads) + " ".join(command))
                    continue

                for warmup_index in range(args.warmups):
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
                        if completed.stdout:
                            print(completed.stdout, file=sys.stderr, end="")
                        print(
                            "warmup failed "
                            f"(warmup={warmup_index + 1}): " + " ".join(command),
                            file=sys.stderr,
                        )
                        return completed.returncode

                for repeat_index in range(args.repetitions):
                    # The C++ executable emits one CSV-style line per operation.
                    # Preserve those lines and annotate corpus, thread count, and
                    # repetition in the same format for all reports.
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
                                f"test={test_name},threads={threads},"
                                f"repeat={repeat_index + 1},{line}"
                            )

                    if completed.stderr:
                        print(completed.stderr, file=sys.stderr, end="")
                    if completed.returncode != 0:
                        if completed.stdout:
                            print(completed.stdout, file=sys.stderr, end="")
                        if not args.stdout:
                            write_result_files(output_by_suite, suites, suite_map, args.out_dir, args.kind, args.scheme)
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
        write_result_files(output_by_suite, suites, suite_map, args.out_dir, args.kind, args.scheme)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
