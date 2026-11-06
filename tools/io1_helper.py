#!/usr/bin/env python3
import argparse
import csv
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BINARY = ROOT / "io1"
DEFAULT_STRATEGIES = ["b1", "b2", "b3", "s0", "s1", "s2", "vnd"]
ALL_STRATEGIES = ["b1", "b2", "b3", "s0", "s1", "s2", "vnd", "vnd-s0", "vnd-s1", "vnd-s2"]


def build():
    command = [
        "g++", "-std=c++20", "-O2", "-Wall", "-Wextra", "-Wpedantic",
        "main.cpp", "-o", "io1",
    ]
    subprocess.run(command, cwd=ROOT, check=True)
    print(f"built {BINARY}")


def ensure_binary():
    if not BINARY.exists():
        build()


def parse_result(output):
    for line in output.splitlines():
        if not line.startswith("RESULT "):
            continue
        values = {}
        for token in line.split()[1:]:
            key, value = token.split("=", 1)
            values[key] = value
        return values
    raise RuntimeError("no RESULT line")


def run_one(instance, strategy, seed, out=None):
    command = [str(BINARY), "--input", str(instance), "--strategy", strategy, "--seed", str(seed)]
    if out is not None:
        out.parent.mkdir(parents=True, exist_ok=True)
        command += ["--out", str(out)]
    completed = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
    values = parse_result(completed.stdout)
    values["exit_code"] = completed.returncode
    values["stderr"] = completed.stderr.strip()
    return values


def self_test():
    ensure_binary()
    completed = subprocess.run([str(BINARY), "--self-test"], cwd=ROOT, capture_output=True, text=True)
    sys.stdout.write(completed.stdout)
    sys.stderr.write(completed.stderr)
    if completed.returncode != 0:
        raise SystemExit(completed.returncode)
    print("self-test passed")


def bench(args):
    ensure_binary()
    instances = sorted(Path(args.instances).glob("*.io1"))
    if not instances:
        raise SystemExit(f"no instances in {args.instances}")
    strategies = [s.strip() for s in args.strategies.split(",") if s.strip()]
    for strategy in strategies:
        if strategy not in ALL_STRATEGIES:
            raise SystemExit(f"unknown strategy: {strategy}")

    output = Path(args.out)
    output.parent.mkdir(parents=True, exist_ok=True)
    rows = []
    for instance in instances:
        for strategy in strategies:
            print(f"running {instance.stem} {strategy}", file=sys.stderr)
            values = run_one(instance, strategy, args.seed)
            rows.append({
                "instance": instance.stem,
                "strategy": strategy,
                "batch_limit": values.get("batch_limit", ""),
                "feasible": values.get("feasible", "0"),
                "mean": float(values.get("mean", "-1")),
                "evals": int(values.get("evals", "0")),
                "cpu_ms": float(values.get("cpu_ms", "0")),
            })

    with output.open("w", newline="") as file:
        writer = csv.DictWriter(file, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)

    best_by_instance = {}
    for row in rows:
        if row["feasible"] != "1":
            continue
        current = best_by_instance.get(row["instance"])
        if current is None or row["mean"] < current:
            best_by_instance[row["instance"]] = row["mean"]

    summary = {}
    for strategy in strategies:
        selected = [row for row in rows if row["strategy"] == strategy]
        feasible = [row for row in selected if row["feasible"] == "1"]
        ratios = [row["mean"] / best_by_instance[row["instance"]]
                  for row in feasible if row["instance"] in best_by_instance]
        summary[strategy] = {
            "feasible": len(feasible),
            "ratio": sum(ratios) / len(ratios) if ratios else float("inf"),
            "cpu_ms": sum(row["cpu_ms"] for row in selected) / len(selected),
            "evals": sum(row["evals"] for row in selected),
        }

    print(f"{'strategy':10} {'feasible':>8} {'avg_ratio':>10} {'avg_cpu_ms':>11} {'evals':>8}")
    for strategy in strategies:
        item = summary[strategy]
        print(f"{strategy:10} {item['feasible']:>8} {item['ratio']:>10.5f} "
              f"{item['cpu_ms']:>11.2f} {item['evals']:>8}")

    ranked = sorted(strategies, key=lambda s: (summary[s]["ratio"], summary[s]["cpu_ms"]))
    best = ranked[0]
    recommended = output.parent / "recommended_selector.conf"
    recommended.write_text(
        f"strategy={best}\nseed={args.seed}\nmax_evals=1000\nmax_neighbors=40\n"
    )
    print(f"recommended strategy: {best}")
    print(f"results: {output}")
    print(f"selector: {recommended}")

    if args.export_best:
        for instance in instances:
            values = run_one(instance, best, args.seed,
                             output.parent / "internal" / f"{instance.stem}.csv")
            print(f"exported {instance.stem}: mean={values.get('mean')}", file=sys.stderr)


def run(args):
    ensure_binary()
    out = Path(args.out) if args.out else None
    command = [str(BINARY), "--input", str(args.instance), "--seed", str(args.seed)]
    if args.strategy:
        command += ["--strategy", args.strategy]
    if args.selector:
        command += ["--selector", str(args.selector)]
    if out is not None:
        out.parent.mkdir(parents=True, exist_ok=True)
        command += ["--out", str(out)]
    completed = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
    sys.stdout.write(completed.stdout)
    sys.stderr.write(completed.stderr)
    raise SystemExit(completed.returncode)


def main():
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)

    subparsers.add_parser("build")
    subparsers.add_parser("test")

    bench_parser = subparsers.add_parser("bench")
    bench_parser.add_argument("--instances", default="instances")
    bench_parser.add_argument("--strategies", default=",".join(DEFAULT_STRATEGIES))
    bench_parser.add_argument("--seed", type=int, default=20261106)
    bench_parser.add_argument("--out", default="results/results.csv")
    bench_parser.add_argument("--export-best", action="store_true")

    run_parser = subparsers.add_parser("run")
    run_parser.add_argument("--instance", required=True)
    run_parser.add_argument("--strategy")
    run_parser.add_argument("--selector")
    run_parser.add_argument("--seed", type=int, default=20261106)
    run_parser.add_argument("--out")

    args = parser.parse_args()
    if args.command == "build":
        build()
    elif args.command == "test":
        self_test()
    elif args.command == "bench":
        bench(args)
    elif args.command == "run":
        run(args)


if __name__ == "__main__":
    main()
