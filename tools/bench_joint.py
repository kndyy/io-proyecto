#!/usr/bin/env python3
import argparse
import csv
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BINARY = ROOT / "io1"
VALIDATOR = ROOT / "tools" / "validate_txt.py"


def build():
    subprocess.run(["g++", "-std=c++20", "-O2", "-Wall", "-Wextra", "-Wpedantic",
                    "main.cpp", "-o", "io1"], cwd=ROOT, check=True)
    subprocess.run([str(BINARY), "--self-test"], cwd=ROOT, check=True)


def parse_line(line, prefix):
    values = {}
    for token in line[len(prefix):].split():
        key, value = token.split("=", 1)
        values[key] = value
    return values


def run_case(instance, args):
    completed = subprocess.run([str(BINARY), "--input", str(instance)] + args,
                               cwd=ROOT, capture_output=True, text=True)
    for line in completed.stdout.splitlines():
        if line.startswith("RESULT "):
            return parse_line(line, "RESULT ")
    raise RuntimeError(completed.stderr or "no RESULT line")


def stage_a(output, seeds):
    rows = []
    for number in (9, 10, 6, 8):
        instance = ROOT / "instances" / f"instancia_{number:02d}.io1"
        for seed in seeds:
            completed = subprocess.run([str(BINARY), "--input", str(instance),
                                        "--dispatch-ablation", "--seed", str(seed)],
                                       cwd=ROOT, capture_output=True, text=True)
            if completed.returncode != 0:
                raise RuntimeError(completed.stderr)
            for line in completed.stdout.splitlines():
                if line.startswith("ABLATION "):
                    row = parse_line(line, "ABLATION ")
                    row["instance"] = f"{number:02d}"
                    row["seed"] = seed
                    rows.append(row)
    path = output / "dispatch_ablation.csv"
    with path.open("w", newline="") as file:
        writer = csv.DictWriter(file, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)
    return path


def stage_bc(output, stage, seeds):
    evals = 5000 if stage == "B" else 20000
    rows = []
    for number in ((9, 10) if stage == "B" else (9, 10, 6, 8)):
        instance = ROOT / "instances" / f"instancia_{number:02d}.io1"
        for seed in seeds:
            txt = output / f"joint_{number:02d}_{seed}.txt"
            joint = run_case(instance, ["--strategy", "phase-joint", "--seed", str(seed),
                                        "--max-evals", str(evals), "--max-neighbors", "120",
                                        "--txt", str(txt)])
            check = subprocess.run([sys.executable, str(VALIDATOR), str(instance), str(txt)],
                                   cwd=ROOT, capture_output=True, text=True)
            if check.returncode != 0:
                raise RuntimeError(check.stdout + check.stderr)
            rows.append({"instance": f"{number:02d}", "seed": seed, "strategy": "phase-joint",
                         "evals": joint["evals"], "mean": joint["mean"],
                         "cpu_ms": joint["cpu_ms"], "validation": "OK"})
            for mode in ("0", "1"):
                bucket = run_case(instance, ["--strategy", "phase-bucket", "--seed", str(seed),
                                             "--max-evals", str(evals), "--max-neighbors", "120",
                                             "--phase-global", mode])
                rows.append({"instance": f"{number:02d}", "seed": seed,
                             "strategy": f"phase-bucket-{mode}", "evals": bucket["evals"],
                             "mean": bucket["mean"], "cpu_ms": bucket["cpu_ms"],
                             "validation": "n/a"})
    path = output / f"stage_{stage}.csv"
    with path.open("w", newline="") as file:
        writer = csv.DictWriter(file, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)
    return path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--stage", choices=["A", "B", "C"], required=True)
    parser.add_argument("--output", default="experiments/local")
    parser.add_argument("--seeds", default="20261109,12345,55555")
    parser.add_argument("--skip-build", action="store_true")
    args = parser.parse_args()
    output = Path(args.output)
    output.mkdir(parents=True, exist_ok=True)
    if not args.skip_build:
        build()
    seeds = [int(value) for value in args.seeds.split(",")]
    start = time.time()
    if args.stage == "A":
        path = stage_a(output, seeds)
    else:
        path = stage_bc(output, args.stage, seeds)
    print(path)
    print(f"elapsed={time.time() - start:.1f}s")


if __name__ == "__main__":
    main()
