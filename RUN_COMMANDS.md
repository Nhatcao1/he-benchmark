# Benchmark Criteria Commands

Run commands from the repository root.

This file is organized by the report criteria. Current implemented exact scope
is BFV and BGV primitive benchmarking. CKKS approximate primitive benchmarking
is also implemented for `--kind exact --scheme ckks`. Multiplicative-depth
benchmarking is implemented for BFV, BGV, and CKKS with `--kind depth`.
End-to-end dot-product workload benchmarking is implemented with
`--kind workload`. Request/response end-to-end sum and plaintext-weighted dot
product benchmarking is implemented with `--kind e2e`. Memory benchmarking is
implemented with `--kind memory`. Sustained packed-operation throughput is
implemented separately with `--kind throughput`:

```text
SEAL baseline
OpenFHE with OMP_NUM_THREADS=1
OpenFHE with OMP_NUM_THREADS=4
OpenFHE with OMP_NUM_THREADS=6
```

OpenFHE-only thread scaling is separate from the normal comparison:

```bash
./run_benchmarks.py --thread-scaling --kind workload --scheme bfv --tests quick8 --ring-size 8192
```

For suspicious thread results, use warmups and repeated process runs:

```bash
./run_benchmarks.py --thread-scaling --scheme bfv --tests 8192 --ring-size 8192 --warmups 1 --repetitions 5 --out-dir cpp/results/thread_scaling_bfv8192_repeat
```

The runner pins OpenFHE child processes with `OMP_DYNAMIC=FALSE`,
`OMP_PROC_BIND=close`, and `OMP_PLACES=cores`, and every recorded row includes
`repeat=...` alongside `started_unix`, `started_utc`, `test`, and `threads`.

Advanced benchmark groups are also available:

```bash
./run_benchmarks.py --kind ntt --scheme lowlevel --tests quick8 --ring-size 8192
./run_benchmarks.py --kind poly --scheme lowlevel --tests quick8 --ring-size 8192
./run_benchmarks.py --kind keyswitch --scheme bfv --tests quick8 --ring-size 8192
./run_benchmarks.py --kind matrix --scheme ckks --all --ring-size 16384
./run_benchmarks.py --kind heap --scheme bfv --tests quick8 --ring-size 8192
./run_benchmarks.py --kind footprint --scheme bfv --tests quick8 --ring-size 8192
./run_benchmarks.py --kind corpus-memory --scheme bfv --tests 256 --ring-size 8192
./run_benchmarks.py --thread-scaling --kind thread-memory --scheme bfv --tests quick8 --ring-size 8192
./run_benchmarks.py --kind cpu --scheme bfv --tests quick8 --ring-size 8192
```

Current runner command:

```bash
./run_benchmarks.py --all --ring-size 8192
```

Run BGV exact instead of the default BFV exact:

```bash
./run_benchmarks.py --scheme bgv --all --ring-size 8192
```

Run CKKS approximate primitives:

```bash
./run_benchmarks.py --scheme ckks --all --ring-size 16384
```

Run CKKS ring-size scaling with an explicit shared config:

```bash
./run_benchmarks.py --scheme ckks --tests quick8 --ckks-config ring-sweep --ring-sizes 2048,4096,8192,16384,32768 --out-dir cpp/results/ckks_ring_sweep
```

Rows include `ckks_config=ring-sweep`, `ckks_depth`, `scale_bits`, `first_mod_bits`, and `security=not_set`. Keep corpus size below `ring_size / 2`; `quick8` and `normal256` are safe starting points for small-ring sweeps.

Sustained throughput runner commands:

