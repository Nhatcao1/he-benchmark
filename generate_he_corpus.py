#!/usr/bin/env python3
"""
Generate a deterministic corpus for fair Microsoft SEAL vs OpenFHE benchmarking.

PURPOSE
-------
This script creates reusable datasets for:
  - BFV / BGV exact integer arithmetic
  - CKKS approximate floating-point arithmetic
  - Slot rotation validation
  - Multiplicative-depth validation
  - SIMD packing-size experiments
  - Optional CKKS matrix multiplication experiments

The generated corpus is intended to be consumed by two independent benchmark
harnesses:
  1. A Microsoft SEAL C++ benchmark harness
  2. An OpenFHE C++ benchmark harness

Both harnesses should read the same CSV files and compare decrypted outputs
against the same plaintext reference values.

DESIGN PRINCIPLES
-----------------
1. Deterministic generation
   Every file is generated from a stable, file-specific random seed. Adding a
   new dataset later will not change previously generated files.

2. Reproducibility
   Every generated file receives a SHA-256 checksum in manifest.json.

3. Separation of performance and validation
   The corpus contains expected values for correctness checks. Latency timing
   should still exclude validation overhead in the C++ benchmark harness.

4. Exact-scheme normalization
   BFV and BGV operate modulo plaintext modulus t. The CSV files store raw signed
   integer expectations. The C++ harness must normalize results modulo t before
   comparison.

5. CKKS accuracy evaluation
   CKKS is approximate. The C++ harness should calculate:
     - Mean absolute error (MAE)
     - Root mean squared error (RMSE)
     - Maximum absolute error
     - Mean relative error
     - Maximum relative error
     - Effective precision bits, where appropriate

6. Rotation debugging
   Rotation datasets use visible sequential patterns so left/right direction
   mismatches between libraries are easy to diagnose.

OUTPUT DIRECTORY
----------------
he_corpus/
  manifest.json

  exact/
    exact_safe_000008.csv
    exact_safe_000256.csv
    exact_safe_004096.csv
    exact_safe_008192.csv
    exact_edge_cases.csv

  rotation/
    rotation_000008.csv
    rotation_000256.csv
    rotation_004096.csv
    rotation_008192.csv

  ckks_normal/
    ckks_normal_000008.csv
    ...

  ckks_small/
    ckks_small_000008.csv
    ...

  ckks_near_zero/
    ckks_near_zero_000008.csv
    ...

  ckks_mixed_scale/
    ckks_mixed_scale_000008.csv
    ...

  depth/
    exact_depth_000008.csv
    ...
    ckks_depth_000008.csv
    ...

  matrices/
    matrix_a_0064x0064.csv
    matrix_b_0064x0064.csv
    matrix_expected_0064x0064.csv

HOW TO USE BFV / BGV EXPECTED VALUES
------------------------------------
The CSV stores raw signed integer values such as:
    expected_mul = a * b

The harness should compare after centered modular normalization:

    expected_mod = expected_raw % plain_modulus

    if expected_mod > plain_modulus // 2:
        expected_signed = expected_mod - plain_modulus
    else:
        expected_signed = expected_mod

Apply the same rule to the decrypted result before comparing.

HOW TO USE CKKS EXPECTED VALUES
-------------------------------
CKKS outputs should not be compared with exact equality.

For each decrypted vector, compare against the matching expected column and
calculate numerical error metrics.

HOW TO USE DEPTH DATASETS
-------------------------
The depth workload is intentionally simple:

    depth_1 = a * b
    depth_2 = depth_1 * depth_1
    depth_3 = depth_2 * depth_2
    depth_4 = depth_3 * depth_3

For BFV/BGV:
  - Measure the last correct depth
  - Measure the first failed depth
  - Record remaining noise budget when available

For CKKS:
  - Record MAE, RMSE, maximum absolute error, relative error, and precision bits
    at each depth
  - Record remaining levels

DEPENDENCIES
------------
Python 3.10+
NumPy

Example:
    python3 generate_he_corpus.py
"""

from __future__ import annotations

import csv
import hashlib
import json
import platform
from pathlib import Path
from typing import Iterable

import numpy as np


# ============================================================================
# Global configuration
# ============================================================================

OUT_DIR = Path("he_corpus")

