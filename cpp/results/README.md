# Benchmark Results

This directory is the local output target for benchmark run logs.

Generated result files are ignored by git. Keep only this note and `.gitkeep`
tracked so fresh clones still have a known place to write output.

The normal entry point is:

```bash
./run_benchmarks.py --all --ring-size 8192
```

It writes:

```text
seal.csv
openfhe.csv
openfhe_threads6.csv
```

Normal report names:

```text
seal.csv
openfhe.csv
openfhe_threads6.csv
```
