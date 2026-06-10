# SEAL/OpenFHE Test Plan

This plan tracks the reduced benchmark scope for BFV and CKKS. BGV can be run
later by mirroring BFV commands with `--scheme bgv`, but it is not part of this
run plan.

## Corpus Meaning

- BFV uses `he_corpus/exact/exact_safe_*.csv`.
- BFV rotation uses `he_corpus/rotation/rotation_*.csv`.
- CKKS uses `he_corpus/ckks_normal/ckks_normal_*.csv`.
- `256` means 256 active packed slots.
- `medium` means 4096 active packed slots.
- `full` means the largest valid corpus for the selected ring:
  - BFV primitive full: `8192`.
  - BFV throughput full: `full8192` at ring `8192`, `full16384` at ring
    `16384`.
  - BFV end-to-end full: `4096` at ring `8192`, `8192` at ring `16384`,
    because the rotate-sum e2e path is limited to one batching row.
  - CKKS full: `normal4096` at ring `8192`, `normal8192` at ring `16384`,
    because CKKS slots are `ring_size / 2`.
  - CKKS throughput full: `full8192` uses `ckks_normal_004096.csv`, and
    `full16384` uses `ckks_normal_008192.csv`.

## Thread Decision

- Normal comparison commands run SEAL, OpenFHE one thread, OpenFHE four threads,
  and OpenFHE six threads.
- Serialization intentionally skips OpenFHE four-thread and six-thread rows.
- The deployment concurrency rows in the original table are deferred. Current
  runner thread options measure OpenFHE internal OpenMP thread count, not
  independent concurrent client requests.

## Criteria