# Stable root seed. File-specific seeds are derived from this value and a
# namespace so that existing files remain unchanged when new datasets are added.
ROOT_SEED = 42

# Common packed-vector sizes for smoke tests, moderate tests, and larger SIMD
# tests. Actual usable slot capacity still depends on the chosen HE parameters.
VECTOR_SIZES = [8, 256, 4096, 8192]

# Exact arithmetic range chosen to avoid accidental wraparound for common
# plaintext moduli during basic tests.
#
# With [-500, 500]:
#   maximum absolute multiplication result = 250,000
#
# This is intentionally conservative. Boundary-specific tests are generated
# separately in exact_edge_cases.csv.
INTEGER_SAFE_LOW = -500
INTEGER_SAFE_HIGH = 500

# CKKS dataset configuration.
CKKS_NORMAL_MEAN = 0.0
CKKS_NORMAL_STDDEV = 10.0

CKKS_SMALL_LOW = -1.0
CKKS_SMALL_HIGH = 1.0

CKKS_NEAR_ZERO_LOW = -1e-6
CKKS_NEAR_ZERO_HIGH = 1e-6

# Multiplicative-depth workload length.
MAX_DEPTH = 4

# Optional application-style CKKS matrix workload.
MATRIX_SIZE = 64

# Generic exact edge cases. These are independent of a specific plain modulus.
# Plain-modulus boundary cases should be generated inside the C++ harness if the
# benchmark parameter set varies.
GENERIC_EXACT_EDGE_VALUES = [
    -500,
    -100,
    -10,
    -2,
    -1,
    0,
    1,
    2,
    10,
    100,
    500,
]

# Pattern used to build mixed-scale CKKS data. The pattern is repeated as needed.
CKKS_MIXED_SCALE_PATTERN = [
    -1e-6,
    1e-6,
    -1e-3,
    1e-3,
    -0.125,
    0.125,
    -1.0,
    1.0,
    -12.5,
    12.5,
    -250.0,
    250.0,
    -1000.0,
    1000.0,
]


# ============================================================================
# Utility functions
# ============================================================================

def stable_seed(namespace: str, size: int | None = None) -> int:
    """
    Derive a stable child seed from the root seed and a dataset namespace.

    WHY THIS MATTERS
    ----------------
    A single sequential RNG changes all later files if a new dataset is inserted
    earlier in the generation order.

    This function isolates each file:
      - exact_safe_000256.csv always receives the same seed
      - ckks_normal_000256.csv always receives the same seed
      - adding a new dataset does not alter prior checksums
    """
    material = f"{ROOT_SEED}:{namespace}:{size if size is not None else 'none'}"
    digest = hashlib.sha256(material.encode("utf-8")).digest()
    return int.from_bytes(digest[:8], byteorder="little", signed=False)


def make_rng(namespace: str, size: int | None = None) -> np.random.Generator:
    """
    Create a deterministic NumPy generator for one corpus file.
    """
    return np.random.default_rng(stable_seed(namespace, size))


def sha256sum(path: Path) -> str:
    """
    Compute SHA-256 for one generated file.
    """
    digest = hashlib.sha256()

    with path.open("rb") as file:
        for block in iter(lambda: file.read(65536), b""):
            digest.update(block)

    return digest.hexdigest()


def write_rows(path: Path, header: list[str], rows: Iterable[Iterable[object]]) -> None:
    """
    Write a CSV file with a header row.
    """
    path.parent.mkdir(parents=True, exist_ok=True)

    with path.open("w", newline="", encoding="utf-8") as file:
        writer = csv.writer(file)
        writer.writerow(header)
        writer.writerows(rows)


def write_matrix(path: Path, matrix: np.ndarray) -> None:
    """
    Write a matrix with enough decimal precision for stable CKKS references.
    """
    path.parent.mkdir(parents=True, exist_ok=True)

    np.savetxt(
        path,
        matrix,
        delimiter=",",
        fmt="%.17g",
    )


def file_record(path: Path, role: str, rows: int | None = None) -> dict[str, object]:
    """
    Build manifest metadata for one generated file.
    """
    record: dict[str, object] = {
        "role": role,
        "sha256": sha256sum(path),
        "bytes": path.stat().st_size,
    }

    if rows is not None:
        record["rows"] = rows

    return record


