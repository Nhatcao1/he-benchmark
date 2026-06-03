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

## Current C++ Test Suite

The first C++ benchmark target is `seal_bfv_exact`.

It validates Microsoft SEAL BFV batched exact arithmetic against the generated exact corpus:

- add
- subtract
- multiply
- square
- negate

The benchmark reads signed integer reference values from `he_corpus/exact/*.csv`, decrypts each result, and compares slots with centered modulo normalization. Timing is printed per operation, but this first target is mainly a correctness baseline before adding OpenFHE and larger timing runs.

Build and run:

```bash
cmake -S cpp -B cpp/build
cmake --build cpp/build --target seal_bfv_exact
./cpp/build/seal_bfv_exact he_corpus/exact/exact_safe_000008.csv
```
