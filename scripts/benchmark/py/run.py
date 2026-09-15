#!/usr/bin/env python3
"""Run each benchmark in an isolated subprocess (fresh interpreter, no
cross-benchmark refcount/GC interference) and repeat runs, reporting min/median."""
import subprocess, sys, statistics, re

PY = sys.executable
RUNS = 3

BENCHES = [
    "fib30", "fib35", "fib40",
    "loop", "globalloop",
    "binary_trees", "instantiation", "invocation",
    "method_call", "properties", "trees", "zoo", "zoo_batch",
]

def run_once(name):
    out = subprocess.run(
        [PY, "scripts/benchmark/py/bench.py", name],
        capture_output=True, text=True, cwd=".",
    )
    if out.returncode != 0:
        print(out.stderr)
        return None
    # extract "name: Nms" (line-formatted)
    label = {"loop": "loop 1e8", "globalloop": "global loop 1e8", "zoo_batch": "zoo_batch(10sec)"}.get(name, name)
    m = re.search(rf"{re.escape(label)}: (\d+)ms", out.stdout)
    return int(m.group(1)) if m else None

def main():
    only = sys.argv[1:] or BENCHES
    for name in only:
        times = []
        for _ in range(RUNS):
            t = run_once(name)
            if t is not None:
                times.append(t)
        if not times:
            print(f"{name}: FAILED")
            continue
        print(f"{name}: min={min(times)}ms median={int(statistics.median(times))}ms all={times}")

if __name__ == "__main__":
    main()