# ============================================================================
# Exact BFV / BGV corpus
# ============================================================================

def generate_exact_safe_vectors(size: int) -> Path:
    """
    Generate conservative signed-integer vectors for BFV/BGV primitive tests.

    Recommended uses:
      - Encode / decode
      - Encrypt / decrypt
      - Add ciphertext-ciphertext
      - Subtract ciphertext-ciphertext
      - Add ciphertext-plaintext
      - Subtract ciphertext-plaintext
      - Multiply ciphertext-ciphertext
      - Multiply ciphertext-plaintext
      - Square
      - Negate
      - Relinearization invariance
      - Mod-switch invariance
      - Serialization round trip

    The CSV stores raw signed expectations. Normalize modulo plaintext modulus
    inside the C++ harness.
    """
    rng = make_rng("exact_safe", size)

    a = rng.integers(
        INTEGER_SAFE_LOW,
        INTEGER_SAFE_HIGH + 1,
        size=size,
        dtype=np.int64,
    )

    b = rng.integers(
        INTEGER_SAFE_LOW,
        INTEGER_SAFE_HIGH + 1,
        size=size,
        dtype=np.int64,
    )

    rows = [
        [
            int(index),
            int(a[index]),
            int(b[index]),
            int(a[index] + b[index]),
            int(a[index] - b[index]),
            int(a[index] * b[index]),
            int(a[index] * a[index]),
            int(-a[index]),
        ]
        for index in range(size)
    ]

    path = OUT_DIR / "exact" / f"exact_safe_{size:06d}.csv"

    write_rows(
        path,
        [
            "index",
            "a",
            "b",
            "expected_add",
            "expected_sub",
            "expected_mul",
            "expected_square_a",
            "expected_negate_a",
        ],
        rows,
    )

    return path


def generate_exact_edge_cases() -> Path:
    """
    Generate a small human-readable edge-case file for exact schemes.

    This file intentionally avoids plain-modulus-specific values because the
    benchmark harness may test multiple plaintext moduli.

    If required, the C++ harness should dynamically add:
      - plain_modulus // 2 - 1
      - plain_modulus // 2
      - plain_modulus // 2 + 1
      - plain_modulus - 1
    """
    values = GENERIC_EXACT_EDGE_VALUES

    rows = []
    index = 0

    for a in values:
        for b in values:
            rows.append(
                [
                    index,
                    a,
                    b,
                    a + b,
                    a - b,
                    a * b,
                    a * a,
                    -a,
                ]
            )
            index += 1

    path = OUT_DIR / "exact" / "exact_edge_cases.csv"

    write_rows(
        path,
        [
            "index",
            "a",
            "b",
            "expected_add",
            "expected_sub",
            "expected_mul",
            "expected_square_a",
            "expected_negate_a",
        ],
        rows,
    )

    return path


# ============================================================================
# Rotation corpus
# ============================================================================

def generate_rotation_vectors(size: int) -> Path:
    """
    Generate an easy-to-debug sequential slot pattern.

    PURPOSE
    -------
    Rotation semantics can differ in sign convention:
      - One implementation may interpret +1 as a left rotation
      - Another implementation may interpret +1 as a right rotation

    Sequential input makes the mismatch immediately visible.

    The harness should test:
      - Rotate +1
      - Rotate -1
      - Rotate +8
    """
    values = list(range(size))

    rows = [
        [index, value]
        for index, value in enumerate(values)
    ]

    path = OUT_DIR / "rotation" / f"rotation_{size:06d}.csv"

    write_rows(
        path,
        [
            "index",
            "value",
        ],
        rows,
    )

    return path


# ============================================================================
# CKKS vector corpora
# ============================================================================

def build_ckks_rows(a: np.ndarray, b: np.ndarray) -> list[list[object]]:
    """
    Build common CKKS primitive-test rows.

    These expected values support:
      - Add
      - Subtract
      - Multiply
      - Square
      - Negate
    """
    size = len(a)

    return [
        [
            int(index),
            float(a[index]),
            float(b[index]),
            float(a[index] + b[index]),
            float(a[index] - b[index]),
            float(a[index] * b[index]),
            float(a[index] * a[index]),
            float(-a[index]),
        ]
        for index in range(size)
    ]


