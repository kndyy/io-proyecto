#!/usr/bin/env python3
import csv
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BINARY = ROOT / "io1"
OUTPUT = ROOT / "submissions"
VALIDATOR = ROOT / "tools" / "validate_txt.py"

CONFIGS = {
    "instancia_01": (30000, 1000, 20261106),
    "instancia_02": (30000, 1000, 20261106),
    "instancia_03": (30000, 1000, 20261106),
    "instancia_04": (30000, 1000, 20261106),
    "instancia_05": (30000, 1000, 12345),
    "instancia_06": (30000, 1000, 9001),
    "instancia_07": (30000, 1000, 3),
    "instancia_08": (30000, 1000, 20261106),
    "instancia_09": (30000, 1000, 42),
    "instancia_10": (30000, 1000, 20261106),
}

EXAMPLE = """recurso;orden;accion;lote;ubicacion;destino;inicio
M1;1;PROCESS;1;;;10
AGV1;1;PICKUP;1;o;;0
AGV1;2;MOVE;;o;a1;0
AGV1;3;DELIVER;1;a1;;10
AGV1;4;MOVE;;a1;b1;10
AGV1;5;PICKUP;1;b1;;20
AGV1;6;MOVE;;b1;f;20
AGV1;7;DELIVER;1;f;;30
"""


def build():
    subprocess.run([
        "g++", "-std=c++20", "-O2", "-Wall", "-Wextra", "-Wpedantic",
        "main.cpp", "-o", "io1",
    ], cwd=ROOT, check=True)


def main():
    if not BINARY.exists():
        build()
    OUTPUT.mkdir(exist_ok=True)
    (OUTPUT / "ejemplo_minimo.txt").write_text(EXAMPLE, encoding="utf-8")

    rows = []
    for instance in sorted((ROOT / "instances").glob("*.io1")):
        max_evals, max_neighbors, seed = CONFIGS[instance.stem]
        target = OUTPUT / f"{instance.stem}.txt"
        warmstart = ROOT / "warmstart" / f"{instance.stem}.txt"
        completed = subprocess.run([
            str(BINARY), "--input", str(instance), "--strategy", "vnd",
            "--from-txt", str(warmstart),
            "--max-evals", str(max_evals), "--max-neighbors", str(max_neighbors),
            "--seed", str(seed), "--txt", str(target),
        ], cwd=ROOT, capture_output=True, text=True)
        if completed.returncode != 0:
            print(completed.stderr, file=sys.stderr)
            raise SystemExit(f"fallo al generar {instance.stem}")
        match = re.search(r"mean=([0-9.]+)", completed.stdout)
        expected = float(match.group(1))
        checked = subprocess.run([
            sys.executable, str(VALIDATOR), str(instance), str(target),
        ], cwd=ROOT, capture_output=True, text=True)
        if checked.returncode != 0:
            print(checked.stdout, checked.stderr, file=sys.stderr)
            raise SystemExit(f"validacion fallida {instance.stem}")
        validated = float(re.search(r"mean=([0-9.]+)", checked.stdout).group(1))
        if abs(validated - expected) > 1e-5:
            raise SystemExit(f"media distinta {instance.stem}: {expected} vs {validated}")
        rows.append({
            "instance": instance.stem,
            "mean": f"{expected:.9f}",
            "validated_mean": f"{validated:.9f}",
            "max_evals": max_evals,
            "max_neighbors": max_neighbors,
            "seed": seed,
            "warmstart": str(warmstart.relative_to(ROOT)),
            "txt": str(target.relative_to(ROOT)),
        })
        print(f"{instance.stem}: mean={expected:.6f}")

    with (OUTPUT / "manifest.csv").open("w", newline="") as file:
        writer = csv.DictWriter(file, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)
    print(f"escritos {len(rows)} envios en {OUTPUT}")


if __name__ == "__main__":
    main()
