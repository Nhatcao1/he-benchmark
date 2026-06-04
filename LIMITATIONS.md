# Benchmark Limitations

This file records library and parameter limitations observed while running the
SEAL/OpenFHE benchmark suite. It is not a bug log.

## BFV Ring Size

- Combined SEAL + OpenFHE BFV runs should use `ring_size >= 8192`.
- `ring_size=2048` is too small for the expanded SEAL BFV benchmark because the
  current suite creates relinearization keys and runs relin/key-switch dependent
  operations.
- `ring_size=4096` may work for SEAL-only runs, but OpenFHE BFVRNS rejects it
  under the current security settings and requests `8192`.
- Recommended combined comparison ring sizes:
  - `8192`
  - `16384`

## BFV Corpus Size vs Ring Size

- Do not run `--all` with a ring size smaller than `8192`.
- `--all` includes the `8192` corpus file, so smaller rings cannot pack all
  active slots.

## OpenFHE Serialization

- OpenFHE BFV object serialization is currently not used inside
  `openfhe_bfv_exact`.
- Attempts to serialize OpenFHE BFV relin/eval keys or objects for byte-size
  reporting failed with cereal polymorphic type registration errors.
- OpenFHE BFV rows currently report timing, throughput, and correctness, but do
  not report object `byte_size`.
- SEAL BFV rows still report byte sizes where available.

## Current Scope

- Current implemented scheme scope is BFV exact benchmarking.
- BGV is not treated as identical to BFV. It should be added as separate
  `scheme=BGV` benchmark targets and result rows.
- CKKS requires separate numerical accuracy metrics such as MAE, RMSE, max
  error, and relative error.
