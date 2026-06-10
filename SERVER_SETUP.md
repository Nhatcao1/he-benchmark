# Server Setup

Assume this source layout on the server:

```text
~
├── SEAL
├── OpenFHE
└── seal-openfhe-benchmark
```

## 1. Install Packages

```bash
sudo apt update
sudo apt install -y git cmake build-essential python3 python3-venv python3-pip
```

## 2. Clone Sources

```bash
cd ~
git clone https://github.com/microsoft/SEAL.git SEAL
git clone https://github.com/openfheorg/openfhe-development.git OpenFHE
git clone <YOUR_REPO_URL> seal-openfhe-benchmark
```

## 3. Build and Install OpenFHE

```bash
cd ~
cmake -S OpenFHE -B OpenFHE/build \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_EXAMPLES=OFF \
  -DBUILD_BENCHMARKS=OFF \
  -DBUILD_UNITTESTS=OFF \
  -DBUILD_EXTRAS=OFF

cmake --build OpenFHE/build -j"$(nproc)"
sudo cmake --install OpenFHE/build
```

SEAL does not need a separate install. The benchmark CMake build uses the
sibling `~/SEAL` source tree directly.

## 4. Generate Corpus

```bash
cd ~/seal-openfhe-benchmark
chmod +x setup_venv.sh generate_corpus.sh run_benchmarks.py
./setup_venv.sh
./generate_corpus.sh
```

This creates deterministic test CSV files under `he_corpus/`.

## 5. Build Benchmarks

```bash
cd ~/seal-openfhe-benchmark
cmake -S cpp -B cpp/build \
  -DCMAKE_BUILD_TYPE=Release

cmake --build cpp/build -j"$(nproc)"
```

OpenFHE benchmark targets are enabled by default. If reusing an old `cpp/build`
directory that cached OpenFHE as disabled, rerun configure with
`-DHE_BENCHMARK_BUILD_OPENFHE=ON`.

## 6. Run Benchmarks

Run every exact BFV corpus test for both libraries using ring size 8192:

```bash
./run_benchmarks.py --all --ring-size 8192
```

Run every exact BGV corpus test for both libraries using ring size 8192:

```bash
./run_benchmarks.py --scheme bgv --all --ring-size 8192
```

Run CKKS approximate primitive tests for both libraries using ring size 16384.
OpenFHE CKKS rejects ring size 8192 under its current HE-standard security
check:

```bash
./run_benchmarks.py --scheme ckks --all --ring-size 16384
```

Run CKKS ring-size scaling with the explicit shared config:

```bash
./run_benchmarks.py --scheme ckks --tests quick8 --ckks-config ring-sweep --ring-sizes 2048,4096,8192,16384,32768 --out-dir cpp/results/ckks_ring_sweep
```

This mode is for comparing how latency changes with `ring_size`; it sets low-depth CKKS params consistently in SEAL/OpenFHE and prints the selected config fields on each row.

Run the 8192-row CKKS corpora explicitly:

```bash
./run_benchmarks.py --scheme ckks --tests normal8192,small8192,nearzero8192,mixed8192 --ring-size 16384
```

Run BFV/BGV rotation tests (ring size 8192 required):

```bash
./run_benchmarks.py --kind rotation --all --ring-size 8192
./run_benchmarks.py --kind rotation --scheme bgv --all --ring-size 8192
```

Run BFV serialization tests:

```bash
./run_benchmarks.py --kind serialization --tests quick8,256,edge --ring-size 8192
```

Run BGV and CKKS serialization tests:

```bash
./run_benchmarks.py --kind serialization --scheme bgv --tests quick8,256,edge --ring-size 8192
./run_benchmarks.py --kind serialization --scheme ckks --tests quick8,normal256 --ring-size 16384
```

Run multiplicative-depth tests:

```bash
./run_benchmarks.py --kind depth --scheme bfv --tests quick8 --ring-size 8192 --max-depth 4
./run_benchmarks.py --kind depth --scheme bgv --tests quick8 --ring-size 8192 --max-depth 4
./run_benchmarks.py --kind depth --scheme ckks --tests quick8 --ring-size 16384 --max-depth 4
./run_benchmarks.py --kind depth --scheme ckks --tests 256,4096 --ring-sizes 8192,16384 --ckks-config ring-sweep --max-depth 4 --out-dir cpp/results/plan_ckks_depth
```

Depth runs report the first failed level if the parameter set runs out of
noise/levels. For this benchmark group, a later `correct=false` row is a
measured saturation point, not necessarily a setup failure.
After CKKS depth code changes, rebuild before rerunning server benchmarks:

```bash
cmake --build cpp/build --target seal_ckks_depth openfhe_ckks_depth -j"$(nproc)"
```

