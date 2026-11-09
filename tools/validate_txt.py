#!/usr/bin/env python3
import math
import re
import sys
from collections import defaultdict
from pathlib import Path

HEADER = "recurso;orden;accion;lote;ubicacion;destino;inicio"
EPS = 1e-6


def load_instance(path):
    tokens = Path(path).read_text().split()
    index = 0

    def take():
        nonlocal index
        value = tokens[index]
        index += 1
        return value

    if take() != "IO1_V1":
        raise ValueError("cabecera IO1_V1")
    n = int(take())
    m = int(take())
    k = int(take())
    capacity = int(take())
    if take() != "LOTS":
        raise ValueError("seccion LOTS")
    lots = {}
    for _ in range(n):
        lot = int(take())
        kind = take()
        quantity = float(take())
        lots[lot] = (kind, quantity)
    if take() != "RATES":
        raise ValueError("seccion RATES")
    rates = defaultdict(dict)
    for _ in range(int(take())):
        machine = int(take())
        kind = take()
        value = float(take())
        rates[machine][kind] = value
    if take() != "LOCATIONS":
        raise ValueError("seccion LOCATIONS")
    locations = int(take())
    names = [take() for _ in range(locations)]
    if take() != "TIMES":
        raise ValueError("seccion TIMES")
    tau = [[float(take()) for _ in range(locations)] for _ in range(locations)]

    o = names.index("o")
    f = names.index("f")
    alpha = [names.index(f"alpha_{j}") for j in range(1, m + 1)]
    beta = [names.index(f"beta_{j}") for j in range(1, m + 1)]
    speed = [[rates[j + 1].get(lots[i + 1][0], 0.0) for j in range(m)] for i in range(n)]
    return {
        "n": n, "m": m, "k": k, "capacity": capacity, "lots": lots,
        "alpha": alpha, "beta": beta, "o": o, "f": f, "tau": tau,
        "speed": speed, "names": names,
    }


def canonical_locations(instance):
    values = {"o", "f"}
    for j in range(1, instance["m"] + 1):
        values.add(f"a{j}")
        values.add(f"b{j}")
    return values


def parse_location(name, instance):
    if name not in canonical_locations(instance):
        raise ValueError(f"ubicacion no canonica: {name}")
    return name


def location_index(name, instance):
    if name == "o":
        return instance["o"]
    if name == "f":
        return instance["f"]
    if name.startswith("a"):
        return instance["alpha"][int(name[1:]) - 1]
    if name.startswith("b"):
        return instance["beta"][int(name[1:]) - 1]
    raise ValueError(f"ubicacion desconocida: {name}")


def parse_txt(path, instance):
    lines = [line.strip() for line in Path(path).read_text(encoding="utf-8").splitlines() if line.strip()]
    if not lines or lines[0] != HEADER:
        raise ValueError("encabezado incorrecto")
    events = []
    seen = set()
    for line in lines[1:]:
        parts = line.split(";")
        if len(parts) != 7:
            raise ValueError(f"columnas incorrectas: {line}")
        resource, order, action, lot, location, destination, start = parts
        if not re.fullmatch(r"[1-9][0-9]*", order):
            raise ValueError(f"orden invalido: {line}")
        order = int(order)
        start = float(start)
        if not math.isfinite(start) or start < 0:
            raise ValueError(f"inicio invalido: {line}")
        if (resource, start, order) in seen:
            raise ValueError(f"terna repetida: {line}")
        seen.add((resource, start, order))
        if action not in {"PROCESS", "PICKUP", "DELIVER", "MOVE"}:
            raise ValueError(f"accion invalida: {line}")
        machine_match = re.fullmatch(r"M([1-9][0-9]*)", resource)
        agv_match = re.fullmatch(r"AGV([1-9][0-9]*)", resource)
        if action == "PROCESS":
            if not machine_match or not lot or location or destination:
                raise ValueError(f"PROCESS invalido: {line}")
            machine = int(machine_match.group(1))
            if machine > instance["m"]:
                raise ValueError(f"maquina invalida: {line}")
            events.append({"resource": resource, "order": order, "action": action,
                           "lot": int(lot), "location": None, "destination": None,
                           "start": start, "machine": machine, "agv": None})
        elif action == "MOVE":
            if not agv_match or lot or not location or not destination:
                raise ValueError(f"MOVE invalido: {line}")
            agv = int(agv_match.group(1))
            if agv > instance["k"]:
                raise ValueError(f"AGV invalido: {line}")
            parse_location(location, instance)
            parse_location(destination, instance)
            events.append({"resource": resource, "order": order, "action": action,
                           "lot": None, "location": location, "destination": destination,
                           "start": start, "machine": None, "agv": agv})
        else:
            if not agv_match or not lot or not location or destination:
                raise ValueError(f"accion AGV invalida: {line}")
            agv = int(agv_match.group(1))
            if agv > instance["k"]:
                raise ValueError(f"AGV invalido: {line}")
            lot = int(lot)
            if lot < 1 or lot > instance["n"]:
                raise ValueError(f"lote invalido: {line}")
            parse_location(location, instance)
            events.append({"resource": resource, "order": order, "action": action,
                           "lot": lot, "location": location, "destination": None,
                           "start": start, "machine": None, "agv": agv})
    return events


