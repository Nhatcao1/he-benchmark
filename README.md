# HE Benchmark Corpus

This folder contains a deterministic corpus generator for Microsoft SEAL and OpenFHE benchmark harnesses.

## Files

- `generate_he_corpus.py`: copied generator script.
- `requirements.txt`: Python dependencies.
- `setup_venv.sh`: creates `.venv` and installs dependencies.
- `generate_corpus.sh`: runs the generator from the local virtual environment.
- `he_corpus/`: expected output directory for generated CSV files and `manifest.json`.

## Server Setup

From this directory:

```bash
chmod +x setup_venv.sh generate_corpus.sh
./setup_venv.sh
./generate_corpus.sh
```

The generator writes output to:

```text
he_corpus/
```

Recommended first test files after generation:

- `he_corpus/exact/exact_safe_000008.csv`
- `he_corpus/rotation/rotation_000008.csv`
- `he_corpus/ckks_normal/ckks_normal_000008.csv`
- `he_corpus/depth/exact_depth_000256.csv`
- `he_corpus/depth/ckks_depth_000256.csv`

## C++ Test Suite

The current C++ benchmark targets are:

- `seal_bfv_exact`
- `openfhe_bfv_exact`

They validate BFV batched exact arithmetic against the generated exact corpus:

- add
- subtract
- multiply
- square
- negate

The benchmarks read signed integer reference values from `he_corpus/exact/*.csv`, decrypt each result, and compare slots with centered modulo normalization. Timing is printed per operation, but these targets are mainly correctness baselines before larger timing runs.

### Build Dependencies

By default the CMake build expects:

- Microsoft SEAL cloned at `../SEAL` relative to this repo's parent directory.
- OpenFHE installed as a CMake package when `HE_BENCHMARK_BUILD_OPENFHE=ON`.

Example OpenFHE install from source:

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

### Build and Run

```bash
cmake -S cpp -B cpp/build \
  -DCMAKE_BUILD_TYPE=Release \
  -DHE_BENCHMARK_BUILD_OPENFHE=ON
cmake --build cpp/build --target seal_bfv_exact openfhe_bfv_exact -j"$(nproc)"

./cpp/build/seal_bfv_exact he_corpus/exact/exact_safe_000008.csv
./cpp/build/openfhe_bfv_exact he_corpus/exact/exact_safe_000008.csv
```

If OpenFHE is installed somewhere other than the default prefix, configure this repo with:

```bash
cmake -S cpp -B cpp/build \
  -DCMAKE_BUILD_TYPE=Release \
  -DHE_BENCHMARK_BUILD_OPENFHE=ON \
  -DCMAKE_PREFIX_PATH=/path/to/openfhe/install
```
