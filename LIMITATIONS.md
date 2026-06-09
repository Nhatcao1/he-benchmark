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
- Use `--only seal`, `--only openfhe`, `--only openfhe4`, or `--only openfhe6` for
  library-specific ring experiments that are not valid combined comparisons.

## BFV Corpus Size vs Ring Size

- Do not run `--all` with a ring size smaller than `8192`.
- `--all` includes the `8192` corpus file, so smaller rings cannot pack all
  active slots.

## CKKS Corpus Size vs Ring Size

- CKKS packed slot capacity is `ring_size / 2`.
- OpenFHE CKKS rejects `ring_size=8192` under its current HE-standard security
  check and requests `16384`.
- Use `--ring-size 16384` for combined SEAL + OpenFHE CKKS runs.
- With `--ring-size 16384`, CKKS files up to 8192 rows fit the packed slot
  capacity.
- The same slot-capacity rule applies to `he_corpus/depth/ckks_depth_*.csv`.
  The runner's CKKS `--kind depth --all` selection excludes the 8192-row file
  by default; run that file explicitly with `--ring-size 16384`.
- CKKS correctness is approximate. Compare `mae`, `rmse`, max error, relative
  error, `precision_bits`, and `pass_rate`; do not expect exact equality.
- `--ckks-config ring-sweep` is an explicit ring-size scaling profile. It uses
  low-depth CKKS parameters and `security=not_set` so SEAL and OpenFHE can both
  run small rings such as 2048 and 4096. Do not mix ring-sweep rows with default
  CKKS rows in one performance comparison without grouping by `ckks_config`.
- For ring sweeps, keep the active corpus size below `ring_size / 2`; `quick8`
  and `normal256` are the safest first tests.

## Multiplicative Depth

- Depth runs use `operation=depth_mul` and the generator-defined workload:
  `depth_1 = a * b`, then `depth_n = depth_(n-1) * depth_(n-1)`.
- Depth runners return success after recording the first failed depth. A
  `correct=false` row is the measured limit for the selected parameter set, not
  automatically a broken executable.
- Exact BFV/BGV rows compare centered plaintext residues modulo the plaintext
  modulus. CKKS rows compare approximate values and can show large relative
  errors when expected values are near zero even if absolute error passes.
- The default CKKS depth runner uses smaller scale bits than the CKKS primitive
  runner so four rescale levels fit in the default `8192` ring.

## OpenFHE Serialization

- OpenFHE object serialization is isolated in scheme-specific serialization
  runners instead of being mixed into exact, depth, or rotation correctness
  runners.
- OpenFHE serialization requires the OpenFHE serialization headers such as
  `ciphertext-ser.h`, `cryptocontext-ser.h`, `key/key-ser.h`, and the matching
  scheme registration header: `scheme/bfvrns/bfvrns-ser.h`,
  `scheme/bgvrns/bgvrns-ser.h`, or `scheme/ckksrns/ckksrns-ser.h`.
- If OpenFHE serialization fails, exact and rotation runners should still be
  treated independently.
- SEAL BFV rows still report byte sizes where available.

## Serialization Thread Scaling

- Serialization runs intentionally skip the OpenFHE 4-thread and 6-thread suites.
- Key and object serialization should be treated as single-process baseline I/O
  and object encoding work, not as an OpenMP thread-scaling primitive.
- For serialization, compare `byte_size`, `latency_ms`, and `mb_per_sec`.

## OpenFHE Thread Scaling

- The normal comparison suite intentionally remains SEAL baseline, OpenFHE with
  `OMP_NUM_THREADS=1`, OpenFHE with `OMP_NUM_THREADS=4`, and OpenFHE with
  `OMP_NUM_THREADS=6`.
- Use `--thread-scaling` for OpenFHE-only scaling runs across
  `OMP_NUM_THREADS=1,2,4,6,8`.
- OpenFHE child processes are launched with `OMP_DYNAMIC=FALSE`,
  `OMP_PROC_BIND=close`, and `OMP_PLACES=cores` to reduce dynamic OpenMP
  scheduling and affinity noise.
- Single-operation rows can be noisy, especially for thread counts above one.
  For suspicious results, use `--warmups 1 --repetitions 5` and compare median
  rows per operation/thread.
- SEAL is not included in the scaling suite because this benchmark harness does
  not expose an equivalent per-run SEAL thread-count control.
- Thread scaling is available for exact, rotation, depth, workload, and memory
  runners. Serialization rejects `--thread-scaling`.

## Advanced Benchmarks

- SEAL low-level NTT and polynomial arithmetic targets use `seal::util`
  internal helpers from the sibling SEAL source checkout. Treat those rows as
  low-level kernel measurements, not public SEAL API portability tests.
- OpenFHE low-level NTT rows use polynomial format switching between
  coefficient and evaluation formats.
- Generic key switching is implemented with OpenFHE public `KeySwitchGen` and
  `KeySwitch`. SEAL rows report `supported=false` because SEAL exposes
  relinearization and Galois-key switching but not generic public key-to-key
  switching.
