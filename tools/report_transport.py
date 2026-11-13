#!/usr/bin/env python3
import argparse
from collections import defaultdict
import csv
import hashlib
import io
import math
from pathlib import Path

from validate_txt import load_instance, parse_txt, validate, location_index
from validate_transport import validate_buffers


def measure(instance, path):
    events = parse_txt(path, instance)
    mean = validate(instance, events)
    validate_buffers(instance, events)
    vehicles = defaultdict(list)
    machines = defaultdict(list)
    pickup = {}
    deliveries = []
    machine_of = {}
    for event in events:
        if event["action"] == "PROCESS":
            lot = event["lot"]
            j = event["machine"] - 1
            machine_of[lot] = j
            end = event["start"] + instance["lots"][lot][1] / instance["speed"][lot - 1][j]
            machines[j].append((event["start"], end, lot))
        else:
            vehicles[event["agv"]].append(event)
            if event["action"] == "PICKUP" and event["location"].startswith("b"):
                pickup[event["lot"]] = event["start"]
            if event["action"] == "DELIVER" and event["location"] == "f":
                deliveries.append(event["start"])
    empty = loaded = blocked = idle = mixed = 0.0
    for actions in vehicles.values():
        load = {}
        for event in sorted(actions, key=lambda e: (e["start"], e["order"])):
            if event["action"] == "MOVE":
                travel = instance["tau"][location_index(event["location"], instance)][location_index(event["destination"], instance)]
                if load:
                    loaded += travel
                    if len(set(load.values())) > 1:
                        mixed += travel
                else:
                    empty += travel
            elif event["action"] == "PICKUP":
                load[event["lot"]] = "in" if event["location"] == "o" else "out"
            else:
                del load[event["lot"]]
    for processes in machines.values():
        previous = None
        ready = 0.0
        for start, end, lot in sorted(processes):
            idle += max(0.0, start - ready)
            holding = max(0.0, pickup[previous] - end) if previous is not None else 0.0
            blocked += holding
            ready = end + holding
            previous = lot
    return dict(mean=mean, empty=empty, loaded=loaded, mixed=mixed, blocked=blocked, idle=idle,
                machine_of=machine_of,
                makespan=max(deliveries), sha256=hashlib.sha256(Path(path).read_bytes()).hexdigest())


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--baseline-dir", default="proposed_submissions")
    parser.add_argument("--candidate-dir", default="proposed_submissions/transport_search")
    parser.add_argument("--out", default="results/transport_search.csv")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    with Path("results/public_targets.csv").open() as file:
        targets = {row["instance"]: row for row in csv.DictReader(file)}
    met = 0
    with io.StringIO() as file:
        fields = ["instance", "baseline_mean", "candidate_mean", "gain_pct"]
        fields += [f"{prefix}_{metric}" for metric in ("empty", "loaded", "mixed", "blocked", "idle", "makespan")
                   for prefix in ("baseline", "candidate")]
        fields += ["reassigned_lots", "candidate_sha256", "local_validation", "public_mean", "target_mean", "gap_pct", "target_met"]
        writer = csv.DictWriter(file, fieldnames=fields, lineterminator="\n")
        writer.writeheader()
        for number in range(1, 11):
            name = f"instancia_{number:02}.txt"
            instance = load_instance(f"instances/instancia_{number:02}.io1")
            baseline = measure(instance, Path(args.baseline_dir) / name)
            candidate = measure(instance, Path(args.candidate_dir) / name)
            gain = 100 * (baseline["mean"] - candidate["mean"]) / baseline["mean"]
            target = targets[f"{number:02}"]
            if not math.isclose(float(target["target_mean"]), 1.01 * float(target["public_mean"]), rel_tol=0, abs_tol=1e-8):
                raise ValueError(f"incorrect 1% target for {number:02}")
            gap = 100 * (candidate["mean"] / float(target["public_mean"]) - 1)
            row = dict(instance=f"{number:02}", baseline_mean=f'{baseline["mean"]:.12f}',
                       candidate_mean=f'{candidate["mean"]:.12f}', gain_pct=f"{gain:.6f}",
                       candidate_sha256=candidate["sha256"], local_validation="buffers_replayed")
            row.update(public_mean=target["public_mean"], target_mean=target["target_mean"],
                       gap_pct=f"{gap:.6f}", target_met=int(candidate["mean"] <= float(target["target_mean"])))
            row["reassigned_lots"] = sum(baseline["machine_of"][lot] != candidate["machine_of"][lot]
                                         for lot in baseline["machine_of"])
            for metric in ("empty", "loaded", "mixed", "blocked", "idle", "makespan"):
                for prefix, values in (("baseline", baseline), ("candidate", candidate)):
                    row[f"{prefix}_{metric}"] = f"{values[metric]:.9f}"
            writer.writerow(row)
            met += row["target_met"]
            print(f'{number:02}: {baseline["mean"]:.9f} -> {candidate["mean"]:.9f} ({gain:.3f}% reduction), '
                  f'public gap={gap:.3f}%, target_met={row["target_met"]}')
        rendered = file.getvalue()
    if args.check:
        if Path(args.out).read_text() != rendered:
            raise SystemExit("published transport report does not match validated TXT files; regenerate it")
    else:
        Path(args.out).write_text(rendered)
    print(f"Validated 10/10 schedules; within_1pct={met}/10; platform_acceptance=not_claimed")


if __name__ == "__main__":
    main()
