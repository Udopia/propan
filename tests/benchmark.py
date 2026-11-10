#!/usr/bin/env python3
"""Runs the probing benchmark on every instance with every propagator.

Checks that all propagators report identical counters and prints a timing table.

    tests/benchmark.py [--build DIR] [--large] [--props occ,watch,bin] [--runs N]

--large additionally downloads the instances listed in data/instances/large.txt
from benchmark-database.de into DIR/benchmarks (once) and runs them too.
"""
import argparse
import os
import re
import subprocess
import sys
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
INSTANCES = os.path.join(HERE, "data", "instances")


def run(probe, path, prop):
    out = subprocess.run([probe, path, prop], capture_output=True, text=True, check=True).stdout
    counters = "|".join(l for l in out.splitlines() if re.match(r"^c (probes|soft-true|block|greedy|root-assigned)|^s ", l))
    time = float(re.search(r"c propagator \S+ time (\S+)", out).group(1))
    return counters, time


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build", default=os.path.join(HERE, "..", "build", "release"))
    ap.add_argument("--large", action="store_true")
    ap.add_argument("--props", default="occ,watch,bin")
    ap.add_argument("--runs", type=int, default=1, help="repetitions, best time is reported")
    args = ap.parse_args()
    probe = os.path.join(args.build, "apps", "probe")
    props = args.props.split(",")

    files = sorted((os.path.join(INSTANCES, f) for f in os.listdir(INSTANCES) if f.endswith(".xz")), key=str.lower)
    if args.large:
        cache = os.path.join(args.build, "benchmarks")
        os.makedirs(cache, exist_ok=True)
        with open(os.path.join(INSTANCES, "large.txt")) as spec:
            for line in spec:
                if line.startswith("#") or not line.strip():
                    continue
                name, digest = line.split()[:2]
                path = os.path.join(cache, name)
                if not os.path.exists(path):
                    print(f"downloading {name} ...", file=sys.stderr)
                    urllib.request.urlretrieve(f"https://benchmark-database.de/file/{digest}", path)
                files.append(path)

    width = max(len(os.path.basename(f)) for f in files)
    print(f"{'instance':{width}s} " + " ".join(f"{p:>9s}" for p in props) + "   speedup vs " + props[0])
    mismatches = 0
    totals = {p: 0.0 for p in props}
    for path in files:
        results = {}
        for prop in props:
            best = None
            for _ in range(args.runs):
                counters, time = run(probe, path, prop)
                best = (counters, time) if best is None or time < best[1] else best
            results[prop] = best
        reference = results[props[0]][0]
        agree = all(r[0] == reference for r in results.values())
        mismatches += not agree
        for p in props:
            totals[p] += results[p][1]
        base = results[props[0]][1]
        speed = " ".join(f"{base / results[p][1]:6.2f}x" if results[p][1] > 0 else "   n/a" for p in props[1:])
        print(f"{os.path.basename(path):{width}s} " + " ".join(f"{results[p][1]:9.3f}" for p in props)
              + f"   {speed}" + ("" if agree else "   MISMATCH"))
    print(f"{'total':{width}s} " + " ".join(f"{totals[p]:9.3f}" for p in props)
          + "   " + " ".join(f"{totals[props[0]] / totals[p]:6.2f}x" for p in props[1:]))
    if mismatches:
        print(f"{mismatches} instance(s) with disagreeing propagators", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
