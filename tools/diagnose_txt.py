#!/usr/bin/env python3
import sys
from collections import defaultdict
from pathlib import Path

from validate_txt import load_instance, parse_txt, location_index

TOL = 1e-12


def diagnose(instance, events):
    errors = []
    n = instance["n"]
    m = instance["m"]
    k = instance["k"]
    capacity = instance["capacity"]

    def add(kind, message):
        errors.append((kind, message))

    machine_of = {}
    process_start = {}
    process_end = {}
    process_by_machine = defaultdict(list)
    for event in events:
        if event["action"] != "PROCESS":
            continue
        lot = event["lot"]
        machine = event["machine"] - 1
        if lot in machine_of:
            add("PROCESS", f"lote {lot} procesado mas de una vez")
            continue
        speed = instance["speed"][lot - 1][machine]
        if speed <= 0:
            add("PROCESS", f"lote {lot} incompatible con M{machine + 1}")
            continue
        duration = instance["lots"][lot][1] / speed
        end = event["start"] + duration
        machine_of[lot] = machine
        process_start[lot] = event["start"]
        process_end[lot] = end
        process_by_machine[machine].append((event["start"], end, lot))

    for lot in range(1, n + 1):
        if lot not in machine_of:
            add("PROCESS", f"lote {lot} sin PROCESS")

    for machine, processes in process_by_machine.items():
        processes.sort()
        previous_end = 0.0
        for start, end, lot in processes:
            if start < previous_end - TOL:
                add("MACHINE_OR_BUFFER",
                    f"lot {lot} starts PROCESS on M{machine + 1} at {start:.12f} "
                    f"before machine release at {previous_end:.12f} "
                    f"(diff {start - previous_end:.3e})")
            previous_end = max(previous_end, end)

    inbound_pickup = {}
    alpha_delivery = {}
    beta_pickup = {}
    final_delivery = {}
    agv_events = defaultdict(list)
    for event in events:
        if event["action"] != "PROCESS":
            agv_events[event["agv"]].append(event)

    for agv, actions in agv_events.items():
        actions.sort(key=lambda value: (value["start"], value["order"]))
        position = instance["o"]
        load = []
        busy_until = 0.0
        for event in actions:
            start = event["start"]
            if start < busy_until - TOL:
                add("AGV",
                    f"AGV{agv} {event['action']} lote {event['lot']} at {start:.12f} "
                    f"before movement end at {busy_until:.12f} "
                    f"(diff {start - busy_until:.3e})")
            action = event["action"]
            if action == "MOVE":
                origin = location_index(event["location"], instance)
                destination = location_index(event["destination"], instance)
                if origin != position:
                    add("AGV", f"AGV{agv} MOVE origin incorrect at {start:.12f}")
                busy_until = start + instance["tau"][origin][destination]
                position = destination
            elif action == "PICKUP":
                if location_index(event["location"], instance) != position:
                    add("AGV", f"AGV{agv} PICKUP lote {event['lot']} at wrong location")
                lot = event["lot"]
                if lot in load:
                    add("AGV", f"AGV{agv} PICKUP lote {lot} duplicado")
                if len(load) >= capacity:
                    add("AGV", f"AGV{agv} capacidad excedida at {start:.12f}")
                load.append(lot)
            else:
                if location_index(event["location"], instance) != position:
                    add("AGV", f"AGV{agv} DELIVER lote {event['lot']} at wrong location")
                lot = event["lot"]
                if lot not in load:
                    add("AGV", f"AGV{agv} DELIVER lote {lot} sin carga")
                else:
                    load.remove(lot)
        if load:
            add("AGV", f"AGV{agv} termina con carga {load}")

    for event in events:
        if event["action"] in {"PROCESS", "MOVE"}:
            continue
        lot = event["lot"]
        if event["action"] == "PICKUP":
            if event["location"] == "o":
                if lot in inbound_pickup:
                    add("LOT", f"lote {lot} pickup inicial duplicado")
                inbound_pickup[lot] = event["start"]
            elif event["location"].startswith("b"):
                if lot not in machine_of:
                    add("LOT", f"lote {lot} pickup beta sin PROCESS")
                    continue
                machine = machine_of[lot]
                if event["location"] != f"b{machine + 1}":
                    add("LOT", f"lote {lot} pickup beta en maquina incorrecta")
                beta_pickup[lot] = event["start"]
            else:
                add("LOT", f"lote {lot} pickup en ubicacion invalida")
        else:
            if event["location"] == "f":
                final_delivery[lot] = event["start"]
            elif event["location"].startswith("a"):
                if lot not in machine_of:
                    add("LOT", f"lote {lot} deliver alpha sin PROCESS")
                    continue
                machine = machine_of[lot]
                if event["location"] != f"a{machine + 1}":
                    add("LOT", f"lote {lot} deliver alpha en maquina incorrecta")
                alpha_delivery[lot] = event["start"]
            else:
                add("LOT", f"lote {lot} deliver en ubicacion invalida")

    for lot in range(1, n + 1):
        if lot not in inbound_pickup:
            add("LOT", f"lote {lot} sin pickup en o")
        if lot not in alpha_delivery:
            add("LOT", f"lote {lot} sin entrega en alpha")
        if lot not in beta_pickup:
            add("LOT", f"lote {lot} sin pickup en beta")
        if lot not in final_delivery:
            add("LOT", f"lote {lot} sin entrega final")
        if lot in process_start and lot in alpha_delivery:
            if alpha_delivery[lot] > process_start[lot] + TOL:
                add("BUFFER",
                    f"lote {lot} PROCESS at {process_start[lot]:.12f} before alpha "
                    f"delivery at {alpha_delivery[lot]:.12f} "
                    f"(diff {process_start[lot] - alpha_delivery[lot]:.3e})")
        if lot in process_end and lot in beta_pickup:
            if beta_pickup[lot] < process_end[lot] - TOL:
                add("BUFFER",
                    f"lote {lot} beta PICKUP at {beta_pickup[lot]:.12f} before "
                    f"process end at {process_end[lot]:.12f} "
                    f"(diff {beta_pickup[lot] - process_end[lot]:.3e})")

    for machine, processes in process_by_machine.items():
        processes.sort()
        for i in range(1, len(processes) - 1):
            previous_lot = processes[i - 1][2]
            if previous_lot not in beta_pickup:
                continue
            if (beta_pickup[previous_lot] > processes[i][1] + TOL and
                    processes[i + 1][0] < beta_pickup[previous_lot] - TOL):
                add("BLOCKING",
                    f"M{machine + 1} starts lot {processes[i + 1][2]} at "
                    f"{processes[i + 1][0]:.12f} before unblock at "
                    f"{beta_pickup[previous_lot]:.12f}")

    return errors


