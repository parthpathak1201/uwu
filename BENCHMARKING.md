# Benchmarking uwu

`./run_docker_tests.sh` includes a lightweight benchmark section. It uses Python's
`time.perf_counter()` around real `uwu` CLI commands and prints average, median,
best, and worst wall-clock times.

That benchmark is useful for:

- catching obvious performance regressions,
- comparing common user operations such as `add`, `commit`, `status`, and `checkout`,
- giving humans a quick feel for whether a change made the CLI slower.

It is not a precise CPU microbenchmark. It includes process startup, filesystem I/O,
Docker filesystem behavior, and OS scheduling noise.

## When to Use Google Benchmark

Google Benchmark is best for C++ microbenchmarks of individual functions, for example:

- `sha256_hex()` throughput,
- compression/decompression throughput,
- Merkle tree build time,
- JSON parse/stringify time,
- ignore matcher performance.

Those benchmarks run inside one C++ process and can measure small code paths more
carefully than a Python CLI wrapper.

A typical future setup would add a `bench/` directory with C++ benchmark files and
link them against Google Benchmark. Then CMake can build a `uwu_bench` executable.

## When to Use perf

`perf` is a Linux profiler. It answers "where is the program spending CPU time?"
rather than "how long did this operation take end to end?"

Good uses:

- finding hot functions during `commit`,
- checking whether compression dominates runtime,
- seeing whether hashing, JSON, Merkle construction, or filesystem traversal is the bottleneck.

Typical Linux commands:

```bash
perf stat ./build/uwu status
perf record ./build/uwu commit benchmark
perf report
```

On macOS, use Instruments or `sample` instead of Linux `perf`.

## Practical Rule

Use all three at different layers:

- CLI benchmark script: end-to-end user-visible timing.
- Google Benchmark: tight C++ function-level timing.
- perf/Instruments: profiling to find where CPU time goes.