| Group | Test ID | Scheme | Ring dimensions | Corpus size / active slots | Input file | What Codex must time | Primary output metrics | Correctness |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Client preparation | `encode` | BFV | 8192, 16384 | 256, medium, full | `exact_*.csv` | Encode one populated vector into plaintext | latency, vectors/s, values/s | Decode and compare exactly modulo plaintext modulus |
| Client preparation | `encode` | CKKS | 8192, 16384 | 256, medium, full | `ckks_normal_*.csv` | Encode one populated vector into plaintext | latency, vectors/s, values/s | Decode and report MAE, RMSE, max error |
| Client recovery | `decode` | BFV | 8192, 16384 | 256, medium, full | `exact_*.csv` | Decode one plaintext vector | latency, vectors/s, values/s | Exact equality |
| Client recovery | `decode` | CKKS | 8192, 16384 | 256, medium, full | `ckks_normal_*.csv` | Decode one plaintext vector | latency, vectors/s, values/s | MAE, RMSE, max error |
| Request creation | `encrypt` | BFV, CKKS | 8192, 16384 | 256, medium, full | Scheme-specific corpus | Encrypt one encoded vector | latency, ciphertexts/s, packed values/s, ciphertext bytes | Decrypt round trip |
| Result recovery | `decrypt` | BFV, CKKS | 8192, 16384 | 256, medium, full | Ciphertext generated from input | Decrypt one ciphertext | latency, ciphertexts/s, packed values/s | Exact equality or CKKS error |
| Aggregate primitive | `add_ct_ct` | BFV, CKKS | 8192, 16384 | 256, medium, full | Columns `a`, `b` | Add two ciphertexts | latency, operations/s, packed values/s | Compare with `a + b` |
| Weighted-query primitive | `mul_ct_pt` | BFV, CKKS | 8192, 16384 | 256, medium, full | Columns `a`, `b` | Multiply encrypted `a` by plaintext `b` | latency, operations/s, packed values/s | Compare with `a * b` |
| Advanced arithmetic | `mul_ct_ct_relin` | BFV | 8192, 16384 | 256, medium, full | Columns `a`, `b` | Multiply ciphertexts and relinearize | total latency, multiply latency, relin latency, output bytes | Compare with `a * b` |
| Advanced arithmetic | `mul_ct_ct_relin_rescale` | CKKS | 8192, 16384 | 256, medium, full | Columns `a`, `b` | Multiply, relinearize, and rescale | total latency, multiply latency, relin latency, rescale latency, remaining levels, output bytes | MAE, RMSE, max error |
| Key switching | `key_switch_keygen`, `key_switch_apply` | BFV, CKKS | BFV: 8192, 16384. CKKS combined: 16384. CKKS 8192: SEAL-only limitation row | 256, medium, full | Scheme-specific corpus | Generate a key-switch key and apply key switching from old secret key to new secret key | keygen latency, apply latency, operations/s, values/s, `supported` | OpenFHE decrypts with the new key; SEAL records `supported=false` because SEAL has relin/Galois switching but no generic public key-to-key switch API |
| Packed reduction primitive | `rotate_1` | BFV, CKKS | 8192, 16384 | 256, medium, full | BFV: `rotation_*.csv`; CKKS: `ckks_normal_*.csv` | Rotate by one slot | latency, operations/s, packed values/s | Verify direction and values |
| Packed reduction primitive | `rotate_8` | BFV, CKKS | 8192, 16384 | 256, medium, full | BFV: `rotation_*.csv`; CKKS: `ckks_normal_*.csv` | Rotate by eight slots | latency, operations/s, packed values/s | Verify direction and values |
| Data exchange overhead | `serialize_ciphertext` | BFV, CKKS | 8192, 16384 | 256, medium, full | Ciphertext generated from corpus | Serialize and deserialize ciphertext | serialize latency, deserialize latency, bytes, MB/s | Decrypt after round trip |
| Sustained throughput | `throughput_encrypt` | BFV, CKKS | 8192, 16384 | 256, medium, full | Scheme-specific corpus | Encrypt prepared plaintext vectors continuously for `--duration-ms` | completed operations, operations/s, packed values/s, active slots | BFV exact modulo check; CKKS MAE, RMSE, max error |
| Sustained throughput | `throughput_mul_ct_pt` | BFV, CKKS | 8192, 16384 | 256, medium, full | Scheme-specific corpus | Multiply one prepared ciphertext by plaintext weights continuously for `--duration-ms` | completed operations, operations/s, packed values/s, active slots | Compare with `a * b` |
| Sustained throughput | `throughput_dot_product_pt` | BFV, CKKS | 8192, 16384 | 256, medium, full | Scheme-specific corpus | Run encrypted plaintext-weighted dot-product requests continuously for `--duration-ms` | completed requests, requests/s, input values/s | Compare sampled scalar result |
| Sustained throughput | `throughput_serialize_ciphertext` | BFV, CKKS | 8192, 16384 | 256, medium, full | Ciphertext generated from corpus | Serialize one prepared ciphertext continuously for `--duration-ms` | total serialized bytes, objects/s, MB/s | Deserialize sampled output and decrypt |
| Setup cost | `keygen` | BFV, CKKS | 8192, 16384 | N/A | No corpus | Generate secret and public keys | latency, serialized secret-key bytes, serialized public-key bytes | Keys remain usable |
| Setup cost | `relin_keygen` | BFV, CKKS | 8192, 16384 | N/A | No corpus | Generate multiplication evaluation key | latency, serialized bytes | Relin succeeds |
| Setup cost | `rotation_keygen` | BFV, CKKS | 8192, 16384 | N/A | No corpus | Generate required rotation keys | latency, serialized bytes | Rotations succeed |
| Remote aggregate query | `end_to_end_sum` | BFV, CKKS | 8192, 16384 | medium, full | Scheme-specific corpus | Encode -> encrypt -> serialize -> deserialize -> rotate-and-add -> serialize -> deserialize -> decrypt -> decode | total latency, server evaluation latency, requests/s, request bytes, response bytes, peak RSS | Compare final sum |
| Protected scoring | `end_to_end_dot_product_pt` | BFV, CKKS | 8192, 16384 | medium, full | Input vector and plaintext weights | Encode -> encrypt -> serialize -> deserialize -> multiply plaintext weights -> rotate-and-add -> serialize -> deserialize -> decrypt -> decode | total latency, server evaluation latency, requests/s, request bytes, response bytes, peak RSS | Compare scalar score |
| Deployment concurrency | `thread_scaling_sum` | BFV, CKKS | 8192 | representative medium corpus | Same corpus for both libraries | Deferred | requests/s, p50, p95, CPU usage, peak RSS, speedup, efficiency | Same result across threads |
| Deployment concurrency | `thread_scaling_dot_product_pt` | BFV, CKKS | 8192 | representative medium corpus | Same corpus for both libraries | Deferred | requests/s, p50, p95, CPU usage, peak RSS, speedup, efficiency | Same result across threads |

