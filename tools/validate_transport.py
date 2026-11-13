#!/usr/bin/env python3
import sys

from validate_txt import load_instance, parse_txt, validate


def validate_buffers(instance, events):
    timeline = []
    for event in events:
        if event["action"] == "PROCESS":
            machine = event["machine"] - 1
            lot = event["lot"]
            duration = instance["lots"][lot][1] / instance["speed"][lot - 1][machine]
            timeline.append((event["start"], 2, machine, lot))
            timeline.append((event["start"] + duration, 0, machine, lot))
        elif event["action"] == "PICKUP" and event["location"].startswith("b"):
            timeline.append((event["start"], 1, int(event["location"][1:]) - 1, event["lot"]))
        elif event["action"] == "DELIVER" and event["location"].startswith("a"):
            timeline.append((event["start"], 3, int(event["location"][1:]) - 1, event["lot"]))
    machines = [dict(alpha=None, beta=None, processing=None, blocked=None)
                for _ in range(instance["m"])]
    for time, kind, index, lot in sorted(timeline):
        machine = machines[index]
        if kind == 0:
            if machine["processing"] != lot:
                raise ValueError(f"unexpected processing completion M{index + 1} lot {lot}")
            machine["processing"] = None
            if machine["beta"] is None:
                machine["beta"] = lot
            else:
                machine["blocked"] = lot
        elif kind == 1:
            if machine["beta"] != lot:
                raise ValueError(f"beta pickup unavailable M{index + 1} lot {lot} at {time}")
            machine["beta"] = machine["blocked"]
            machine["blocked"] = None
        elif kind == 2:
            if machine["processing"] is not None or machine["blocked"] is not None:
                raise ValueError(f"processing on occupied/blocked M{index + 1} at {time}")
            if machine["alpha"] != lot:
                raise ValueError(f"input unavailable M{index + 1} lot {lot} at {time}")
            machine["alpha"] = None
            machine["processing"] = lot
        else:
            if machine["alpha"] is not None:
                raise ValueError(f"alpha occupied M{index + 1} lot {lot} at {time}")
            machine["alpha"] = lot
    if any(any(value is not None for value in machine.values()) for machine in machines):
        raise ValueError("nonempty machine/buffer at end of schedule")


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: validate_transport.py instance.io1 schedule.txt")
    instance = load_instance(sys.argv[1])
    events = parse_txt(sys.argv[2], instance)
    mean = validate(instance, events)
    validate_buffers(instance, events)
    print(f"OK mean={mean:.12f} events={len(events)} buffers=replayed")


if __name__ == "__main__":
    main()