Run end-to-end dot-product workloads:

```bash
./run_benchmarks.py --kind workload --scheme bfv --tests quick8 --ring-size 8192
./run_benchmarks.py --kind workload --scheme bgv --tests quick8 --ring-size 8192
./run_benchmarks.py --kind workload --scheme ckks --tests quick8 --ring-size 16384
```

Run request/response end-to-end workloads:

```bash
./run_benchmarks.py --kind e2e --scheme bfv --tests 256,4096 --ring-sizes 8192,16384 --out-dir cpp/results/e2e_bfv
./run_benchmarks.py --kind e2e --scheme bgv --tests 256,4096 --ring-sizes 8192,16384 --out-dir cpp/results/e2e_bgv
./run_benchmarks.py --kind e2e --scheme ckks --tests normal256,normal4096 --ring-sizes 8192,16384 --ckks-config ring-sweep --out-dir cpp/results/e2e_ckks
./run_benchmarks.py --kind e2e --scheme ckks --tests normal8192 --ring-size 16384 --ckks-config ring-sweep --out-dir cpp/results/e2e_ckks_full16384
```

Run sustained throughput benchmarks:

```bash
./run_benchmarks.py --kind throughput --scheme bfv --tests 256,medium --ring-sizes 8192,16384 --duration-ms 5000 --out-dir cpp/results/throughput_bfv_common
./run_benchmarks.py --kind throughput --scheme bfv --tests full8192 --ring-size 8192 --duration-ms 5000 --out-dir cpp/results/throughput_bfv8192
./run_benchmarks.py --kind throughput --scheme bfv --tests full16384 --ring-size 16384 --duration-ms 5000 --out-dir cpp/results/throughput_bfv16384
./run_benchmarks.py --kind throughput --scheme ckks --tests 256,medium --ring-sizes 8192,16384 --ckks-config ring-sweep --duration-ms 5000 --out-dir cpp/results/throughput_ckks_common
./run_benchmarks.py --kind throughput --scheme ckks --tests full8192 --ring-size 8192 --ckks-config ring-sweep --duration-ms 5000 --out-dir cpp/results/throughput_ckks8192
./run_benchmarks.py --kind throughput --scheme ckks --tests full16384 --ring-size 16384 --ckks-config ring-sweep --duration-ms 5000 --out-dir cpp/results/throughput_ckks16384
```

Keep `full8192` paired with `--ring-size 8192` and `full16384` paired with
`--ring-size 16384`. The runner forms every test/ring pair, so do not combine
both full corpus names with `--ring-sizes 8192,16384` in a single command.

Run memory benchmarks:

```bash
./run_benchmarks.py --kind memory --scheme bfv --tests quick8 --ring-size 8192
./run_benchmarks.py --kind memory --scheme bgv --tests quick8 --ring-size 8192
./run_benchmarks.py --kind memory --scheme ckks --tests quick8 --ring-size 16384
```

Run separate OpenFHE thread-scaling benchmarks:

```bash
./run_benchmarks.py --thread-scaling --kind workload --scheme bfv --tests quick8 --ring-size 8192 --out-dir cpp/results/thread_scaling_bfv
./run_benchmarks.py --thread-scaling --kind workload --scheme bgv --tests quick8 --ring-size 8192 --out-dir cpp/results/thread_scaling_bgv
./run_benchmarks.py --thread-scaling --kind workload --scheme ckks --tests quick8 --ring-size 16384 --out-dir cpp/results/thread_scaling_ckks
```

When investigating suspicious thread scaling, run repeated full-process
measurements:

```bash
./run_benchmarks.py --thread-scaling --scheme bfv --tests 8192 --ring-size 8192 --warmups 1 --repetitions 5 --out-dir cpp/results/thread_scaling_bfv8192_repeat
```

OpenFHE child processes are launched with `OMP_NUM_THREADS`, `OMP_DYNAMIC=FALSE`,
`OMP_PROC_BIND=close`, and `OMP_PLACES=cores`. Compare medians across
`repeat=1..5` rather than relying on a single recorded row.

Run advanced benchmark groups:

```bash
./run_benchmarks.py --kind ntt --scheme lowlevel --tests quick8 --ring-size 8192 --out-dir cpp/results/ntt
./run_benchmarks.py --kind poly --scheme lowlevel --tests quick8 --ring-size 8192 --out-dir cpp/results/poly
./run_benchmarks.py --kind keyswitch --scheme bfv --tests quick8 --ring-size 8192 --out-dir cpp/results/keyswitch_bfv
./run_benchmarks.py --kind keyswitch --scheme bgv --tests quick8 --ring-size 8192 --out-dir cpp/results/keyswitch_bgv
./run_benchmarks.py --kind keyswitch --scheme ckks --tests quick8 --ring-size 16384 --out-dir cpp/results/keyswitch_ckks
./run_benchmarks.py --kind matrix --scheme ckks --all --ring-size 16384 --out-dir cpp/results/matrix_ckks
./run_benchmarks.py --kind heap --scheme bfv --tests quick8 --ring-size 8192 --out-dir cpp/results/heap_bfv
./run_benchmarks.py --kind footprint --scheme bfv --tests quick8 --ring-size 8192 --out-dir cpp/results/footprint_bfv
./run_benchmarks.py --kind corpus-memory --scheme bfv --tests 256 --ring-size 8192 --out-dir cpp/results/corpus_memory_bfv
./run_benchmarks.py --thread-scaling --kind thread-memory --scheme bfv --tests quick8 --ring-size 8192 --out-dir cpp/results/thread_memory_bfv
./run_benchmarks.py --kind cpu --scheme bfv --tests quick8 --ring-size 8192 --out-dir cpp/results/cpu_bfv
```

SEAL key-switch rows intentionally report `supported=false` because SEAL does
not expose generic public key-to-key switching. OpenFHE key-switch rows use
OpenFHE's public `KeySwitchGen` and `KeySwitch` APIs.

The default comparison remains SEAL vs OpenFHE one-thread vs OpenFHE
four-thread vs OpenFHE six-thread. `--thread-scaling` is OpenFHE-only and writes files for
`OMP_NUM_THREADS=1,2,4,6,8`. Use it for scaling plots instead of mixing many
OpenFHE thread counts into the normal SEAL/OpenFHE comparison.

This writes three separate reports:

```text
cpp/results/seal.csv
cpp/results/openfhe.csv
cpp/results/openfhe_threads4.csv
cpp/results/openfhe_threads6.csv
```

`seal.csv` is the SEAL baseline. `openfhe.csv` is OpenFHE with one OpenMP
thread. `openfhe_threads4.csv` and `openfhe_threads6.csv` are the same OpenFHE
benchmark with `OMP_NUM_THREADS=4` and `OMP_NUM_THREADS=6`.

Thread-scaling report files use names such as:

```text
cpp/results/thread_scaling_bfv/openfhe_threads1_scaling_workload.csv
cpp/results/thread_scaling_bfv/openfhe_threads2_scaling_workload.csv
cpp/results/thread_scaling_bfv/openfhe_threads4_scaling_workload.csv
cpp/results/thread_scaling_bfv/openfhe_threads6_scaling_workload.csv
cpp/results/thread_scaling_bfv/openfhe_threads8_scaling_workload.csv
```

Each report uses the same CSV-style row format. The runner adds `test=...`,
`threads=...`, and `repeat=...` so the files can be compared directly.

Run a smaller subset while debugging:

```bash
./run_benchmarks.py --tests quick8,edge --ring-size 8192
```

Run only one debug suite:

```bash
./run_benchmarks.py --tests 4096 --only seal --ring-size 8192
```

List supported test names:

```bash
./run_benchmarks.py --list-tests
```

## Direct Binary Debugging