- CKKS matrix multiplication is implemented as encrypted A rows multiplied by
  plaintext B columns, with rotate-sum dot products for each 64x64 output cell.
  It is a real CKKS matrix workload, but not ciphertext-ciphertext matrix
  multiplication.
- Heap tracing counts C++ `new`/`delete` activity in the benchmark process.
  `allocated_bytes` and `peak_live_bytes` are the primary fields; some library
  deallocations may not be visible when the compiler/runtime uses unsized
  deallocation paths.
- Corpus-memory and thread-memory runners hold one ciphertext per corpus row.
  Increase corpus size gradually because ciphertext count directly controls
  resident memory pressure.

## End-to-End Dot Product

- The current end-to-end workload is a packed vector dot product:
  encode/encrypt two vectors, multiply slotwise, rotate-sum, decrypt, and decode
  the scalar result.
- Exact BFV/BGV dot runs are intentionally limited to one batching row. With
  `ring_size=8192`, use workload files up to 4096 rows.
- The dot workload requires power-of-two row counts because the rotate-sum tree
  uses steps 1, 2, 4, and so on.
- This is an end-to-end workload sanity benchmark, not a full matrix-vector or
  matrix-matrix packing benchmark.

## End-to-End Request/Response

- The `--kind e2e` runner records two request/response workflows:
  `end_to_end_sum` and `end_to_end_dot_product_pt`.
- Those rows include client request ciphertext serialization, server-side
  deserialization/evaluation/serialization, client-side deserialization,
  decryption, and decode in `latency_ms`.
- `server_eval_latency_ms` isolates only the server homomorphic evaluation part.
- `request_bytes` and `response_bytes` are ciphertext byte sizes for the
  simulated wire payloads, not full context/key distribution sizes.
- The plaintext-weighted dot product multiplies encrypted `a` by plaintext
  weights `b`, then rotate-sums the packed vector. It is intentionally different
  from the older `--kind workload` ciphertext-ciphertext dot product.

## Memory Measurement

- Memory rows report process peak RSS via `getrusage(RUSAGE_SELF).ru_maxrss`,
  normalized to KB.
- Peak RSS is monotonic within a process. `delta_peak_rss_kb` is relative to the
  beginning of that benchmark process, not isolated allocation for only the
  named operation.
- Memory rows are best used to compare whole benchmark phases and final peak
  footprint across libraries/schemes under the same server OS.

## BFV/BGV Rotation Semantics

- BFV and BGV rotation correctness is checked with library-specific packing
  semantics.
- Rotation tests do not technically require exact tests to run first, but exact
  BFV/BGV should be treated as the sanity prerequisite because both groups
  depend on the same key generation, encoding, encryption, decryption, and slot
  comparison assumptions.
- SEAL `BatchEncoder` rotation is modeled as row-wise rotation over its padded
  two-row batching layout.
- OpenFHE BFV/BGV rotation is also checked row-wise over the effective
  half-ring rotation domain observed for packed exact vectors.
- Because of these packing differences, rotation results should be compared by
  operation timing and correctness status, not by assuming identical slot-index
  movement across SEAL and OpenFHE.

## BFV Modulus Switching

- SEAL BFV supports `mod_switch_to_next` in the current exact runner and reports
  `operation=mod_switch`.
- OpenFHE BFV rows report `operation=mod_switch,supported=false` because
  OpenFHE's public `ModReduce` / `LevelReduce` APIs are documented for BGV and
  CKKS rather than BFV.
- Treat OpenFHE BFV mod-switch rows as a recorded library/scheme limitation, not
  as a failed correctness result.

## Current Scope

- Current implemented scheme scope is BFV exact primitive benchmarking, BGV
  exact primitive benchmarking, BFV/BGV rotation benchmarking, BFV/BGV/CKKS
  multiplicative-depth benchmarking, BFV/BGV/CKKS serialization benchmarking,
  BFV/BGV/CKKS dot-product end-to-end workload benchmarking, BFV/BGV/CKKS
  request/response end-to-end sum and plaintext-weighted dot-product
  benchmarking, and CKKS approximate primitive benchmarking. BFV/BGV/CKKS peak-RSS memory
  benchmarking is also implemented as `--kind memory`.
- BGV exact is implemented as separate `scheme=BGV` benchmark targets and
  result rows. BGV serialization and rotation are implemented as separate
  runners.
- CKKS primitive rows are implemented as `scheme=CKKS` benchmark targets and
  report MAE, RMSE, max error, relative error, precision bits, scale, and level
  fields. CKKS depth is implemented as a separate `--kind depth` runner.
  CKKS serialization is implemented as a separate `--kind serialization`
  runner. CKKS matrix multiplication is implemented as `--kind matrix`.
- OpenFHE thread scaling is implemented as a runner-level mode and writes
  separate scaling files so normal SEAL/OpenFHE comparison reports stay stable.
- Advanced groups are implemented for low-level NTT/INTT, low-level polynomial
  arithmetic, key switching, CKKS 64x64 matrix multiplication, heap tracing,
  persistent footprint, corpus memory scaling, thread memory scaling, and CPU
  utilization rows.