def write_ckks_vectors(path: Path, a: np.ndarray, b: np.ndarray) -> Path:
    """
    Write one CKKS primitive corpus file.
    """
    write_rows(
        path,
        [
            "index",
            "a",
            "b",
            "expected_add",
            "expected_sub",
            "expected_mul",
            "expected_square_a",
            "expected_negate_a",
        ],
        build_ckks_rows(a, b),
    )

    return path


def generate_ckks_normal_vectors(size: int) -> Path:
    """
    Generate a general-purpose CKKS corpus using Normal(0, 10).
    """
    rng = make_rng("ckks_normal", size)

    a = rng.normal(CKKS_NORMAL_MEAN, CKKS_NORMAL_STDDEV, size=size)
    b = rng.normal(CKKS_NORMAL_MEAN, CKKS_NORMAL_STDDEV, size=size)

    path = OUT_DIR / "ckks_normal" / f"ckks_normal_{size:06d}.csv"
    return write_ckks_vectors(path, a, b)


def generate_ckks_small_vectors(size: int) -> Path:
    """
    Generate small-magnitude CKKS values in [-1, 1].

    This dataset is useful for precision studies because values remain bounded
    during shallow multiplication chains.
    """
    rng = make_rng("ckks_small", size)

    a = rng.uniform(CKKS_SMALL_LOW, CKKS_SMALL_HIGH, size=size)
    b = rng.uniform(CKKS_SMALL_LOW, CKKS_SMALL_HIGH, size=size)

    path = OUT_DIR / "ckks_small" / f"ckks_small_{size:06d}.csv"
    return write_ckks_vectors(path, a, b)


def generate_ckks_near_zero_vectors(size: int) -> Path:
    """
    Generate near-zero CKKS values.

    WHY THIS MATTERS
    ----------------
    Relative error becomes unstable near zero. This dataset ensures the report
    includes absolute-error metrics rather than relying only on relative error.
    """
    rng = make_rng("ckks_near_zero", size)

    a = rng.uniform(CKKS_NEAR_ZERO_LOW, CKKS_NEAR_ZERO_HIGH, size=size)
    b = rng.uniform(CKKS_NEAR_ZERO_LOW, CKKS_NEAR_ZERO_HIGH, size=size)

    path = OUT_DIR / "ckks_near_zero" / f"ckks_near_zero_{size:06d}.csv"
    return write_ckks_vectors(path, a, b)


def generate_ckks_mixed_scale_vectors(size: int) -> Path:
    """
    Generate CKKS values across multiple orders of magnitude.

    The dataset is deterministic and intentionally mixes:
      - Near-zero values
      - Small decimals
      - Unit-scale values
      - Moderate values
      - Larger values
      - Positive and negative signs

    This is useful for revealing cases where a parameter set performs well on
    one scale but poorly on another.
    """
    pattern = np.array(CKKS_MIXED_SCALE_PATTERN, dtype=np.float64)

    a = np.resize(pattern, size)
    b = np.resize(pattern[::-1], size)

    path = OUT_DIR / "ckks_mixed_scale" / f"ckks_mixed_scale_{size:06d}.csv"
    return write_ckks_vectors(path, a, b)


# ============================================================================
# Multiplicative-depth corpora
# ============================================================================

def exact_depth_values(a: int, b: int) -> list[int]:
    """
    Return raw signed exact-scheme depth values.

    The harness must normalize each value modulo plaintext modulus.
    """
    depth_values = []

    current = a * b
    depth_values.append(current)

    for _ in range(2, MAX_DEPTH + 1):
        current = current * current
        depth_values.append(current)

    return depth_values


def ckks_depth_values(a: float, b: float) -> list[float]:
    """
    Return CKKS plaintext reference values for the depth workload.
    """
    depth_values = []

    current = a * b
    depth_values.append(current)

    for _ in range(2, MAX_DEPTH + 1):
        current = current * current
        depth_values.append(current)

    return depth_values


