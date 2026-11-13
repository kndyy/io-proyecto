#!/usr/bin/env python3
import argparse
import csv
import hashlib
from pathlib import Path
import shutil

from validate_txt import load_instance, parse_txt, validate
from validate_transport import validate_buffers


def measure(instance, path):
    events = parse_txt(path, instance)
    mean = validate(instance, events)
    validate_buffers(instance, events)
    return mean


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("directories", nargs="+")
    parser.add_argument("--dest", default="proposed_submissions/transport_search")
    parser.add_argument("--archive", default="experiments/local/target_incumbent_archive")
    parser.add_argument("--history", default="results/transport_portfolio.csv")
    args = parser.parse_args()
    archive = Path(args.archive)
    archive.mkdir(parents=True, exist_ok=True)
    history = Path(args.history)
    new = not history.exists()
    if not new:
        history.write_bytes(history.read_bytes().replace(b"\r\n", b"\n"))
    with history.open("a") as file:
        writer = csv.writer(file, lineterminator="\n")
        if new:
            writer.writerow(["instance", "previous_mean", "new_mean", "source_path", "sha256"])
        for number in range(1, 11):
            name = f"instancia_{number:02}.txt"
            instance = load_instance(f"instances/instancia_{number:02}.io1")
            dest = Path(args.dest) / name
            previous = best = measure(instance, dest)
            source = None
            for directory in args.directories:
                candidate = Path(directory) / name
                if not candidate.exists():
                    continue
                mean = measure(instance, candidate)
                if mean < best - 1e-7:
                    best, source = mean, candidate
            if source is not None:
                old_hash = hashlib.sha256(dest.read_bytes()).hexdigest()
                shutil.copyfile(dest, archive / f"{number:02}_{old_hash}.txt")
                shutil.copyfile(source, dest)
                digest = hashlib.sha256(dest.read_bytes()).hexdigest()
                writer.writerow([f"{number:02}", f"{previous:.12f}", f"{best:.12f}", str(source), digest])
                print(f"{number:02}: {previous:.9f} -> {best:.9f} from {source}")


if __name__ == "__main__":
    main()
