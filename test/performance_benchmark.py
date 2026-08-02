#!/usr/bin/env python3
import shutil
import statistics
import subprocess
import time
from pathlib import Path

BLUE = "\033[94m"
GREEN = "\033[92m"
YELLOW = "\033[93m"
RESET = "\033[0m"

SANDBOX = Path("/tmp/uwu_benchmark_sandbox")


def timed(label, runs, fn):
    samples = []
    for _ in range(runs):
        start = time.perf_counter()
        fn()
        samples.append((time.perf_counter() - start) * 1_000_000.0)
    avg = statistics.mean(samples)
    med = statistics.median(samples)
    best = min(samples)
    worst = max(samples)
    print(f"{label:<34} avg={avg:10.2f} us  median={med:10.2f} us  best={best:10.2f} us  worst={worst:10.2f} us  runs={runs}")


def run_uwu(*args):
    res = subprocess.run(["uwu", *args], cwd=SANDBOX, capture_output=True, text=True)
    if "error:" in res.stderr:
        raise RuntimeError(f"uwu {' '.join(args)} failed:\n{res.stderr}")
    return res


def write_many_files(count, version):
    for i in range(count):
        path = SANDBOX / f"files/file_{i:04d}.txt"
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(f"file {i}\nversion {version}\n" + ("payload\n" * 20))


def main():
    print(f"\n{YELLOW}=== Lightweight uwu Performance Benchmarks ==={RESET}")
    print("These are practical wall-clock timings for common CLI operations. Lower is better.")
    print("They are not CPU-cycle-perfect microbenchmarks; use them to spot regressions and compare rough costs.\n")

    shutil.rmtree(SANDBOX, ignore_errors=True)
    SANDBOX.mkdir(parents=True)

    timed("uwu init", 1, lambda: run_uwu("init"))

    write_many_files(200, 0)
    all_files = [f"files/file_{i:04d}.txt" for i in range(200)]
    timed("add 200 new files", 1, lambda: run_uwu("add", *all_files))
    timed("commit 200-file snapshot", 1, lambda: run_uwu("commit", "benchmark seed"))
    timed("status clean tree", 10, lambda: run_uwu("status"))

    write_many_files(200, 1)
    timed("add 200 modified files", 1, lambda: run_uwu("add", *all_files))
    timed("commit 200-file delta", 1, lambda: run_uwu("commit", "benchmark update"))

    shas = [(SANDBOX / ".uwu/refs/heads/main").read_text().strip()]
    for i in range(2, 12):
        write_many_files(40, i)
        changed = [f"files/file_{j:04d}.txt" for j in range(40)]
        run_uwu("add", *changed)
        run_uwu("commit", f"benchmark chain {i}")
        shas.append((SANDBOX / ".uwu/refs/heads/main").read_text().strip())

    first = shas[0]
    latest = shas[-1]
    timed("checkout old commit", 5, lambda: run_uwu("checkout", first))
    timed("checkout latest commit", 5, lambda: run_uwu("checkout", latest))

    print(f"\n{GREEN}=== PERFORMANCE BENCHMARKS COMPLETED ==={RESET}")
    print("Tip: run several times and compare trends, not one-off numbers.\n")


if __name__ == "__main__":
    main()
