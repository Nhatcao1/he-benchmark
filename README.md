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
- `seal_bgv_exact`
- `openfhe_bgv_exact`
- `seal_ckks`
- `openfhe_ckks`
- `seal_bfv_depth`
- `openfhe_bfv_depth`
- `seal_bgv_depth`
- `openfhe_bgv_depth`
- `seal_ckks_depth`
- `openfhe_ckks_depth`
- `seal_bgv_serialization`
- `openfhe_bgv_serialization`
- `seal_ckks_serialization`
- `openfhe_ckks_serialization`
- `seal_bfv_dot`
- `openfhe_bfv_dot`
- `seal_bgv_dot`
- `openfhe_bgv_dot`
- `seal_ckks_dot`
- `openfhe_ckks_dot`
- `seal_bfv_memory`
- `openfhe_bfv_memory`
- `seal_bgv_memory`
- `openfhe_bgv_memory`
- `seal_ckks_memory`
- `openfhe_ckks_memory`
- `seal_lowlevel_ntt`
- `openfhe_lowlevel_ntt`
- `seal_lowlevel_poly`
- `openfhe_lowlevel_poly`
- `seal_bfv_keyswitch`
- `openfhe_bfv_keyswitch`
- `seal_bgv_keyswitch`
- `openfhe_bgv_keyswitch`
- `seal_ckks_keyswitch`
- `openfhe_ckks_keyswitch`
- `seal_ckks_matrix`
- `openfhe_ckks_matrix`
- BFV/BGV resource targets for heap, footprint, corpus-memory, thread-memory,
  and CPU rows.

The BFV/BGV targets validate batched exact arithmetic against the generated exact corpus:

- add
- subtract
- multiply
- add plaintext
- subtract plaintext
- multiply plaintext
- square
- negate
- relinearization
- modulus switching

The benchmarks read signed integer reference values from `he_corpus/exact/*.csv`, decrypt each result, and compare slots with centered modulo normalization. Timing is printed per operation, but these targets are mainly correctness baselines before larger timing runs.

The CKKS targets validate approximate packed arithmetic against generated CKKS corpora:

- `ckks_normal`
- `ckks_small`
- `ckks_near_zero`
- `ckks_mixed_scale`

CKKS rows report `mae`, `rmse`, `max_abs_error`, relative-error metrics, `precision_bits`, `pass_rate`, scale, and level fields. CKKS primitives currently include encode, decode, key generation, relinearization-key generation, rotation-key generation, encrypt, decrypt, add, subtract, multiply, add/subtract/multiply plaintext, square, negate, relinearization, rotations, and rescale.

For CKKS ring-size scaling experiments, use the explicit `ring-sweep` config. It applies the same low-depth CKKS profile to SEAL and OpenFHE, relaxes the security check so small rings can run, and prints `ckks_config`, `ckks_depth`, `scale_bits`, `first_mod_bits`, and `security` on each row:

```bash
./run_benchmarks.py --scheme ckks --tests quick8 --ckks-config ring-sweep --ring-sizes 2048,4096,8192,16384,32768
```


Depth targets use `he_corpus/depth/*.csv` and report one `operation=depth_mul`
row per sequential multiplication level. BFV/BGV depth rows validate exact
centered-modulo results; CKKS depth rows validate approximate results with the
same error metrics as the CKKS primitive runner. A later `correct=false` row in
a depth run can be the measured saturation point for that parameter set.

Serialization targets cover BFV, BGV, and CKKS. They report save/load latency,
serialized byte size, and MB/s for ciphertexts, secret keys, public keys,
relinearization keys, and rotation keys. CKKS ciphertext serialization rows also
include scale and level fields.

Rotation targets cover BFV and BGV packed exact vectors. They report
`rotation_keygen`, `rotate_1`, `rotate_-1`, and `rotate_8` rows with correctness
status under each library's packing semantics.

The workload targets implement one end-to-end encrypted dot product. They time
encode, encrypt, encrypted slotwise multiply, relinearization, rotation-based
accumulation, decrypt, and decode as `operation=dot_product_e2e`. BFV/BGV rows
check exact scalar correctness; CKKS rows report absolute and relative error.