```bash
./run_benchmarks.py --kind throughput --scheme bfv --tests 256,medium --ring-sizes 8192,16384 --duration-ms 5000 --out-dir cpp/results/throughput_bfv_common
./run_benchmarks.py --kind throughput --scheme bfv --tests full8192 --ring-size 8192 --duration-ms 5000 --out-dir cpp/results/throughput_bfv8192
./run_benchmarks.py --kind throughput --scheme bfv --tests full16384 --ring-size 16384 --duration-ms 5000 --out-dir cpp/results/throughput_bfv16384
./run_benchmarks.py --kind throughput --scheme ckks --tests 256,medium --ring-sizes 8192,16384 --ckks-config ring-sweep --duration-ms 5000 --out-dir cpp/results/throughput_ckks_common
./run_benchmarks.py --kind throughput --scheme ckks --tests full8192 --ring-size 8192 --ckks-config ring-sweep --duration-ms 5000 --out-dir cpp/results/throughput_ckks8192
./run_benchmarks.py --kind throughput --scheme ckks --tests full16384 --ring-size 16384 --ckks-config ring-sweep --duration-ms 5000 --out-dir cpp/results/throughput_ckks16384
```

Do not combine `--tests full8192,full16384` with `--ring-sizes 8192,16384` in
one command, because the runner intentionally forms every test/ring pair. Keep
the full-slot corpus paired with its matching ring. BFV `full8192` means 8192
active batching slots at ring 8192; CKKS `full8192` means 4096 active CKKS slots
at ring 8192 because CKKS slot capacity is `ring_size / 2`.
Use `256,medium` with `--ring-sizes 8192,16384` for same-corpus cross-ring
throughput comparisons, then run full-slot commands separately.

Rotation runner command:

```bash
./run_benchmarks.py --kind rotation --all --ring-size 8192
./run_benchmarks.py --kind rotation --scheme bgv --all --ring-size 8192
```

Serialization runner command:

```bash
./run_benchmarks.py --kind serialization --tests quick8,256,edge --ring-size 8192
./run_benchmarks.py --kind serialization --scheme bgv --tests quick8,256,edge --ring-size 8192
./run_benchmarks.py --kind serialization --scheme ckks --tests quick8,normal256 --ring-size 16384
```

Depth runner commands:

```bash
./run_benchmarks.py --kind depth --scheme bfv --tests quick8 --ring-size 8192 --max-depth 4
./run_benchmarks.py --kind depth --scheme bgv --tests quick8 --ring-size 8192 --max-depth 4
./run_benchmarks.py --kind depth --scheme ckks --tests quick8 --ring-size 16384 --max-depth 4
./run_benchmarks.py --kind depth --scheme ckks --tests 256,4096 --ring-sizes 8192,16384 --ckks-config ring-sweep --max-depth 4 --out-dir cpp/results/plan_ckks_depth
```

End-to-end dot-product workload commands:

```bash
./run_benchmarks.py --kind workload --scheme bfv --tests quick8 --ring-size 8192
./run_benchmarks.py --kind workload --scheme bgv --tests quick8 --ring-size 8192
./run_benchmarks.py --kind workload --scheme ckks --tests quick8 --ring-size 16384
```

End-to-end request/response commands:

```bash
./run_benchmarks.py --kind e2e --scheme bfv --tests 256,4096 --ring-sizes 8192,16384 --out-dir cpp/results/e2e_bfv
./run_benchmarks.py --kind e2e --scheme bgv --tests 256,4096 --ring-sizes 8192,16384 --out-dir cpp/results/e2e_bgv
./run_benchmarks.py --kind e2e --scheme ckks --tests normal256,normal4096 --ring-sizes 8192,16384 --ckks-config ring-sweep --out-dir cpp/results/e2e_ckks
./run_benchmarks.py --kind e2e --scheme ckks --tests normal8192 --ring-size 16384 --ckks-config ring-sweep --out-dir cpp/results/e2e_ckks_full16384
```

Memory runner commands:

```bash
./run_benchmarks.py --kind memory --scheme bfv --tests quick8 --ring-size 8192
./run_benchmarks.py --kind memory --scheme bgv --tests quick8 --ring-size 8192
./run_benchmarks.py --kind memory --scheme ckks --tests quick8 --ring-size 16384
```

It writes:

```text
cpp/results/seal.csv
cpp/results/openfhe.csv
cpp/results/openfhe_threads4.csv
cpp/results/openfhe_threads6.csv
```