def generate_exact_depth_vectors(size: int) -> Path:
    """
    Generate bounded exact-scheme depth data.

    Values are intentionally restricted to a small range because repeated
    squaring grows quickly.
    """
    rng = make_rng("exact_depth", size)

    a = rng.integers(-3, 4, size=size, dtype=np.int64)
    b = rng.integers(-3, 4, size=size, dtype=np.int64)

    rows = []

    for index in range(size):
        values = exact_depth_values(int(a[index]), int(b[index]))

        rows.append(
            [
                int(index),
                int(a[index]),
                int(b[index]),
                *values,
            ]
        )

    path = OUT_DIR / "depth" / f"exact_depth_{size:06d}.csv"

    write_rows(
        path,
        [
            "index",
            "a",
            "b",
            *[f"expected_depth_{depth}" for depth in range(1, MAX_DEPTH + 1)],
        ],
        rows,
    )

    return path


def generate_ckks_depth_vectors(size: int) -> Path:
    """
    Generate bounded CKKS depth data.

    Values are drawn from [-0.9, 0.9] so repeated squaring remains numerically
    manageable while still exposing precision degradation across levels.
    """
    rng = make_rng("ckks_depth", size)

    a = rng.uniform(-0.9, 0.9, size=size)
    b = rng.uniform(-0.9, 0.9, size=size)

    rows = []

    for index in range(size):
        values = ckks_depth_values(float(a[index]), float(b[index]))

        rows.append(
            [
                int(index),
                float(a[index]),
                float(b[index]),
                *values,
            ]
        )

    path = OUT_DIR / "depth" / f"ckks_depth_{size:06d}.csv"

    write_rows(
        path,
        [
            "index",
            "a",
            "b",
            *[f"expected_depth_{depth}" for depth in range(1, MAX_DEPTH + 1)],
        ],
        rows,
    )

    return path


# ============================================================================
# CKKS matrix multiplication corpus
# ============================================================================

def generate_matrices() -> list[Path]:
    """
    Generate an optional application-style CKKS matrix multiplication workload.

    IMPORTANT
    ---------
    Matrix multiplication is not a single HE primitive.

    A fair SEAL vs OpenFHE matrix benchmark must document:
      - SIMD packing layout
      - Number of ciphertexts
      - Number of rotations
      - Number of additions
      - Number of ciphertext-plaintext multiplications
      - Number of ciphertext-ciphertext multiplications
      - Number of relinearization operations
      - Number of rescale operations
      - Thread count
    """
    rng = make_rng("matrices", MATRIX_SIZE)

    a = rng.normal(
        CKKS_NORMAL_MEAN,
        CKKS_NORMAL_STDDEV,
        size=(MATRIX_SIZE, MATRIX_SIZE),
    )

    b = rng.normal(
        CKKS_NORMAL_MEAN,
        CKKS_NORMAL_STDDEV,
        size=(MATRIX_SIZE, MATRIX_SIZE),
    )

    expected = a @ b

    outputs = {
        OUT_DIR / "matrices" / f"matrix_a_{MATRIX_SIZE:04d}x{MATRIX_SIZE:04d}.csv": a,
        OUT_DIR / "matrices" / f"matrix_b_{MATRIX_SIZE:04d}x{MATRIX_SIZE:04d}.csv": b,
        OUT_DIR / "matrices" / f"matrix_expected_{MATRIX_SIZE:04d}x{MATRIX_SIZE:04d}.csv": expected,
    }

    for path, matrix in outputs.items():
        write_matrix(path, matrix)

    return list(outputs.keys())


# ============================================================================
# Manifest generation
# ============================================================================

