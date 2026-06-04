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
  -DCMAKE_BUILD_TYPE=Release \
  -DHE_BENCHMARK_BUILD_OPENFHE=ON

cmake --build cpp/build --target seal_bfv_exact seal_bfv_rotation openfhe_bfv_exact openfhe_bfv_rotation -j"$(nproc)"
```

## 6. Run Benchmarks

Run every exact BFV corpus test for both libraries using ring size 8192:

```bash
./run_benchmarks.py --all --ring-size 8192
```

Run BFV rotation tests (ring size 8192 required):

```bash
./run_benchmarks.py --kind rotation --all --ring-size 8192
```

This writes three separate reports:

```text
cpp/results/seal.csv
cpp/results/openfhe.csv
cpp/results/openfhe_threads6.csv
```

`seal.csv` is the SEAL baseline. `openfhe.csv` is OpenFHE with one OpenMP
thread. `openfhe_threads6.csv` is the same OpenFHE benchmark with
`OMP_NUM_THREADS=6`.

Each report uses the same CSV-style row format. The runner adds `test=...` and
`threads=...` so the files can be compared directly.

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
```

Each executable prints one CSV-style line per operation:

```text
library=SEAL,scheme=BFV,operation=add,size=8,ring_size=8192,correct=true,latency_ms=...,ops_per_sec=...,values_per_sec=...
library=OpenFHE,scheme=BFV,operation=add,size=8,ring_size=8192,correct=true,latency_ms=...,ops_per_sec=...,values_per_sec=...
```