The normal runner keeps report comparison to SEAL vs OpenFHE one-thread vs
OpenFHE four-thread vs OpenFHE six-thread. For OpenFHE-only scaling, add `--thread-scaling`; this
replaces the normal suite list with OpenFHE thread counts 1, 2, 4, 6, and 8:

```bash
./run_benchmarks.py --thread-scaling --kind workload --scheme bfv --tests quick8 --ring-size 8192
./run_benchmarks.py --thread-scaling --kind workload --scheme bgv --tests quick8 --ring-size 8192
./run_benchmarks.py --thread-scaling --kind workload --scheme ckks --tests quick8 --ring-size 16384
```

Thread-scaling output files are separate:

```text
cpp/results/openfhe_threads1_scaling_<kind>.csv
cpp/results/openfhe_threads2_scaling_<kind>.csv
cpp/results/openfhe_threads4_scaling_<kind>.csv
cpp/results/openfhe_threads6_scaling_<kind>.csv
cpp/results/openfhe_threads8_scaling_<kind>.csv
```

For exact BFV, the suffix is omitted, for example
`cpp/results/openfhe_threads1_scaling.csv`. Serialization does not support
`--thread-scaling`.

All report rows use the same format:

```text
started_unix=...,started_utc=...,test=quick8,threads=1,repeat=1,library=SEAL,scheme=BFV,operation=add,size=8,ring_size=8192,correct=true,latency_ms=...,ops_per_sec=...,values_per_sec=...
started_unix=...,started_utc=...,test=quick8,threads=4,repeat=1,library=OpenFHE,scheme=BFV,operation=add,size=8,ring_size=8192,correct=true,latency_ms=...,ops_per_sec=...,values_per_sec=...
started_unix=...,started_utc=...,test=quick8,threads=6,repeat=1,library=OpenFHE,scheme=BFV,operation=add,size=8,ring_size=8192,correct=true,latency_ms=...,ops_per_sec=...,values_per_sec=...
started_unix=...,started_utc=...,test=quick8,threads=1,repeat=1,library=SEAL,scheme=BGV,operation=add,size=8,ring_size=8192,correct=true,latency_ms=...,ops_per_sec=...,values_per_sec=...
started_unix=...,started_utc=...,test=quick8,threads=1,repeat=1,library=SEAL,scheme=CKKS,operation=add,size=8,ring_size=16384,correct=true,latency_ms=...,ops_per_sec=...,values_per_sec=...,mae=...,rmse=...,precision_bits=...
```

For current BFV/BGV exact primitive rows:

```text
ops_per_sec = 1000 / latency_ms
values_per_sec = ops_per_sec * active_slots
```

`active_slots` is the corpus row count shown as `size=...`.

## Setup Commands

```bash
./setup_venv.sh
./generate_corpus.sh
cmake -S cpp -B cpp/build -DCMAKE_BUILD_TYPE=Release
cmake --build cpp/build -j"$(nproc)"
```

## Criteria Status

