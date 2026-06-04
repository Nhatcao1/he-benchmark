# Benchmark Criteria Commands

Run commands from the repository root.

This file is organized by the report criteria. Current implemented scope is
only BFV exact primitive benchmarking:

```text
SEAL baseline
OpenFHE with OMP_NUM_THREADS=1
OpenFHE with OMP_NUM_THREADS=6
```

Current runner command:

```bash
./run_benchmarks.py --all --ring-size 8192
```

Rotation runner command:

```bash
./run_benchmarks.py --kind rotation --all --ring-size 8192
```

It writes:

```text
cpp/results/seal.csv
cpp/results/openfhe.csv
cpp/results/openfhe_threads6.csv
```

All report rows use the same format:

```text
started_unix=...,started_utc=...,test=quick8,threads=1,library=SEAL,scheme=BFV,operation=add,size=8,ring_size=8192,correct=true,latency_ms=...,ops_per_sec=...,values_per_sec=...
started_unix=...,started_utc=...,test=quick8,threads=6,library=OpenFHE,scheme=BFV,operation=add,size=8,ring_size=8192,correct=true,latency_ms=...,ops_per_sec=...,values_per_sec=...
```

For current BFV exact primitive rows:

```text
ops_per_sec = 1000 / latency_ms
values_per_sec = ops_per_sec * active_slots
```

`active_slots` is the corpus row count shown as `size=...`.

## Setup Commands

```bash
./setup_venv.sh
./generate_corpus.sh
cmake -S cpp -B cpp/build -DCMAKE_BUILD_TYPE=Release -DHE_BENCHMARK_BUILD_OPENFHE=ON
cmake --build cpp/build --target seal_bfv_exact openfhe_bfv_exact
```

## Criteria Status

