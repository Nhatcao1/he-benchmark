# Run Commands

Run these from the repository root.

## Setup

```bash
./setup_venv.sh
./generate_corpus.sh
cmake -S cpp -B cpp/build -DCMAKE_BUILD_TYPE=Release -DHE_BENCHMARK_BUILD_OPENFHE=ON
cmake --build cpp/build --target seal_bfv_exact openfhe_bfv_exact
```

This prepares the Python corpus generator, creates `he_corpus/`, configures the
C++ build, and builds both exact BFV benchmark executables.

## General Runner

```bash
./run_benchmarks.py --all --ring-size 8192
```

Runs every known exact BFV corpus test and writes three reports:

```text
cpp/results/seal.csv
cpp/results/openfhe.csv
cpp/results/openfhe_threads6.csv
```

The runner maps test names to exact CSV files. `openfhe.csv` runs OpenFHE with
one OpenMP thread; `openfhe_threads6.csv` runs the same OpenFHE binary with
`OMP_NUM_THREADS=6`.

Each report uses the same row format. The runner adds `test=...` and
`threads=...` before the executable output, for example:

```text
test=quick8,threads=1,library=SEAL,scheme=BFV,operation=add,size=8,ring_size=8192,correct=true,latency_ms=...
test=quick8,threads=6,library=OpenFHE,scheme=BFV,operation=add,size=8,ring_size=8192,correct=true,latency_ms=...
```

```bash
./run_benchmarks.py --tests quick8,edge --ring-size 8192
```

Runs only the 8-slot quick correctness corpus and signed edge-case corpus.

```bash
./run_benchmarks.py --tests 4096 --ring-size 8192
```

Runs one larger packed-vector corpus and writes the same three comparison
reports.

```bash
./run_benchmarks.py --tests quick8 --only seal --ring-size 8192
```

Debug-only path for running just one suite. Normal comparison runs should omit
`--only`.

```bash
./run_benchmarks.py --list-tests
```

Shows the supported test names and the corpus file each name resolves to.

## Direct Debug Commands

```bash
./cpp/build/seal_bfv_exact --corpus he_corpus/exact/exact_safe_000008.csv --ring-size 8192
./cpp/build/openfhe_bfv_exact --corpus he_corpus/exact/exact_safe_000008.csv --ring-size 8192
```

Use direct commands only when debugging one executable. Normal runs should go
through `run_benchmarks.py` so corpus selection and result paths stay consistent.