| Report criterion | Specific test | Scheme scope | Current status | Command / evidence |
| --- | --- | --- | --- | --- |
| Encode | Encode vector to plaintext | BFV, BGV, CKKS | Implemented for BFV/BGV exact and CKKS approximate | Reports `operation=encode`, `latency_ms`, `ops_per_sec`, `values_per_sec`, `byte_size` where available |
| Decode | Decode plaintext to vector | BFV, BGV, CKKS | Implemented for BFV/BGV exact and CKKS approximate | Reports `operation=decode`, `latency_ms`, `ops_per_sec`, `values_per_sec` |
| Key generation | Generate secret key and public key | BFV, BGV, CKKS | Implemented for BFV/BGV exact and CKKS approximate | Reports `operation=keygen`, key sizes where available |
| Relinearization-key generation | Generate relinearization key | BFV, BGV, CKKS | Implemented for BFV/BGV exact and CKKS approximate | Reports `operation=relin_keygen`, `byte_size` where available |
| Rotation-key generation | Generate Galois/rotation keys for +1, -1, +8 | BFV, BGV, CKKS | Implemented for BFV/BGV rotation and CKKS approximate | BFV/BGV: `--kind rotation`; CKKS: `--scheme ckks` |
| Encryption | Encrypt plaintext to ciphertext | BFV, BGV, CKKS | Implemented for BFV/BGV exact and CKKS approximate | Reports `operation=encrypt`, `latency_ms`, `ops_per_sec`, `values_per_sec`, byte size where available |
| Decryption | Decrypt ciphertext to plaintext | BFV, BGV, CKKS | Implemented for BFV/BGV exact and CKKS approximate | Reports `operation=decrypt`, `latency_ms`, `ops_per_sec`, `values_per_sec` |
| Addition ct-ct | `Enc(a) + Enc(b)` | BFV, BGV, CKKS | Implemented | Use `--scheme bfv`, `--scheme bgv`, or `--scheme ckks` |
| Subtraction ct-ct | `Enc(a) - Enc(b)` | BFV, BGV, CKKS | Implemented | Use `--scheme bfv`, `--scheme bgv`, or `--scheme ckks` |
| Addition ct-pt | `Enc(a) + b` | BFV, BGV, CKKS | Implemented | Reports `operation=add_plain` |
| Subtraction ct-pt | `Enc(a) - b` | BFV, BGV, CKKS | Implemented | Reports `operation=sub_plain` |
| Multiplication ct-ct | `Enc(a) * Enc(b)` | BFV, BGV, CKKS | Implemented | CKKS also reports scale and level after rescale |
| Multiplication ct-pt | `Enc(a) * b` | BFV, BGV, CKKS | Implemented | Reports `operation=mul_plain` |
| Square | `Enc(a)^2` | BFV, BGV, CKKS | Implemented | Reports `operation=square_a` |
| Negate | `-Enc(a)` | BFV, BGV, CKKS | Implemented | Reports `operation=negate_a` |
| Relinearization | Multiply, measure size, relin, measure size | BFV, BGV, CKKS | Implemented | Reports `operation=relin`, components before/after, reduction ratio; SEAL reports serialized sizes |
| Rotate slots | Rotate packed vector +1, -1, +8 slots | BFV, BGV, CKKS | Implemented for BFV/BGV and CKKS | BFV/BGV: `--kind rotation`; CKKS: `--scheme ckks`; reports `rotate_1`, `rotate_-1`, `rotate_8` |
| Modulus switching | Drop ciphertext to one lower modulus level | BFV, BGV | Implemented for SEAL BFV/BGV and OpenFHE BGV; unsupported row for OpenFHE BFV | SEAL reports `operation=mod_switch`; OpenFHE BFV reports `supported=false`; OpenFHE BGV uses `ModReduce` |
| CKKS rescaling | Multiply, relin, rescale | CKKS | Implemented for CKKS approximate | Reports `operation=rescale`, scale before/after, level before/after, accuracy metrics |
| Exact correctness | Encrypt, compute, decrypt, validate | BFV, BGV | Implemented for BFV/BGV exact | `./run_benchmarks.py --all --ring-size 8192`; `./run_benchmarks.py --scheme bgv --all --ring-size 8192` |
| CKKS numerical accuracy | Encrypt, compute, decrypt, compare baseline | CKKS | Implemented for CKKS approximate primitives | Reports `mae`, `rmse`, `max_abs_error`, relative errors, `precision_bits`, and `pass_rate` |
| Multiplicative depth | `depth_1 = a * b`, then repeated squaring | BFV, BGV, CKKS | Implemented | Run `./run_benchmarks.py --kind depth --scheme bfv --tests quick8 --ring-size 8192 --max-depth 4`; also supports `--scheme bgv` and `--scheme ckks` |
| Serialization ciphertext | Serialize and deserialize ciphertext | BFV, BGV, CKKS | Implemented | Run `./run_benchmarks.py --kind serialization --scheme bfv --tests quick8,256,edge --ring-size 8192`; also supports `--scheme bgv` and `--scheme ckks`; reports `serialize_ciphertext`, `deserialize_ciphertext`, `byte_size`, `mb_per_sec` |
| Serialization keys | Serialize and deserialize keys | BFV, BGV, CKKS | Implemented | Reports secret/public key serialize+deserialize; relin/rotation key serialization rows; SEAL also reports relin/rotation key deserialize rows |
| End-to-end dot product | Encode/encrypt two vectors, multiply, rotate-sum, decrypt scalar | BFV, BGV, CKKS | Implemented | Run `./run_benchmarks.py --kind workload --scheme bfv --tests quick8 --ring-size 8192`; also supports `--scheme bgv` and `--scheme ckks`; reports `operation=dot_product_e2e` |
| End-to-end sum and plaintext-weighted dot | Encode -> encrypt -> serialize -> deserialize -> server evaluate -> serialize -> deserialize -> decrypt -> decode | BFV, BGV, CKKS | Implemented | Run `./run_benchmarks.py --kind e2e --scheme bfv --tests 256,4096 --ring-sizes 8192,16384`; reports `end_to_end_sum`, `end_to_end_dot_product_pt`, request/response bytes, server latency, peak RSS |
| Sustained packed throughput | Repeat prepared encrypt, ct-pt multiply, dot-product, and ciphertext serialization loops for a fixed interval | BFV, CKKS | Implemented | Run `./run_benchmarks.py --kind throughput --scheme bfv --tests full8192 --ring-size 8192`; reports completed operations, operations/s, packed values/s, active slots, slot utilization |
| Peak memory | Track peak RSS after major phases | BFV, BGV, CKKS | Implemented | Run `./run_benchmarks.py --kind memory --scheme bfv --tests quick8 --ring-size 8192`; also supports `--scheme bgv` and `--scheme ckks`; reports `peak_rss_kb` and `delta_peak_rss_kb` |
| SIMD packing efficiency | Vary used slot count | BFV, BGV, CKKS | Implemented for BFV/BGV exact and CKKS approximate | Compare `values_per_sec` across corpus sizes; CKKS 8192-row files require `--ring-size 16384` |
| Thread scaling | Run same workload with multiple thread counts | BFV, BGV, CKKS | Implemented as a separate OpenFHE-only suite for exact, rotation, depth, workload, and memory runners | Use `--thread-scaling`; compares OpenFHE `OMP_NUM_THREADS=1,2,4,6,8` outputs |
| Primitive NTT / INTT | Forward and inverse transform | Low-level kernel | Implemented | Run `./run_benchmarks.py --kind ntt --scheme lowlevel --tests quick8 --ring-size 8192`; SEAL uses `seal::util`; OpenFHE uses polynomial format switching |
| Primitive polynomial arithmetic | Low-level add, dyadic multiply, shift / DCRT operations | Low-level kernel | Implemented | Run `./run_benchmarks.py --kind poly --scheme lowlevel --tests quick8 --ring-size 8192` |
| Key switching | Switch ciphertext to compatible key representation | BFV, BGV, CKKS | Implemented for OpenFHE; SEAL emits explicit unsupported rows | Run `./run_benchmarks.py --kind keyswitch --scheme bfv --tests quick8 --ring-size 8192`; also supports BGV/CKKS |
| CKKS matrix multiplication | 64 x 64 matrix multiplication | CKKS | Implemented as encrypted-row by plaintext-column CKKS workload | Run `./run_benchmarks.py --kind matrix --scheme ckks --all --ring-size 16384`; supports `--ckks-config ring-sweep` and reports CKKS config fields |
| Heap allocation tracing per primitive | Count C++ heap allocations around resource phases | BFV, BGV | Implemented | Run `./run_benchmarks.py --kind heap --scheme bfv --tests quick8 --ring-size 8192`; reports `allocations`, `allocated_bytes`, `peak_live_bytes` |
| Persistent object memory footprint | Hold context, keys, plaintexts, ciphertexts and report footprint | BFV, BGV | Implemented | Run `./run_benchmarks.py --kind footprint --scheme bfv --tests quick8 --ring-size 8192`; reports object rows and peak RSS |
| Corpus memory scaling | Hold many ciphertexts in RAM | BFV, BGV | Implemented | Run `./run_benchmarks.py --kind corpus-memory --scheme bfv --tests 256 --ring-size 8192`; `held_ciphertexts` follows corpus row count |
| Thread memory scaling | Hold ciphertexts while varying OpenFHE thread count | BFV, BGV | Implemented | Run `./run_benchmarks.py --thread-scaling --kind thread-memory --scheme bfv --tests quick8 --ring-size 8192` |
| CPU utilization rows | Report process CPU time vs wall time | BFV, BGV | Implemented | Run `./run_benchmarks.py --kind cpu --scheme bfv --tests quick8 --ring-size 8192`; reports `cpu_ms` and `cpu_utilization_pct` |

