#!/usr/bin/env python3
import argparse
import csv
import statistics
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TOOL = ROOT / "tools" / "counterfactual_dispatch.cpp"
BINARY = Path("/tmp/io1-counterfactual")


def build():
    subprocess.run(["g++", "-std=c++20", "-O2", "-Wall", "-Wextra", "-Wpedantic",
                    "-I.", str(TOOL), "-o", str(BINARY)], cwd=ROOT, check=True)


def run_matrix(args):
    output = Path(args.output)
    output.mkdir(parents=True, exist_ok=True)
    rows = []
    summaries = []
    for instance in args.instances.split(","):
        for seed in args.seeds.split(","):
            for mode in args.modes.split(","):
                path = output / f"oracle_{instance}_{seed}_m{mode}.csv"
                print(f"running {instance} {seed} mode={mode}", flush=True)
                completed = subprocess.run([
                    str(BINARY), "--instance", instance, "--seed", seed,
                    "--freeze-evals", str(args.freeze_evals), "--mode", mode,
                    "--lookahead", str(args.lookahead), "--max-events", str(args.max_events),
                    "--alts", str(args.alts), "--out", str(path),
                ], cwd=ROOT, capture_output=True, text=True)
                if completed.returncode != 0:
                    raise RuntimeError(completed.stderr)
                with path.open() as file:
                    rows.extend(list(csv.DictReader(file)))
                with Path(str(path) + ".summary.csv").open() as file:
                    summaries.extend(list(csv.DictReader(file)))
    return rows, summaries


def gate(rows, summaries):
    verdict = {}
    for instance in ("09", "10"):
        instance_summaries = [s for s in summaries if s["instance"] == instance]
        instance_rows = [r for r in rows if r["instance"] == instance]
        coverage_poor = sum(int(s["selected_action_count"]) < 15 for s in instance_summaries)
        seed_best = {}
        for summary in instance_summaries:
            seed = summary["seed"]
            value = float(summary["best_single_gain_pct"])
            seed_best[seed] = max(seed_best.get(seed, 0.0), value)
        good = sum(value >= 0.5 for value in seed_best.values())
        median_gain = statistics.median(seed_best.values()) if seed_best else 0.0
        bands = set()
        for row in instance_rows:
            if row["alternative_rank"] in ("", "-1"):
                continue
            if row["override_applied"] == "1" and row["feasible"] == "1":
                if float(row["delta_pct"]) >= 0.5:
                    bands.add(row["band"])
        verdict[instance] = {
            "seeds_at_least_0.5": good,
            "median_gain": median_gain,
            "bands": sorted(bands),
            "coverage_poor_snapshots": coverage_poor,
        }
    passed = all(
        item["seeds_at_least_0.5"] >= 3 and item["median_gain"] >= 0.5 and
        len(item["bands"]) >= 2 and item["coverage_poor_snapshots"] < 2
        for item in verdict.values()
    )
    return verdict, passed


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--instances", default="09,10")
    parser.add_argument("--seeds", default="20261109,12345,55555,10101,98989")
    parser.add_argument("--modes", default="0,1")
    parser.add_argument("--freeze-evals", default="5000")
    parser.add_argument("--lookahead", type=int, default=0)
    parser.add_argument("--max-events", type=int, default=20)
    parser.add_argument("--alts", type=int, default=3)
    parser.add_argument("--output", default="experiments/local/q0_5k")
    parser.add_argument("--skip-build", action="store_true")
    args = parser.parse_args()
    if not args.skip_build:
        build()
    rows, summaries = run_matrix(args)
    output = Path(args.output)
    with (output / "rows.csv").open("w", newline="") as file:
        writer = csv.DictWriter(file, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)
    with (output / "summary.csv").open("w", newline="") as file:
        writer = csv.DictWriter(file, fieldnames=list(summaries[0].keys()))
        writer.writeheader()
        writer.writerows(summaries)
    verdict, passed = gate(rows, summaries)
    for instance, item in verdict.items():
        print(instance, item)
    print("Q0", "PASS" if passed else "FAIL")


if __name__ == "__main__":
    main()
