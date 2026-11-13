#!/usr/bin/env python3
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import csv
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import time

from validate_txt import load_instance, parse_txt, validate
from validate_transport import validate_buffers


def score(instance, path):
    events = parse_txt(path, instance)
    mean = validate(instance, events)
    validate_buffers(instance, events)
    return mean


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", default="./io1")
    parser.add_argument("--instances", default="02,03,04,05,06,07,08,09,10")
    parser.add_argument("--weights", default="0,0.5,1,2")
    parser.add_argument("--seeds", default="20261115")
    parser.add_argument("--max-evals", type=int, default=1000000)
    parser.add_argument("--temperature", type=float, default=0.5)
    parser.add_argument("--cycle", type=int, default=50000)
    parser.add_argument("--workers", type=int, default=2)
    parser.add_argument("--from-dir", default="proposed_submissions/transport_search")
    parser.add_argument("--warm", action="store_true")
    parser.add_argument("--lns", action="store_true")
    parser.add_argument("--assignment", action="store_true")
    parser.add_argument("--jobs", action="store_true")
    parser.add_argument("--rebuild", type=int, default=0)
    parser.add_argument("--spt", action="store_true")
    parser.add_argument("--window", type=int, default=0)
    parser.add_argument("--profile", type=int, default=0)
    parser.add_argument("--request-scan", action="store_true")
    parser.add_argument("--fractional", action="store_true")
    parser.add_argument("--biases", default="99")
    parser.add_argument("--batches", default="0")
    parser.add_argument("--globals", default="auto")
    parser.add_argument("--destroy", type=int, default=6)
    parser.add_argument("--repair", type=int, default=48)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    binary = str(Path(args.binary).resolve())
    output = Path(args.output)
    output.mkdir(parents=True, exist_ok=True)
    instances = {f"{int(number):02}": load_instance(f"instances/instancia_{int(number):02}.io1")
                 for number in args.instances.split(",")}
    best = {}
    for number, instance in instances.items():
        source = Path(args.from_dir) / f"instancia_{number}.txt"
        best[number] = score(instance, source)
        shutil.copyfile(source, output / source.name)
    provenance = dict(vars(args))
    provenance["source_sha"] = subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip()
    provenance["binary_sha256"] = hashlib.sha256(Path(binary).read_bytes()).hexdigest()
    provenance["source_sha256"] = {name: hashlib.sha256(Path(name).read_bytes()).hexdigest()
                                   for name in ("main.cpp", "transport.hpp", "heuristic.hpp", "search_types.hpp", "simulator.hpp", "instance.hpp")}
    (output / "provenance.json").write_text(json.dumps(provenance, indent=2) + "\n")

    def run(number, weight, seed, bias, batch, global_mode):
        label = f"{number}_w{weight}_s{seed}"
        if bias != "99" or batch != "0" or global_mode != "auto":
            label += f"_b{bias}_c{batch}_g{global_mode}"
        txt = output / f"candidate_{label}.txt"
        command = [binary, "--input", f"instances/instancia_{number}.io1", "--strategy", "transport-search",
                   "--max-evals", str(args.max_evals), "--seed", seed, "--transport-temperature", str(args.temperature),
                   "--transport-cycle", str(args.cycle), "--txt", str(txt),
                   "--transport-trace", str(output / f"trace_{label}.csv")]
        if args.warm:
            command += ["--from-txt", str(Path(args.from_dir) / f"instancia_{number}.txt")]
        else:
            command += ["--transport-start", "slots", "--transport-start-weight", weight]
            if bias != "99": command += ["--transport-seed-bias", bias]
            if batch != "0": command += ["--transport-seed-batch", batch]
            if global_mode != "auto": command += ["--phase-global", global_mode]
        if args.lns:
            command += ["--transport-lns", "--transport-destroy", str(args.destroy), "--transport-repair", str(args.repair)]
        if args.assignment:
            command.append("--transport-assignment")
        if args.jobs:
            command.append("--transport-jobs")
        if args.rebuild:
            command += ["--transport-rebuild", str(args.rebuild)]
            command += ["--transport-profile", str(args.profile)]
        if args.spt:
            command.append("--transport-spt")
        if args.window:
            command += ["--transport-window", str(args.window)]
        if args.request_scan:
            command.append("--transport-request-scan")
        if args.fractional:
            command.append("--transport-fractional")
        begin = time.monotonic()
        completed = subprocess.run(command, text=True, capture_output=True, check=True)
        elapsed = time.monotonic() - begin
        (output / f"run_{label}.log").write_text(completed.stdout + completed.stderr)
        mean = score(instances[number], txt)
        return number, weight, seed, bias, batch, global_mode, mean, elapsed, txt

    with (output / "summary.csv").open("w") as file, ThreadPoolExecutor(max_workers=args.workers) as pool:
        writer = csv.writer(file)
        writer.writerow(["instance", "weight", "seed", "bias", "batch", "global", "mean", "elapsed_s", "incumbent", "improved", "txt"])
        futures = [pool.submit(run, number, weight, seed, bias, batch, global_mode) for number in instances
                   for weight in args.weights.split(",") for seed in args.seeds.split(",")
                   for bias in args.biases.split(",") for batch in args.batches.split(",") for global_mode in args.globals.split(",")]
        for future in as_completed(futures):
            number, weight, seed, bias, batch, global_mode, mean, elapsed, txt = future.result()
            improved = mean < best[number] - 1e-9
            if improved:
                best[number] = mean
                shutil.copyfile(txt, output / f"instancia_{number}.txt")
            writer.writerow([number, weight, seed, bias, batch, global_mode, f"{mean:.12f}", f"{elapsed:.3f}", f"{best[number]:.12f}", int(improved), str(txt)])
            file.flush()
            print(f"{number} weight={weight} seed={seed} bias={bias} batch={batch} global={global_mode} mean={mean:.9f} best={best[number]:.9f} "
                  f"improved={int(improved)} elapsed={elapsed:.1f}s", flush=True)


if __name__ == "__main__":
    main()