## Current Acceptance Checks

Run quick BFV exact correctness:

```bash
./run_benchmarks.py --tests quick8 --ring-size 8192
grep -R "correct=false" cpp/results
```

Pass criteria:

```text
No correct=false rows.
Rows include ops_per_sec and values_per_sec.
```

Run signed edge-case BFV exact correctness:

```bash
./run_benchmarks.py --tests edge --ring-size 8192
grep -R "correct=false" cpp/results
```

Run quick BGV exact correctness:

```bash
./run_benchmarks.py --scheme bgv --tests quick8 --ring-size 8192
grep -R "correct=false" cpp/results
```

Run quick CKKS approximate correctness:

```bash
./run_benchmarks.py --scheme ckks --tests quick8 --ring-size 16384
grep -R "correct=false" cpp/results
```

Run quick CKKS ring-sweep correctness:

```bash
./run_benchmarks.py --scheme ckks --tests quick8 --ckks-config ring-sweep --ring-sizes 2048,4096,8192,16384,32768 --out-dir cpp/results/ckks_ring_sweep
grep -R "correct=false" cpp/results/ckks_ring_sweep
```

Run broad CKKS approximate primitive coverage for generated CKKS files up to 4096 rows:

```bash
./run_benchmarks.py --scheme ckks --all --ring-size 16384 --out-dir cpp/results/ckks_16384
grep -R "correct=false" cpp/results/ckks_16384
```