The runner resolves test names to exact corpus files. Use direct binary commands
only when debugging a specific executable:

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
./cpp/build/openfhe_bgv_depth --corpus he_corpus/depth/exact_depth_000008.csv --ring-size 8192 --max-depth 4
./cpp/build/seal_ckks_depth --corpus he_corpus/depth/ckks_depth_000008.csv --ring-size 8192 --max-depth 4
./cpp/build/openfhe_ckks_depth --corpus he_corpus/depth/ckks_depth_000008.csv --ring-size 16384 --max-depth 4
./cpp/build/seal_bgv_serialization --corpus he_corpus/exact/exact_safe_000008.csv --ring-size 8192
./cpp/build/openfhe_bgv_serialization --corpus he_corpus/exact/exact_safe_000008.csv --ring-size 8192
./cpp/build/seal_ckks_serialization --corpus he_corpus/ckks_normal/ckks_normal_000008.csv --ring-size 8192
./cpp/build/openfhe_ckks_serialization --corpus he_corpus/ckks_normal/ckks_normal_000008.csv --ring-size 16384
./cpp/build/seal_bfv_dot --corpus he_corpus/exact/exact_safe_000008.csv --ring-size 8192
./cpp/build/openfhe_bgv_dot --corpus he_corpus/exact/exact_safe_000008.csv --ring-size 8192
./cpp/build/seal_ckks_dot --corpus he_corpus/ckks_normal/ckks_normal_000008.csv --ring-size 8192
./cpp/build/openfhe_ckks_dot --corpus he_corpus/ckks_normal/ckks_normal_000008.csv --ring-size 16384
./cpp/build/seal_bfv_memory --corpus he_corpus/exact/exact_safe_000008.csv --ring-size 8192
./cpp/build/openfhe_bgv_memory --corpus he_corpus/exact/exact_safe_000008.csv --ring-size 8192
./cpp/build/seal_ckks_memory --corpus he_corpus/ckks_normal/ckks_normal_000008.csv --ring-size 8192
./cpp/build/openfhe_ckks_memory --corpus he_corpus/ckks_normal/ckks_normal_000008.csv --ring-size 16384
./cpp/build/seal_lowlevel_ntt --corpus he_corpus/exact/exact_safe_000008.csv --ring-size 8192
./cpp/build/openfhe_lowlevel_ntt --corpus he_corpus/exact/exact_safe_000008.csv --ring-size 8192
./cpp/build/seal_bfv_keyswitch --corpus he_corpus/exact/exact_safe_000008.csv --ring-size 8192
./cpp/build/openfhe_bfv_keyswitch --corpus he_corpus/exact/exact_safe_000008.csv --ring-size 8192
./cpp/build/seal_ckks_matrix --corpus he_corpus/matrices/matrix_a_0064x0064.csv --ring-size 8192
./cpp/build/openfhe_ckks_matrix --corpus he_corpus/matrices/matrix_a_0064x0064.csv --ring-size 16384
./cpp/build/seal_bfv_cpu --corpus he_corpus/exact/exact_safe_000008.csv --ring-size 8192
./cpp/build/openfhe_bfv_cpu --corpus he_corpus/exact/exact_safe_000008.csv --ring-size 8192
```

Each executable prints one CSV-style line per operation:

```text
library=SEAL,scheme=BFV,operation=add,size=8,ring_size=8192,correct=true,latency_ms=...,ops_per_sec=...,values_per_sec=...
library=OpenFHE,scheme=BFV,operation=add,size=8,ring_size=8192,correct=true,latency_ms=...,ops_per_sec=...,values_per_sec=...
library=SEAL,scheme=BGV,operation=add,size=8,ring_size=8192,correct=true,latency_ms=...,ops_per_sec=...,values_per_sec=...
library=OpenFHE,scheme=BGV,operation=add,size=8,ring_size=8192,correct=true,latency_ms=...,ops_per_sec=...,values_per_sec=...
library=SEAL,scheme=CKKS,operation=add,size=8,ring_size=16384,correct=true,latency_ms=...,ops_per_sec=...,values_per_sec=...,mae=...,rmse=...,precision_bits=...
library=OpenFHE,scheme=CKKS,operation=add,size=8,ring_size=16384,correct=true,latency_ms=...,ops_per_sec=...,values_per_sec=...,mae=...,rmse=...,precision_bits=...
library=SEAL,scheme=BFV,operation=depth_mul,size=8,ring_size=8192,correct=true,latency_ms=...,ops_per_sec=...,values_per_sec=...,depth=1,max_depth=4,noise_budget_after_bits=...
library=OpenFHE,scheme=CKKS,operation=depth_mul,size=8,ring_size=16384,correct=true,latency_ms=...,ops_per_sec=...,values_per_sec=...,depth=1,max_depth=4,mae=...,precision_bits=...
library=SEAL,scheme=CKKS,operation=serialize_ciphertext,size=8,ring_size=16384,correct=true,latency_ms=...,byte_size=...,mb_per_sec=...,scale=...,level=...
library=SEAL,scheme=BFV,operation=dot_product_e2e,size=8,ring_size=8192,correct=true,latency_ms=...,ops_per_sec=...,values_per_sec=...,rotations_count=...,expected=...,actual=...
library=SEAL,scheme=BFV,operation=memory_keygen,size=8,ring_size=8192,correct=true,latency_ms=...,peak_rss_kb=...,delta_peak_rss_kb=...
library=SEAL,scheme=LOWLEVEL,operation=ntt_forward,size=8192,ring_size=8192,correct=true,latency_ms=...,ops_per_sec=...,values_per_sec=...
library=OpenFHE,scheme=BFV,operation=key_switch_apply,size=8,ring_size=8192,correct=true,latency_ms=...,ops_per_sec=...,values_per_sec=...
library=SEAL,scheme=CKKS,operation=matrix_multiply_64x64_ctpt,size=4096,ring_size=16384,correct=true,latency_ms=...,max_abs_error=...,rmse=...
library=SEAL,scheme=BFV,operation=resource_encrypt,size=8,ring_size=8192,correct=true,latency_ms=...,resource_mode=cpu,cpu_ms=...,cpu_utilization_pct=...
```
