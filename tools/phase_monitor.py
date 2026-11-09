import argparse
import csv
import pathlib
import re
import subprocess
import sys

REFERENCE = [351.0, 450.3642, 566.7783, 591.5508, 716.0324,
             998.9354, 1207.7515, 1426.5020, 1477.9573, 1488.6675]


def run(binary, number, strategy, seed, evaluations, neighbors, directory):
    name = f"instancia_{number:02d}"
    output = directory / f"{name}_{strategy}.txt"
    command = [binary, "--input", f"instances/{name}.io1",
               "--strategy", strategy, "--seed", str(seed),
               "--max-evals", str(evaluations), "--max-neighbors",
               str(neighbors), "--txt", str(output)]
    result = subprocess.run(command, capture_output=True, text=True, check=True)
    match = re.search(r"feasible=1 mean=([0-9.]+).*cpu_ms=([0-9.]+)", result.stdout)
    if not match:
        raise RuntimeError(f"{name} {strategy}: {result.stdout} {result.stderr}")
    checked = subprocess.run([sys.executable, "tools/validate_txt.py",
                              f"instances/{name}.io1", str(output)],
                             capture_output=True, text=True)
    if checked.returncode != 0:
        raise RuntimeError(f"{name} {strategy}: {checked.stdout} {checked.stderr}")
    metrics = {}
    for key in ("blocked_time", "idle_time", "empty_travel",
                "loaded_travel", "phase_input_trips", "phase_output_trips"):
        value = re.search(rf"{key}=([0-9.]+)", result.stdout)
        metrics[key] = float(value.group(1)) if value else 0.0
    return float(match.group(1)), float(match.group(2)), metrics


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", default="./io1")
    parser.add_argument("--evals", type=int, default=2500)
    parser.add_argument("--neighbors", type=int, default=35)
    parser.add_argument("--seed", type=int, default=20261109)
    parser.add_argument("--only", default="01,02,03,04,05,06,07,08,09,10")
    parser.add_argument("--out", default="monitor_output")
    args = parser.parse_args()
    directory = pathlib.Path(args.out)
    directory.mkdir(parents=True, exist_ok=True)
    instances = [int(x) for x in args.only.split(",")]
    rows = []
    for number in instances:
        scores = {}
        for strategy in ("integrated", "phase-bucket"):
            scores[strategy] = run(args.binary, number, strategy, args.seed,
                                   args.evals, args.neighbors, directory)
        accepted = REFERENCE[number - 1]
        row = {"instance": f"{number:02d}", "accepted": accepted,
               "integrated": scores["integrated"][0],
               "phase_bucket": scores["phase-bucket"][0],
               "phase_cpu_ms": scores["phase-bucket"][1],
               "phase_blocked_time": scores["phase-bucket"][2]["blocked_time"],
               "phase_idle_time": scores["phase-bucket"][2]["idle_time"],
               "phase_empty_travel": scores["phase-bucket"][2]["empty_travel"],
               "phase_loaded_travel": scores["phase-bucket"][2]["loaded_travel"],
               "phase_input_trips": scores["phase-bucket"][2]["phase_input_trips"],
               "phase_output_trips": scores["phase-bucket"][2]["phase_output_trips"],
               "gain_vs_integrated_pct":
                   100 * (scores["integrated"][0] - scores["phase-bucket"][0]) /
                   scores["integrated"][0],
               "gain_vs_accepted_pct":
                   100 * (accepted - scores["phase-bucket"][0]) / accepted}
        rows.append(row)
        print(f"{number:02d} accepted={accepted:.4f} "
              f"integrated={row['integrated']:.4f} "
              f"phase={row['phase_bucket']:.4f} "
              f"gain={row['gain_vs_integrated_pct']:+.2f}%", flush=True)
    with (directory / "summary.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=rows[0].keys())
        writer.writeheader()
        writer.writerows(rows)
    wins = sum(row["phase_bucket"] < row["integrated"] - 1e-8 for row in rows)
    print(f"WIN_COUNT={wins}/{len(rows)}")


if __name__ == "__main__":
    main()