The `--kind e2e` targets implement request/response workflows for
`end_to_end_sum` and `end_to_end_dot_product_pt`. They include ciphertext
serialization/deserialization around the simulated client/server boundary and
report request bytes, response bytes, total latency, server evaluation latency,
and peak RSS.

Memory targets report process peak RSS after each major phase: context setup,
key generation, relinearization-key generation, rotation-key generation, encode,
encrypt, multiply, rotate, and decrypt. Rows include `peak_rss_kb` and
`delta_peak_rss_kb`.

The Python runner's normal comparison writes SEAL, OpenFHE one-thread,
OpenFHE four-thread, and OpenFHE six-thread reports. OpenFHE-only thread scaling is intentionally a
separate mode:

```bash
./run_benchmarks.py --thread-scaling --kind workload --scheme bfv --tests quick8 --ring-size 8192
```

That mode writes OpenFHE reports for `OMP_NUM_THREADS=1,2,4,6,8`, leaving the
normal SEAL/OpenFHE comparison files unchanged.

Advanced runners cover low-level NTT/INTT, low-level polynomial arithmetic,
explicit OpenFHE key switching, CKKS 64x64 matrix multiplication, heap
allocation tracing, persistent object footprint, corpus memory scaling, thread
memory scaling, and CPU utilization rows. SEAL generic key switching is emitted
as `supported=false` because SEAL exposes relinearization and Galois-key
switching, not a generic public key-to-key switch API.

### Build Dependencies

By default the CMake build expects:

- Microsoft SEAL cloned at `../SEAL` relative to this repo's parent directory.
- OpenFHE installed as a CMake package. Fresh CMake builds enable OpenFHE
  benchmark targets by default.

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
  -DCMAKE_BUILD_TYPE=Release
cmake --build cpp/build -j"$(nproc)"

./cpp/build/seal_bfv_exact he_corpus/exact/exact_safe_000008.csv
./cpp/build/openfhe_bfv_exact he_corpus/exact/exact_safe_000008.csv
./cpp/build/seal_bgv_exact he_corpus/exact/exact_safe_000008.csv
./cpp/build/openfhe_bgv_exact he_corpus/exact/exact_safe_000008.csv
./cpp/build/seal_ckks --corpus he_corpus/ckks_normal/ckks_normal_000008.csv --ring-size 8192
./cpp/build/openfhe_ckks --corpus he_corpus/ckks_normal/ckks_normal_000008.csv --ring-size 16384
./cpp/build/seal_bfv_depth --corpus he_corpus/depth/exact_depth_000008.csv --ring-size 8192 --max-depth 4
./cpp/build/openfhe_ckks_depth --corpus he_corpus/depth/ckks_depth_000008.csv --ring-size 16384 --max-depth 4
./cpp/build/seal_bgv_serialization --corpus he_corpus/exact/exact_safe_000008.csv --ring-size 8192
./cpp/build/openfhe_ckks_serialization --corpus he_corpus/ckks_normal/ckks_normal_000008.csv --ring-size 16384
./cpp/build/seal_bfv_dot --corpus he_corpus/exact/exact_safe_000008.csv --ring-size 8192
./cpp/build/openfhe_ckks_dot --corpus he_corpus/ckks_normal/ckks_normal_000008.csv --ring-size 16384
./cpp/build/seal_bfv_memory --corpus he_corpus/exact/exact_safe_000008.csv --ring-size 8192
./cpp/build/openfhe_ckks_memory --corpus he_corpus/ckks_normal/ckks_normal_000008.csv --ring-size 16384
```

If OpenFHE is installed somewhere other than the default prefix, configure this repo with:

```bash
cmake -S cpp -B cpp/build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/path/to/openfhe/install
```

For a temporary SEAL-only local build, pass
`-DHE_BENCHMARK_BUILD_OPENFHE=OFF`. If an existing build directory was
configured before OpenFHE became the default, force the cached option back on
with `-DHE_BENCHMARK_BUILD_OPENFHE=ON`.