Run the 8192-row CKKS corpora explicitly:

```bash
./run_benchmarks.py --scheme ckks --tests normal8192,small8192,nearzero8192,mixed8192 --ring-size 16384 --out-dir cpp/results/ckks_16384
```

Pass criteria:

```text
No correct=false rows.
Rows include ops_per_sec and values_per_sec.
```

Run multiplicative depth tests:

```bash
./run_benchmarks.py --kind depth --scheme bfv --tests quick8 --ring-size 8192 --out-dir cpp/results/depth_bfv_quick
./run_benchmarks.py --kind depth --scheme bgv --tests quick8 --ring-size 8192 --out-dir cpp/results/depth_bgv_quick
./run_benchmarks.py --kind depth --scheme ckks --tests quick8 --ring-size 16384 --out-dir cpp/results/depth_ckks_quick
```

Depth interpretation:

```text
Rows use operation=depth_mul and include depth, max_depth, level fields, and
scheme-specific accuracy/noise fields. A later correct=false row records the
first failed depth for that parameter set; it is not a runner crash.
OpenFHE exact BFV/BGV depth rows print `security=not_set` because depth sweeps
must test the requested ring/depth pair instead of letting OpenFHE silently
upgrade BFV `8192`/depth-4 to `16384`.
For CKKS ring-size comparisons, use `--ckks-config ring-sweep`; depth runners
still honor `--max-depth` as the tested chain length and print
`security=not_set` on each row.
```

Run main current comparison:

```bash
./run_benchmarks.py --all --ring-size 8192
grep -R "correct=false" cpp/results
```

Pass criteria:

```text
cpp/results/seal.csv exists.
cpp/results/openfhe.csv exists.
cpp/results/openfhe_threads6.csv exists.
No correct=false rows.
Rows include ops_per_sec and values_per_sec.
```

Run separate OpenFHE thread scaling:

```bash
./run_benchmarks.py --thread-scaling --kind workload --scheme bfv --tests quick8 --ring-size 8192 --out-dir cpp/results/thread_scaling_bfv
./run_benchmarks.py --thread-scaling --kind workload --scheme bgv --tests quick8 --ring-size 8192 --out-dir cpp/results/thread_scaling_bgv
./run_benchmarks.py --thread-scaling --kind workload --scheme ckks --tests quick8 --ring-size 16384 --out-dir cpp/results/thread_scaling_ckks
```

Thread-scaling pass criteria:

```text
OpenFHE-only files exist for threads 1, 2, 4, 6, and 8.
Rows include threads=..., ops_per_sec, and values_per_sec.
Compare speedup against the threads=1 OpenFHE row for the same scheme/test.
```

If six-thread rows are dramatically slower than one-thread rows, rerun the same
test with warmups and repetitions before drawing conclusions:

```bash
./run_benchmarks.py --thread-scaling --scheme bfv --tests 8192 --ring-size 8192 --warmups 1 --repetitions 5 --out-dir cpp/results/thread_scaling_bfv8192_repeat
```

Then compare per-operation medians across `repeat=1..5`, not only one row.

Run end-to-end dot-product workload:

```bash
./run_benchmarks.py --kind workload --scheme bfv --all --ring-size 8192 --out-dir cpp/results/workload_bfv
./run_benchmarks.py --kind workload --scheme bgv --all --ring-size 8192 --out-dir cpp/results/workload_bgv
./run_benchmarks.py --kind workload --scheme ckks --all --ring-size 16384 --out-dir cpp/results/workload_ckks
```

Dot-product pass criteria:

```text
No correct=false rows.
Rows include operation=dot_product_e2e, rotations_count, expected, actual,
ops_per_sec, and values_per_sec.
CKKS rows also include abs_error, relative_error, and precision_bits.
```

Run memory benchmarks:

```bash
./run_benchmarks.py --kind memory --scheme bfv --tests quick8 --ring-size 8192 --out-dir cpp/results/memory_bfv
./run_benchmarks.py --kind memory --scheme bgv --tests quick8 --ring-size 8192 --out-dir cpp/results/memory_bgv
./run_benchmarks.py --kind memory --scheme ckks --tests quick8 --ring-size 16384 --out-dir cpp/results/memory_ckks
```

Memory interpretation:

```text
Rows use operation=memory_* and report peak_rss_kb plus delta_peak_rss_kb.
Peak RSS is process-level and monotonic, so later rows show the largest memory
seen so far in that process, not isolated per-operation allocation.
```

Relinearization is included in the exact BFV runner. There is no separate
`--kind relin` command. Run exact BFV, then inspect `operation=relin` rows:

```bash
./run_benchmarks.py --all --ring-size 8192 --out-dir cpp/results/bfv_exact8192
grep -R "correct=false" cpp/results/bfv_exact8192
grep -R "operation=relin" cpp/results/bfv_exact8192
```

Relin pass criteria:

```text
No correct=false rows.
Relin rows show components_before=3 and components_after=2.
SEAL rows also show size_before_bytes, size_after_bytes, and reduction_ratio.
```

SEAL BFV modulus switching is also included in the exact BFV runner:

```bash
./run_benchmarks.py --all --ring-size 8192 --out-dir cpp/results/bfv_exact8192
grep -R "operation=mod_switch" cpp/results/bfv_exact8192
```

Mod-switch interpretation:

```text
SEAL rows show supported=true, level_before, level_after, and levels_dropped.
OpenFHE BFV rows show supported=false because OpenFHE's public ModReduce /
LevelReduce APIs are documented for BGV/CKKS rather than BFV.
```

Run BFV/BGV rotation correctness:

