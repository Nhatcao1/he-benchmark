# Run Commands

Run these from the repository root:

```bash
./setup_venv.sh
```

Sets up the local Python virtual environment and installs generator dependencies.
Use this before generating the corpus on a fresh machine or shell.

```bash
./generate_corpus.sh
```

Regenerates deterministic CSV test corpora under `he_corpus/`. This checks the
Python generator path and refreshes exact, CKKS, rotation, depth, and matrix
inputs.

```bash
cmake -S cpp -B cpp/build
```

Configures the C++ benchmark build. By default this expects Microsoft SEAL at
`../SEAL` relative to this repo's parent directory. OpenFHE targets are opt-in.

If OpenFHE is not installed yet:

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
cd ~/seal-openfhe-benchmark
```

If OpenFHE was installed to a custom prefix, configure this repo with:

```bash
cmake -S cpp -B cpp/build \
  -DCMAKE_BUILD_TYPE=Release \
  -DHE_BENCHMARK_BUILD_OPENFHE=ON \
  -DCMAKE_PREFIX_PATH=/path/to/openfhe/install
```

To build OpenFHE targets with the default OpenFHE install prefix:

```bash
cmake -S cpp -B cpp/build \
  -DCMAKE_BUILD_TYPE=Release \
  -DHE_BENCHMARK_BUILD_OPENFHE=ON
```

```bash
cmake --build cpp/build --target seal_bfv_exact
```

Builds the current SEAL BFV exact arithmetic benchmark executable.

```bash
cmake --build cpp/build --target openfhe_bfv_exact
```

Builds the current OpenFHE BFV exact arithmetic benchmark executable.

```bash
./cpp/build/seal_bfv_exact he_corpus/exact/exact_safe_000008.csv
```

Smoke-tests BFV exact add, subtract, multiply, square, and negate on 8 packed
slots. This is the fastest correctness check after C++ changes.

```bash
./cpp/build/openfhe_bfv_exact he_corpus/exact/exact_safe_000008.csv
```

Runs the same exact arithmetic smoke test through OpenFHE.

```bash
./cpp/build/seal_bfv_exact he_corpus/exact/exact_edge_cases.csv
./cpp/build/openfhe_bfv_exact he_corpus/exact/exact_edge_cases.csv
```

Runs the same BFV exact operations on small signed edge cases. Use this after
changes to centered modular encoding or comparison logic.

```bash
./cpp/build/seal_bfv_exact he_corpus/exact/exact_safe_004096.csv
./cpp/build/openfhe_bfv_exact he_corpus/exact/exact_safe_004096.csv
```

Runs the BFV exact operations on a larger packed vector. Use this when checking
SIMD slot handling and benchmark output at a more realistic size.
