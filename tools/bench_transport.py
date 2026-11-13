#!/usr/bin/env python3
import argparse
import csv
import hashlib
import json
from pathlib import Path
import subprocess
import time

from validate_txt import load_instance, parse_txt, validate
from validate_transport import validate_buffers


def objective(instance, path):
    events = parse_txt(path, instance)
    mean = validate(instance, events)
    validate_buffers(instance, events)
    return mean


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    parser.add_argument("--instances", default="09,10")
    parser.add_argument("--seeds", default="20261109,12345,55555")
    parser.add_argument("--max-evals", type=int, default=1000000)
    parser.add_argument("--temperature", type=float, default=0.5)
    parser.add_argument("--cycle", type=int, default=50000)
    parser.add_argument("--fixed-machines", action="store_true")
    parser.add_argument("--lns", action="store_true")
    parser.add_argument("--destroy", type=int, default=4)
    parser.add_argument("--repair", type=int, default=32)
    parser.add_argument("--from-dir", default="proposed_submissions")
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    output = Path(args.output)
    output.mkdir(parents=True, exist_ok=True)
    binary = Path(args.binary).resolve()
    provenance = dict(vars(args))
    provenance["source_sha"] = subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip()
    provenance["binary_sha256"] = hashlib.sha256(binary.read_bytes()).hexdigest()
    provenance["source_sha256"] = {
        name: hashlib.sha256(Path(name).read_bytes()).hexdigest()
        for name in ("main.cpp", "transport.hpp", "heuristic.hpp", "search_types.hpp", "simulator.hpp", "instance.hpp")
    }
    (output / "provenance.json").write_text(json.dumps(provenance, indent=2) + "\n")
    with (output / "summary.csv").open("w") as file:
        writer = csv.writer(file)
        writer.writerow(["instance", "seed", "max_evals", "start_mean", "mean", "gain_pct", "elapsed_s", "txt"])
        for number in args.instances.split(","):
            number = f"{int(number):02}"
            instance_path = f"instances/instancia_{number}.io1"
            instance = load_instance(instance_path)
            start = Path(args.from_dir) / f"instancia_{number}.txt"
            baseline = objective(instance, start)
            best = baseline
            for seed in args.seeds.split(","):
                txt = output / f"instancia_{number}_seed_{seed}.txt"
                command = [str(binary), "--input", instance_path, "--strategy", "transport-search",
                           "--from-txt", str(start), "--seed", seed, "--max-evals", str(args.max_evals),
                           "--transport-temperature", str(args.temperature), "--transport-cycle", str(args.cycle),
                           "--transport-trace", str(txt.with_suffix(".trace.csv")),
                           "--txt", str(txt), "--debug-dir", str(output / f"debug_{number}_{seed}")]
                if args.fixed_machines:
                    command.append("--transport-fixed-machines")
                if args.lns:
                    command += ["--transport-lns", "--transport-destroy", str(args.destroy), "--transport-repair", str(args.repair)]
                begin = time.monotonic()
                completed = subprocess.run(command, text=True, capture_output=True, check=True)
                elapsed = time.monotonic() - begin
                (txt.with_suffix(".log")).write_text(completed.stdout + completed.stderr)
                mean = objective(instance, txt)
                writer.writerow([number, seed, args.max_evals, f"{best:.12f}", f"{mean:.12f}",
                                 f"{100 * (baseline - mean) / baseline:.6f}", f"{elapsed:.3f}", str(txt)])
                file.flush()
                print(f"{number} seed={seed} start={best:.9f} mean={mean:.9f} "
                      f"gain={100 * (baseline - mean) / baseline:.3f}% elapsed={elapsed:.2f}s", flush=True)
                if mean < best:
                    best, start = mean, txt
            (output / f"instancia_{number}.txt").write_bytes(start.read_bytes())


if __name__ == "__main__":
    main()
