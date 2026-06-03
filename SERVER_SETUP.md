# Server Setup and Run Commands

Run these commands on the remote Linux server.

## 1. Install System Packages

```bash
sudo apt update
sudo apt install -y git cmake build-essential python3 python3-venv python3-pip
```

## 2. Prepare Source Layout

The benchmark repo expects Microsoft SEAL beside it:

```text
~
├── SEAL
└── seal-openfhe-benchmark
```

If you already cloned `seal-openfhe-benchmark`, only clone SEAL:

```bash
cd ~
git clone https://github.com/microsoft/SEAL.git SEAL
```

If you have not cloned the benchmark repo yet:

```bash
cd ~
git clone <YOUR_REPO_URL> seal-openfhe-benchmark
git clone https://github.com/microsoft/SEAL.git SEAL
```

## 3. Build and Install OpenFHE

```bash
cd ~
git clone https://github.com/openfheorg/openfhe-development.git OpenFHE

cmake -S OpenFHE -B OpenFHE/build \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_EXAMPLES=OFF \
  -DBUILD_BENCHMARKS=OFF \
  -DBUILD_UNITTESTS=OFF \
  -DBUILD_EXTRAS=OFF

cmake --build OpenFHE/build -j"$(nproc)"
sudo cmake --install OpenFHE/build
```

If `sudo cmake --install` installs to `/usr/local`, your benchmark CMake should find OpenFHE automatically.

## 4. Generate Benchmark Corpus

```bash
cd ~/seal-openfhe-benchmark

chmod +x setup_venv.sh generate_corpus.sh
./setup_venv.sh
./generate_corpus.sh
```

This creates or refreshes test CSV files under:

```text
he_corpus/
```

## 5. Configure Benchmark Build

```bash
cd ~/seal-openfhe-benchmark

cmake -S cpp -B cpp/build \
  -DCMAKE_BUILD_TYPE=Release \
  -DHE_BENCHMARK_BUILD_OPENFHE=ON
```

If OpenFHE is installed in a custom location, use:

```bash
cmake -S cpp -B cpp/build \
  -DCMAKE_BUILD_TYPE=Release \
  -DHE_BENCHMARK_BUILD_OPENFHE=ON \
  -DCMAKE_PREFIX_PATH=/path/to/openfhe/install
```

## 6. Build SEAL and OpenFHE Benchmarks

```bash
cmake --build cpp/build --target seal_bfv_exact openfhe_bfv_exact -j"$(nproc)"
```

## 7. Run Smoke Tests

Small correctness test:

```bash
./cpp/build/seal_bfv_exact he_corpus/exact/exact_safe_000008.csv
./cpp/build/openfhe_bfv_exact he_corpus/exact/exact_safe_000008.csv
```

Edge cases:

```bash
./cpp/build/seal_bfv_exact he_corpus/exact/exact_edge_cases.csv
./cpp/build/openfhe_bfv_exact he_corpus/exact/exact_edge_cases.csv
```

Larger packed vector:

```bash
./cpp/build/seal_bfv_exact he_corpus/exact/exact_safe_004096.csv
./cpp/build/openfhe_bfv_exact he_corpus/exact/exact_safe_004096.csv
```

Expected output is one CSV-style line per operation, for example:

```text
library=SEAL,scheme=BFV,operation=add,size=8,correct=true,latency_ms=...
library=OpenFHE,scheme=BFV,operation=add,size=8,correct=true,latency_ms=...
```

## 8. If Configure Cannot Find OpenFHE

Check where OpenFHE installed its CMake files:

```bash
sudo find /usr/local -name 'OpenFHEConfig.cmake'
```

Then configure with the install prefix above the `lib/cmake/OpenFHE` folder. Example:

```bash
cmake -S cpp -B cpp/build \
  -DCMAKE_BUILD_TYPE=Release \
  -DHE_BENCHMARK_BUILD_OPENFHE=ON \
  -DCMAKE_PREFIX_PATH=/usr/local
```

## 9. Clean Rebuild

Use this if CMake cache gets confused after changing SEAL/OpenFHE paths:

```bash
rm -rf cpp/build
cmake -S cpp -B cpp/build \
  -DCMAKE_BUILD_TYPE=Release \
  -DHE_BENCHMARK_BUILD_OPENFHE=ON
cmake --build cpp/build --target seal_bfv_exact openfhe_bfv_exact -j"$(nproc)"
```