```bash
./run_benchmarks.py --kind rotation --all --ring-size 8192 --out-dir cpp/results/bfv_rotation8192
./run_benchmarks.py --kind rotation --scheme bgv --all --ring-size 8192 --out-dir cpp/results/bgv_rotation8192
grep -R "correct=false" cpp/results/bfv_rotation8192
grep -R "correct=false" cpp/results/bgv_rotation8192
```

Pass criteria:

```text
No correct=false rows.
Rows include rotation_keygen, rotate_1, rotate_-1, and rotate_8.
```

Run serialization for all implemented schemes:

```bash
./run_benchmarks.py --kind serialization --scheme bfv --tests quick8,256,edge --ring-size 8192 --out-dir cpp/results/serialization_bfv
./run_benchmarks.py --kind serialization --scheme bgv --tests quick8,256,edge --ring-size 8192 --out-dir cpp/results/serialization_bgv
./run_benchmarks.py --kind serialization --scheme ckks --tests quick8,normal256 --ring-size 16384 --out-dir cpp/results/serialization_ckks
```

Serialization pass criteria:

```text
No correct=false rows.
Rows include byte_size and mb_per_sec.
CKKS ciphertext rows include scale and level.
```

## Debug Commands

List current test names and corpus files:

```bash
./run_benchmarks.py --list-tests
```

Print the process commands without running them:

```bash
./run_benchmarks.py --tests quick8 --ring-size 8192 --dry-run
```

Run only SEAL while debugging local code:

```bash
./run_benchmarks.py --tests quick8 --only seal --ring-size 8192
./run_benchmarks.py --kind rotation --tests quick8 --only seal --ring-size 8192
./run_benchmarks.py --kind depth --scheme ckks --tests quick8 --only seal --ring-size 8192
```

Run one executable directly:

```bash
./cpp/build/seal_bfv_exact --corpus he_corpus/exact/exact_safe_000008.csv --ring-size 8192
./cpp/build/openfhe_bfv_exact --corpus he_corpus/exact/exact_safe_000008.csv --ring-size 8192
./cpp/build/seal_bgv_exact --corpus he_corpus/exact/exact_safe_000008.csv --ring-size 8192
./cpp/build/openfhe_bgv_exact --corpus he_corpus/exact/exact_safe_000008.csv --ring-size 8192
./cpp/build/seal_ckks --corpus he_corpus/ckks_normal/ckks_normal_000008.csv --ring-size 8192
./cpp/build/openfhe_ckks --corpus he_corpus/ckks_normal/ckks_normal_000008.csv --ring-size 16384
./cpp/build/seal_ckks --corpus he_corpus/ckks_normal/ckks_normal_000008.csv --ring-size 2048 --ckks-config ring-sweep
./cpp/build/openfhe_ckks --corpus he_corpus/ckks_normal/ckks_normal_000008.csv --ring-size 2048 --ckks-config ring-sweep
./cpp/build/seal_bfv_depth --corpus he_corpus/depth/exact_depth_000008.csv --ring-size 8192 --max-depth 4
./cpp/build/openfhe_ckks_depth --corpus he_corpus/depth/ckks_depth_000008.csv --ring-size 16384 --max-depth 4
./cpp/build/seal_bgv_serialization --corpus he_corpus/exact/exact_safe_000008.csv --ring-size 8192
./cpp/build/openfhe_ckks_serialization --corpus he_corpus/ckks_normal/ckks_normal_000008.csv --ring-size 16384
./cpp/build/seal_bfv_dot --corpus he_corpus/exact/exact_safe_000008.csv --ring-size 8192
./cpp/build/openfhe_ckks_dot --corpus he_corpus/ckks_normal/ckks_normal_000008.csv --ring-size 16384
./cpp/build/seal_bfv_memory --corpus he_corpus/exact/exact_safe_000008.csv --ring-size 8192
./cpp/build/openfhe_ckks_memory --corpus he_corpus/ckks_normal/ckks_normal_000008.csv --ring-size 16384
```
