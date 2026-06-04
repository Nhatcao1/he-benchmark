# Benchmark Limitations

This file records library and parameter limitations observed while running the
SEAL/OpenFHE benchmark suite. It is not a bug log.

## BFV Ring Size

- Combined SEAL + OpenFHE BFV runs should use `ring_size >= 8192`.
- Ring-size support is not assumed to be symmetric between libraries. Combined
  runs are limited by the stricter library for the selected parameter set.
- `ring_size=2048` is too small for the expanded SEAL BFV benchmark because the
  current suite creates relinearization keys and runs relin/key-switch dependent
  operations.
- `ring_size=4096` may work for SEAL-only runs, but OpenFHE BFVRNS rejects it
  under the current security settings and requests `8192`.
- Recommended combined comparison ring sizes:
  - `8192`
  - `16384`
- Use `--only seal`, `--only openfhe`, or `--only openfhe6` for
  library-specific ring experiments that are not valid combined comparisons.

## BFV Corpus Size vs Ring Size

- Do not run `--all` with a ring size smaller than `8192`.
- `--all` includes the `8192` corpus file, so smaller rings cannot pack all
  active slots.

## OpenFHE Serialization

- OpenFHE BFV object serialization is isolated in `openfhe_bfv_serialization`
  instead of being mixed into exact or rotation correctness runners.
- OpenFHE serialization requires the OpenFHE serialization headers such as
  `ciphertext-ser.h`, `cryptocontext-ser.h`, `key/key-ser.h`, and
  `scheme/bfvrns/bfvrns-ser.h` so cereal type registration is available.
- If OpenFHE serialization fails, exact and rotation runners should still be
  treated independently.
- SEAL BFV rows still report byte sizes where available.

## Serialization Thread Scaling

- Key and object serialization rows may appear in both OpenFHE 1-thread and
  OpenFHE 6-thread result files because the runner executes both suites.
- Those rows should not be interpreted as meaningful OpenMP thread-scaling
  measurements unless the underlying library serialization routine is known to
  parallelize internally.
- For serialization, compare `byte_size`, `latency_ms`, and `mb_per_sec` first;
  thread scaling is secondary and may be noise.

## BFV Rotation Semantics

- BFV rotation correctness is checked with library-specific packing semantics.
- Rotation tests do not technically require exact tests to run first, but exact
  BFV should be treated as the sanity prerequisite because both groups depend on
  the same key generation, encoding, encryption, decryption, and slot comparison
  assumptions.
- SEAL `BatchEncoder` rotation is modeled as row-wise rotation over its padded
  two-row batching layout.
- OpenFHE BFV rotation is also checked row-wise over the effective half-ring
  rotation domain observed for packed BFV vectors.
- Because of these packing differences, rotation results should be compared by
  operation timing and correctness status, not by assuming identical slot-index
  movement across SEAL and OpenFHE.

## Current Scope

- Current implemented scheme scope is BFV exact primitive benchmarking and BFV
  rotation benchmarking.
- BGV is not treated as identical to BFV. It should be added as separate
  `scheme=BGV` benchmark targets and result rows.
- CKKS requires separate numerical accuracy metrics such as MAE, RMSE, max
  error, and relative error.