def main():
    root = Path(__file__).resolve().parent.parent
    output = root / "diagnostics" / "diagnostico.txt"
    output.parent.mkdir(exist_ok=True)
    lines = []
    total = 0
    for instance_path in sorted((root / "instances").glob("*.io1")):
        txt = root / "submissions" / f"{instance_path.stem}.txt"
        lines.append(f"=== {instance_path.stem} ===")
        try:
            instance = load_instance(instance_path)
            events = parse_txt(txt, instance)
            errors = diagnose(instance, events)
        except Exception as error:
            lines.append(f"ERROR DE FORMATO: {error}")
            lines.append("")
            total += 1
            continue
        if not errors:
            lines.append("OK")
        else:
            counts = defaultdict(int)
            for kind, _ in errors:
                counts[kind] += 1
            lines.append("conteo: " + ", ".join(f"{kind}={counts[kind]}" for kind in counts))
            for number, (kind, message) in enumerate(errors[:40], 1):
                lines.append(f"{number:02d}. [{kind}] {message}")
            if len(errors) > 40:
                lines.append(f"... {len(errors) - 40} errores adicionales")
            total += len(errors)
        lines.append("")
    lines.append(f"TOTAL_ERRORES={total}")
    output.write_text("\n".join(lines), encoding="utf-8")
    print(output)
    print(f"errores={total}")


if __name__ == "__main__":
    sys.exit(main())