| Report criterion | Specific test | Scheme scope | Current status | Command / evidence |
| --- | --- | --- | --- | --- |
| Encode | Encode vector to plaintext | BFV, BGV, CKKS | Implemented for BFV exact | Reports `operation=encode`, `latency_ms`, `ops_per_sec`, `values_per_sec`, `byte_size` |
| Decode | Decode plaintext to vector | BFV, BGV, CKKS | Implemented for BFV exact | Reports `operation=decode`, `latency_ms`, `ops_per_sec`, `values_per_sec` |
| Key generation | Generate secret key and public key | BFV, BGV, CKKS | Implemented for BFV exact | Reports `operation=keygen`, `secret_key_bytes`, `public_key_bytes` |
| Relinearization-key generation | Generate relinearization key | BFV, BGV, CKKS | Implemented for BFV exact | Reports `operation=relin_keygen`, `byte_size` |
| Rotation-key generation | Generate Galois/rotation keys for +1, -1, +8 | BFV, BGV, CKKS | Implemented for BFV | Reports `operation=rotation_keygen`; run `./run_benchmarks.py --kind rotation --all --ring-size 8192` |
| Encryption | Encrypt plaintext to ciphertext | BFV, BGV, CKKS | Implemented for BFV exact | Reports `operation=encrypt`, `latency_ms`, `ops_per_sec`, `values_per_sec`, `byte_size` |
| Decryption | Decrypt ciphertext to plaintext | BFV, BGV, CKKS | Implemented for BFV exact | Reports `operation=decrypt`, `latency_ms`, `ops_per_sec`, `values_per_sec` |
| Addition ct-ct | `Enc(a) + Enc(b)` | BFV, BGV, CKKS | Implemented for BFV | Reports `latency_ms`, `ops_per_sec`, `values_per_sec`; run `./run_benchmarks.py --tests quick8 --ring-size 8192` |
| Subtraction ct-ct | `Enc(a) - Enc(b)` | BFV, BGV, CKKS | Implemented for BFV | Reports `latency_ms`, `ops_per_sec`, `values_per_sec`; run `./run_benchmarks.py --tests quick8 --ring-size 8192` |
| Addition ct-pt | `Enc(a) + b` | BFV, BGV, CKKS | Implemented for BFV exact | Reports `operation=add_plain`, `latency_ms`, `ops_per_sec`, `values_per_sec`, `byte_size` |
| Subtraction ct-pt | `Enc(a) - b` | BFV, BGV, CKKS | Implemented for BFV exact | Reports `operation=sub_plain`, `latency_ms`, `ops_per_sec`, `values_per_sec`, `byte_size` |
| Multiplication ct-ct | `Enc(a) * Enc(b)` | BFV, BGV, CKKS | Implemented for BFV | Reports `latency_ms`, `ops_per_sec`, `values_per_sec`; run `./run_benchmarks.py --tests quick8 --ring-size 8192` |
| Multiplication ct-pt | `Enc(a) * b` | BFV, BGV, CKKS | Implemented for BFV exact | Reports `operation=mul_plain`, `latency_ms`, `ops_per_sec`, `values_per_sec`, `byte_size` |
| Square | `Enc(a)^2` | BFV, BGV, CKKS | Implemented for BFV | Reports `latency_ms`, `ops_per_sec`, `values_per_sec`; run `./run_benchmarks.py --tests quick8 --ring-size 8192` |
| Negate | `-Enc(a)` | BFV, BGV, CKKS | Implemented for BFV | Reports `latency_ms`, `ops_per_sec`, `values_per_sec`; run `./run_benchmarks.py --tests quick8 --ring-size 8192` |
| Relinearization | Multiply, measure size, relin, measure size | BFV, BGV, CKKS | Partial for BFV | BFV multiply/square call relin, but size-before/after is not reported yet |
| Rotate slots | Rotate packed vector +1, -1, +8 slots | BFV, BGV, CKKS | Implemented for BFV | Run `./run_benchmarks.py --kind rotation --all --ring-size 8192`; reports `rotate_1`, `rotate_-1`, `rotate_8` |
| Modulus switching | Drop ciphertext to one lower modulus level | BFV, BGV | Planned | No modulus-switch runner yet |
| CKKS rescaling | Multiply, relin, rescale | CKKS | Planned | CKKS runner not implemented yet |
| Exact correctness | Encrypt, compute, decrypt, validate | BFV, BGV | Implemented for BFV | `./run_benchmarks.py --all --ring-size 8192` |
| CKKS numerical accuracy | Encrypt, compute, decrypt, compare baseline | CKKS | Planned | CKKS corpus exists, no runner yet |
| Multiplicative depth | Multiplication chain depth 1 to 4 | BFV, BGV, CKKS | Planned | Depth corpus exists, no runner yet |
| Serialization ciphertext | Serialize and deserialize ciphertext | BFV, BGV, CKKS | Planned | No serialization runner yet |
| Serialization keys | Serialize and deserialize keys | BFV, BGV, CKKS | Planned | No serialization runner yet |
| SIMD packing efficiency | Vary used slot count | BFV, BGV, CKKS | Implemented for BFV exact | Compare `values_per_sec` across quick8, 256, 4096, 8192; run `./run_benchmarks.py --all --ring-size 8192` |
| Thread scaling | Run same workload with multiple thread counts | BFV, BGV, CKKS | Partial for OpenFHE BFV | Compare `ops_per_sec` and `values_per_sec` in `openfhe.csv` vs `openfhe_threads6.csv` |
| Primitive NTT / INTT | NTT forward, INTT inverse | Low-level kernel | Planned | No low-level kernel runner yet |
| Primitive polynomial arithmetic | Native AddEq, Native MulEq, DCRT MulEq | Low-level kernel | Planned | No low-level kernel runner yet |
| Key switching | Switch ciphertext to compatible key representation | BFV, BGV, CKKS | Planned | No key-switch runner yet |
| CKKS matrix multiplication | Packed matrix multiplication 64 x 64 | CKKS | Planned | Matrix corpus exists, no runner yet |

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

Pass criteria:

```text
No correct=false rows.
Rows include ops_per_sec and values_per_sec.
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

Run BFV rotation correctness:

```bash
./run_benchmarks.py --kind rotation --all --ring-size 8192 --out-dir cpp/results/bfv_rotation8192
grep -R "correct=false" cpp/results/bfv_rotation8192
```

Pass criteria:

```text
No correct=false rows.
Rows include rotation_keygen, rotate_1, rotate_-1, and rotate_8.
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
```

Run one executable directly:

```bash
./cpp/build/seal_bfv_exact --corpus he_corpus/exact/exact_safe_000008.csv --ring-size 8192
./cpp/build/openfhe_bfv_exact --corpus he_corpus/exact/exact_safe_000008.csv --ring-size 8192
```