## Build

```bash
git pull
cmake -S cpp -B cpp/build -DCMAKE_BUILD_TYPE=Release
cmake --build cpp/build -j"$(nproc)"
```

## Run Commands

BFV primitives, setup rows, relin, ct-pt multiply, encode/decode/encrypt/decrypt:

```bash
./run_benchmarks.py --scheme bfv --tests 256,4096,8192 --ring-sizes 8192,16384 --out-dir cpp/results/plan_bfv_primitives
grep -R "correct=false" cpp/results/plan_bfv_primitives
```

CKKS primitives, setup rows, relin/rescale, ct-pt multiply, encode/decode/encrypt/decrypt:

```bash
./run_benchmarks.py --scheme ckks --tests normal256,normal4096 --ring-sizes 8192,16384 --ckks-config ring-sweep --out-dir cpp/results/plan_ckks_primitives
grep -R "correct=false" cpp/results/plan_ckks_primitives

./run_benchmarks.py --scheme ckks --tests normal8192 --ring-size 16384 --ckks-config ring-sweep --out-dir cpp/results/plan_ckks_primitives_full16384
grep -R "correct=false" cpp/results/plan_ckks_primitives_full16384
```

BFV rotations:

```bash
./run_benchmarks.py --kind rotation --scheme bfv --tests 256,4096,8192 --ring-sizes 8192,16384 --out-dir cpp/results/plan_bfv_rotation
grep -R "correct=false" cpp/results/plan_bfv_rotation
```

CKKS rotations:

```bash
./run_benchmarks.py --kind rotation --scheme ckks --tests normal256,normal4096 --ring-sizes 8192,16384 --ckks-config ring-sweep --out-dir cpp/results/plan_ckks_rotation
grep -R "correct=false" cpp/results/plan_ckks_rotation

./run_benchmarks.py --kind rotation --scheme ckks --tests normal8192 --ring-size 16384 --ckks-config ring-sweep --out-dir cpp/results/plan_ckks_rotation_full16384
grep -R "correct=false" cpp/results/plan_ckks_rotation_full16384
```

Serialization:

```bash
./run_benchmarks.py --kind serialization --scheme bfv --tests 256,4096,8192 --ring-sizes 8192,16384 --out-dir cpp/results/plan_bfv_serialization
grep -R "correct=false" cpp/results/plan_bfv_serialization

./run_benchmarks.py --kind serialization --scheme ckks --tests normal256,normal4096 --ring-sizes 8192,16384 --ckks-config ring-sweep --out-dir cpp/results/plan_ckks_serialization
grep -R "correct=false" cpp/results/plan_ckks_serialization

./run_benchmarks.py --kind serialization --scheme ckks --tests normal8192 --ring-size 16384 --ckks-config ring-sweep --out-dir cpp/results/plan_ckks_serialization_full16384
grep -R "correct=false" cpp/results/plan_ckks_serialization_full16384
```

Sustained throughput:

```bash
./run_benchmarks.py --kind throughput --scheme bfv --tests 256,medium --ring-sizes 8192,16384 --duration-ms 5000 --out-dir cpp/results/plan_bfv_throughput_common
grep -R "correct=false" cpp/results/plan_bfv_throughput_common

./run_benchmarks.py --kind throughput --scheme bfv --tests full8192 --ring-size 8192 --duration-ms 5000 --out-dir cpp/results/plan_bfv_throughput_full8192
grep -R "correct=false" cpp/results/plan_bfv_throughput_full8192

./run_benchmarks.py --kind throughput --scheme bfv --tests full16384 --ring-size 16384 --duration-ms 5000 --out-dir cpp/results/plan_bfv_throughput_full16384
grep -R "correct=false" cpp/results/plan_bfv_throughput_full16384

./run_benchmarks.py --kind throughput --scheme ckks --tests 256,medium --ring-sizes 8192,16384 --ckks-config ring-sweep --duration-ms 5000 --out-dir cpp/results/plan_ckks_throughput_common
grep -R "correct=false" cpp/results/plan_ckks_throughput_common

./run_benchmarks.py --kind throughput --scheme ckks --tests full8192 --ring-size 8192 --ckks-config ring-sweep --duration-ms 5000 --out-dir cpp/results/plan_ckks_throughput_full8192
grep -R "correct=false" cpp/results/plan_ckks_throughput_full8192

./run_benchmarks.py --kind throughput --scheme ckks --tests full16384 --ring-size 16384 --ckks-config ring-sweep --duration-ms 5000 --out-dir cpp/results/plan_ckks_throughput_full16384
grep -R "correct=false" cpp/results/plan_ckks_throughput_full16384
```

Key switching:

```bash
./run_benchmarks.py --kind keyswitch --scheme bfv --tests 256,4096,8192 --ring-sizes 8192,16384 --out-dir cpp/results/plan_bfv_keyswitch
grep -R "correct=false" cpp/results/plan_bfv_keyswitch

./run_benchmarks.py --kind keyswitch --scheme ckks --tests normal256,normal4096,normal8192 --ring-size 16384 --out-dir cpp/results/plan_ckks_keyswitch16384
grep -R "correct=false" cpp/results/plan_ckks_keyswitch16384

./run_benchmarks.py --kind keyswitch --scheme ckks --tests normal256,normal4096 --ring-size 8192 --only seal --out-dir cpp/results/plan_ckks_keyswitch8192_seal_limit
grep -R "correct=false" cpp/results/plan_ckks_keyswitch8192_seal_limit
```

End-to-end sum and plaintext-weighted dot product:

```bash
./run_benchmarks.py --kind e2e --scheme bfv --tests 4096 --ring-size 8192 --out-dir cpp/results/plan_bfv_e2e_medium8192
grep -R "correct=false" cpp/results/plan_bfv_e2e_medium8192

./run_benchmarks.py --kind e2e --scheme bfv --tests 8192 --ring-size 16384 --out-dir cpp/results/plan_bfv_e2e_full16384
grep -R "correct=false" cpp/results/plan_bfv_e2e_full16384

./run_benchmarks.py --kind e2e --scheme ckks --tests normal4096 --ring-size 8192 --ckks-config ring-sweep --out-dir cpp/results/plan_ckks_e2e_medium8192
grep -R "correct=false" cpp/results/plan_ckks_e2e_medium8192

./run_benchmarks.py --kind e2e --scheme ckks --tests normal8192 --ring-size 16384 --ckks-config ring-sweep --out-dir cpp/results/plan_ckks_e2e_full16384
grep -R "correct=false" cpp/results/plan_ckks_e2e_full16384
```

Optional OpenFHE-only internal thread scaling for the current e2e runner:

```bash
./run_benchmarks.py --thread-scaling --kind e2e --scheme bfv --tests 4096 --ring-size 8192 --out-dir cpp/results/plan_thread_scaling_bfv_e2e8192
grep -R "correct=false" cpp/results/plan_thread_scaling_bfv_e2e8192

./run_benchmarks.py --thread-scaling --kind e2e --scheme ckks --tests normal4096 --ring-size 8192 --ckks-config ring-sweep --out-dir cpp/results/plan_thread_scaling_ckks_e2e8192
grep -R "correct=false" cpp/results/plan_thread_scaling_ckks_e2e8192
```