def generate_manifest(generated_files: list[tuple[Path, str, int | None]]) -> Path:
    """
    Write manifest.json with corpus metadata and SHA-256 checksums.
    """
    manifest = {
        "description": (
            "Deterministic synthetic corpus for fair Microsoft SEAL vs OpenFHE "
            "benchmarking, correctness validation, and CKKS accuracy analysis."
        ),
        "corpus_version": "2.0.0",
        "generator_version": "2.0.0",
        "root_seed": ROOT_SEED,
        "python_version": platform.python_version(),
        "numpy_version": np.__version__,
        "integer_dtype": "int64",
        "float_dtype": "float64",
        "signed_decode_rule": "canonical-centered-modulo",
        "vector_sizes": VECTOR_SIZES,
        "max_depth": MAX_DEPTH,
        "exact_safe_integer_range": [
            INTEGER_SAFE_LOW,
            INTEGER_SAFE_HIGH,
        ],
        "ckks_datasets": {
            "normal": {
                "distribution": "normal",
                "mean": CKKS_NORMAL_MEAN,
                "stddev": CKKS_NORMAL_STDDEV,
            },
            "small": {
                "distribution": "uniform",
                "low": CKKS_SMALL_LOW,
                "high": CKKS_SMALL_HIGH,
            },
            "near_zero": {
                "distribution": "uniform",
                "low": CKKS_NEAR_ZERO_LOW,
                "high": CKKS_NEAR_ZERO_HIGH,
            },
            "mixed_scale": {
                "distribution": "fixed_repeating_pattern",
                "pattern": CKKS_MIXED_SCALE_PATTERN,
            },
        },
        "depth_workload": {
            "depth_1": "a * b",
            "depth_n": "depth_(n-1) * depth_(n-1)",
        },
        "matrix_size": MATRIX_SIZE,
        "files": {
            str(path): file_record(path, role, rows)
            for path, role, rows in generated_files
        },
    }

    manifest_path = OUT_DIR / "manifest.json"

    manifest_path.write_text(
        json.dumps(manifest, indent=2),
        encoding="utf-8",
    )

    return manifest_path


# ============================================================================
# Main program
# ============================================================================

def main() -> None:
    """
    Generate the complete benchmark corpus.
    """
    OUT_DIR.mkdir(parents=True, exist_ok=True)

    generated_files: list[tuple[Path, str, int | None]] = []

    # Exact BFV/BGV primitive datasets.
    for size in VECTOR_SIZES:
        path = generate_exact_safe_vectors(size)
        generated_files.append((path, "exact_safe", size))

    # Human-readable exact edge cases.
    edge_path = generate_exact_edge_cases()
    generated_files.append((edge_path, "exact_edge_cases", len(GENERIC_EXACT_EDGE_VALUES) ** 2))

    # Rotation validation datasets.
    for size in VECTOR_SIZES:
        path = generate_rotation_vectors(size)
        generated_files.append((path, "rotation_pattern", size))

    # CKKS primitive datasets.
    for size in VECTOR_SIZES:
        path = generate_ckks_normal_vectors(size)
        generated_files.append((path, "ckks_normal", size))

        path = generate_ckks_small_vectors(size)
        generated_files.append((path, "ckks_small", size))

        path = generate_ckks_near_zero_vectors(size)
        generated_files.append((path, "ckks_near_zero", size))

        path = generate_ckks_mixed_scale_vectors(size)
        generated_files.append((path, "ckks_mixed_scale", size))

    # Multiplicative-depth datasets.
    for size in VECTOR_SIZES:
        path = generate_exact_depth_vectors(size)
        generated_files.append((path, "exact_depth", size))

        path = generate_ckks_depth_vectors(size)
        generated_files.append((path, "ckks_depth", size))

    # Optional application-style matrix workload.
    matrix_paths = generate_matrices()

    generated_files.extend(
        [
            (matrix_paths[0], "matrix_a", MATRIX_SIZE),
            (matrix_paths[1], "matrix_b", MATRIX_SIZE),
            (matrix_paths[2], "matrix_expected", MATRIX_SIZE),
        ]
    )

    manifest_path = generate_manifest(generated_files)

    print(f"Generated {len(generated_files)} corpus files.")
    print(f"Output directory: {OUT_DIR.resolve()}")
    print(f"Manifest:         {manifest_path.resolve()}")
    print()
    print("Recommended first files:")
    print("  BFV/BGV smoke test:      he_corpus/exact/exact_safe_000008.csv")
    print("  BFV/BGV main test:       he_corpus/exact/exact_safe_000256.csv")
    print("  Rotation smoke test:     he_corpus/rotation/rotation_000008.csv")
    print("  CKKS smoke test:         he_corpus/ckks_normal/ckks_normal_000008.csv")
    print("  CKKS accuracy test:      he_corpus/ckks_mixed_scale/ckks_mixed_scale_000256.csv")
    print("  Exact depth test:        he_corpus/depth/exact_depth_000256.csv")
    print("  CKKS depth test:         he_corpus/depth/ckks_depth_000256.csv")


if __name__ == "__main__":
    main()