def validate(instance, events):
    n = instance["n"]
    m = instance["m"]
    k = instance["k"]
    capacity = instance["capacity"]
    machine_of = {}
    process_start = {}
    process_end = {}
    process_by_machine = defaultdict(list)

    for event in events:
        if event["action"] != "PROCESS":
            continue
        lot = event["lot"]
        machine = event["machine"] - 1
        if lot < 1 or lot > n:
            raise ValueError(f"lote invalido en PROCESS: {lot}")
        if lot in machine_of:
            raise ValueError(f"lote procesado mas de una vez: {lot}")
        speed = instance["speed"][lot - 1][machine]
        if speed <= 0:
            raise ValueError(f"lote {lot} incompatible con M{machine + 1}")
        duration = instance["lots"][lot][1] / speed
        end = event["start"] + duration
        machine_of[lot] = machine
        process_start[lot] = event["start"]
        process_end[lot] = end
        process_by_machine[machine].append((event["start"], end, lot))

    if len(machine_of) != n:
        raise ValueError(f"faltan procesos: {len(machine_of)} de {n}")

    for machine, processes in process_by_machine.items():
        processes.sort()
        previous_end = 0.0
        for start, end, lot in processes:
            if start < previous_end - EPS:
                raise ValueError(f"solape en M{machine + 1} lote {lot}")
            previous_end = end

    alpha_delivery = {}
    beta_pickup = {}
    final_delivery = {}
    inbound_pickup = {}
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
            if start < busy_until - EPS:
                raise ValueError(f"accion durante viaje AGV{agv}: {event}")
            action = event["action"]
            if action == "MOVE":
                origin = location_index(event["location"], instance)
                destination = location_index(event["destination"], instance)
                if origin != position:
                    raise ValueError(f"origen incorrecto AGV{agv}: {event}")
                busy_until = start + instance["tau"][origin][destination]
                position = destination
            elif action == "PICKUP":
                if location_index(event["location"], instance) != position:
                    raise ValueError(f"pickup fuera de posicion AGV{agv}: {event}")
                lot = event["lot"]
                if lot in load:
                    raise ValueError(f"pickup duplicado AGV{agv}: {event}")
                if len(load) >= capacity:
                    raise ValueError(f"capacidad excedida AGV{agv}: {event}")
                load.append(lot)
            else:
                if location_index(event["location"], instance) != position:
                    raise ValueError(f"deliver fuera de posicion AGV{agv}: {event}")
                lot = event["lot"]
                if lot not in load:
                    raise ValueError(f"deliver sin carga AGV{agv}: {event}")
                load.remove(lot)
        if load:
            raise ValueError(f"AGV{agv} termina con carga: {load}")

    for event in events:
        if event["action"] in {"PROCESS", "MOVE"}:
            continue
        lot = event["lot"]
        if event["action"] == "PICKUP":
            if event["location"] == "o":
                if lot in inbound_pickup:
                    raise ValueError(f"pickup inicial duplicado: {event}")
                inbound_pickup[lot] = event["start"]
            elif event["location"].startswith("b"):
                machine = machine_of[lot]
                if event["location"] != f"b{machine + 1}":
                    raise ValueError(f"pickup de salida incorrecto: {event}")
                if lot in beta_pickup:
                    raise ValueError(f"pickup de salida duplicado: {event}")
                beta_pickup[lot] = event["start"]
            else:
                raise ValueError(f"pickup en ubicacion invalida: {event}")
        else:
            if event["location"] == "f":
                if lot in final_delivery:
                    raise ValueError(f"entrega final duplicada: {event}")
                final_delivery[lot] = event["start"]
            elif event["location"].startswith("a"):
                machine = machine_of[lot]
                if event["location"] != f"a{machine + 1}":
                    raise ValueError(f"deliver de entrada incorrecto: {event}")
                if lot in alpha_delivery:
                    raise ValueError(f"deliver de entrada duplicado: {event}")
                alpha_delivery[lot] = event["start"]
            else:
                raise ValueError(f"deliver en ubicacion invalida: {event}")

    for lot in range(1, n + 1):
        if lot not in alpha_delivery:
            raise ValueError(f"lote {lot} sin entrega en alpha")
        if lot not in beta_pickup:
            raise ValueError(f"lote {lot} sin pickup en beta")
        if lot not in final_delivery:
            raise ValueError(f"lote {lot} sin entrega final")
        if alpha_delivery[lot] > process_start[lot] + EPS:
            raise ValueError(f"lote {lot} procesado antes de alpha")
        if beta_pickup[lot] < process_end[lot] - EPS:
            raise ValueError(f"lote {lot} recogido antes de terminar")

    for machine, processes in process_by_machine.items():
        processes.sort()
        deliveries = sorted((alpha_delivery[lot], lot) for _, _, lot in processes)
        for (_, previous_lot), (delivery, lot) in zip(deliveries, deliveries[1:]):
            if process_start[previous_lot] > delivery + EPS:
                raise ValueError(f"alpha ocupado en M{machine + 1}: lote {lot}")
        for i in range(1, len(processes) - 1):
            previous_lot = processes[i - 1][2]
            if (beta_pickup[previous_lot] > processes[i][1] + EPS and
                    processes[i + 1][0] < beta_pickup[previous_lot] - EPS):
                raise ValueError(f"maquina M{machine + 1} avanzó bloqueada")
        for start, end, lot in processes:
            if beta_pickup[lot] < end - EPS:
                raise ValueError(f"lote {lot} recogido antes de bloquear beta")

    mean = sum(final_delivery.values()) / n
    return mean


def main():
    if len(sys.argv) != 3:
        print("uso: validate_txt.py <instancia.io1> <envio.txt>")
        raise SystemExit(1)
    instance = load_instance(sys.argv[1])
    events = parse_txt(sys.argv[2], instance)
    mean = validate(instance, events)
    print(f"OK mean={mean:.9f} events={len(events)}")


if __name__ == "__main__":
    main()
