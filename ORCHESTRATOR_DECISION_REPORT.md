# Orchestrator decision report — IO1

## Repository state

- Repository: https://github.com/kndyy/io-proyecto
- Branch: `main`
- Default strategy: `phase-bucket`
- Accepted TXT files and `selector.conf`: unchanged
- No IO1Lab submission was made by the experiments in this report
- No scheduled monitoring or background workflow is enabled

## Executive summary

`phase-bucket` is the strongest implemented and accepted strategy. Every later
optimization attempt failed its predeclared gate:

- `phase-joint` (bounded route enumeration plus joint AGV assignment): worse on
  fixed schedules and 8–12× runtime.
- `alns-joint` (mixed AGV routes): worse on 02–10.
- `phase-learned` (CEM dispatch weights): no improvement after fixing the global
  score bug and repairing the experiment.
- Q0 counterfactual oracle: no consistent single-action signal on 09/10.
- K1 transport-key ablation: no signal on 09 and one seed on 10.
- First-dispatch rollout: no improvement; full state-clone rollout was not
  authorized because Q0 failed.

The measured loss is structural: AGV transport is saturated, empty travel is
33–38% of AGV travel, output batches are underfilled, and machines starve while
waiting for transport. Local dispatch changes do not fix this.

## Accepted scores versus public snapshot

| Inst. | Accepted | Public best | Gap |
|---|---:|---:|---:|
| 01 | 337.4091 | 326.6046 | +3.31% |
| 02 | 438.2044 | 404.0021 | +8.47% |
| 03 | 520.8947 | 475.9221 | +9.45% |
| 04 | 565.9533 | 525.9226 | +7.61% |
| 05 | 654.5154 | 591.3411 | +10.68% |
| 06 | 881.8119 | 754.2413 | +16.91% |
| 07 | 1073.4233 | 901.9320 | +19.01% |
| 08 | 1294.2152 | 1077.9824 | +20.06% |
| 09 | 1273.7104 | 958.7442 | +32.85% |
| 10 | 1300.1083 | 993.2925 | +30.89% |

The public values are external benchmarks, not proven optima. The top-30%
threshold cannot be inferred from them alone.

## Approach ledger

| Approach | Result | Verdict |
|---|---|---|
| `b1`, `b2`, `b3` | Baselines; far from best | Rejected as final |
| `s0`, `s1`, `s2` | Greedy constructions | Superseded |
| `vnd` | Relocate/swap local search | Superseded by `integrated` |
| `integrated` | ILS + VND + adaptive AGV dispatch; accepted | Control/fallback |
| `phase-bucket` | Phase-aware separate I/O dispatch, adaptive batch/bias, optional lookahead, per-instance modes | **KEEP: production** |
| `alns-joint` | Mixed I/O/return routes; only 01 improved; blocking increased | Rejected |
| `phase-joint` | Complete-route enumeration + joint assignment; worse fixed schedules; 8–12× runtime | Rejected |
| `phase-learned` | CEM weights with normalization and coordinate search; holdout ratio 1.000 | Rejected |
| Q0 oracle | 5k: 1196 valid alternatives, 5 improving; 20k: 720 valid, 0 improving | FAIL |
| K1 keys | 09: 0/5 seeds ≥0.5%; 10: 1/5 | FAIL |
| First-dispatch rollout | Zero or negative gain | Rejected/incomplete |
| Full state-clone rollout | Not implemented because Q0 failed | Not authorized |
| Full transport-key memetic | Not implemented because K1 failed | Not authorized |

## Detailed bottleneck diagnosis

The accepted configurations were re-run with the full debug reports enabled. The
reported means matched the accepted platform values exactly. Machine idle is
decomposed as: no AGV assigned (`starved`) + input reserved and in transit +
output blocking + post-processing tail.

| Inst. | Mean | Makespan | Mach. util. | AGV util. | Empty share | Starved | Input in transit | Blocked | Tail |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 01 | 337.4 | 706 | 77.8% | 69.7% | 37.3% | 4 | 64 | 0 | 559 |
| 02 | 438.2 | 806 | 87.8% | 90.9% | 33.2% | 60 | 110 | 0 | 321 |
| 03 | 520.9 | 1031 | 81.7% | 88.9% | 29.8% | 118 | 156 | 0 | 860 |
| 04 | 566.0 | 1112 | 90.2% | 88.6% | 35.2% | 59 | 172 | 5 | 528 |
| 05 | 654.5 | 1404 | 83.9% | 87.0% | 33.2% | 65 | 216 | 6 | 1525 |
| 06 | 881.8 | 1730 | 80.9% | 92.2% | 37.7% | 784 | 322 | 213 | 1320 |
| 07 | 1073.4 | 2123 | 72.9% | 91.8% | 38.0% | 1706 | 1114 | 342 | 2019 |
| 08 | 1294.2 | 2564 | 67.4% | 94.4% | 38.1% | 2292 | 1874 | 601 | 3598 |
| 09 | 1273.7 | 2513 | 75.8% | 95.1% | 33.0% | 1932 | 769 | 1009 | 2970 |
| 10 | 1300.1 | 2612 | 74.9% | 94.7% | 33.1% | 3577 | 1217 | 244 | 2816 |

`tail` is summed machine idle after each machine’s last process. `starved`
means idle with pending work and no AGV reservation; `input in transit` means
idle while an input is reserved and being delivered.

### Where time is lost

1. AGVs are the binding resource on 06–10. AGV travel utilization is 92–95%.
   On 10, 8,276 s loaded plus 4,093 s empty travel occupy almost the entire
   2,612 s horizon across five AGVs.
2. Empty travel is the largest controllable overhead: 33–38% of AGV travel.
3. Output batching is underfilled: 76% on 10, 79% on 09, while input fill is
   91–99%.
4. Ten has 3,577 s of true machine starvation plus 1,217 s waiting for already
   reserved input. Nine has 1,932 s starvation plus 769 s reserved-input wait;
   Eight has 2,292 s plus 1,874 s. Transport is both late and unbalanced.
5. Blocking is concentrated: 1,009 s on 09 (M1–M4), 601 s on 08 (M1/M3), 244 s
   on 10 (mostly M3). It is not the only issue, but it is material on 09/08.
6. The average is dragged by the late tail: mean completion is about 50% of
   makespan on all instances. On 09/10 the tail is ~2,970/2,816 s.
7. Instances 01–05 are a different regime: gaps are 3–11% and AGV utilization is
   lower. Most upside is in 06–10, especially 09/10.

### Per-lot waiting and hotspots

Alpha wait is time in α before processing; beta wait is time in β before
pickup. Values are p50 / p90 / max seconds.

| Inst. | Alpha wait | Beta wait | Outbound transport |
|---|---|---|---|
| 06 | 4.5 / 93 / 141 | 43 / 111 / 221 | 29 / 42 / 67 |
| 07 | 0 / 65 / 202 | 47 / 114 / 240 | 30 / 45 / 52 |
| 08 | 0 / 58 / 173 | 54 / 124 / 274 | 29 / 45 / 67 |
| 09 | 13 / 73 / 202 | 83 / 156 / 221 | 32 / 53 / 75 |
| 10 | 0 / 80 / 144 | 45 / 96 / 216 | 37 / 59 / 75 |

The dominant lot-level delay is **waiting in β for outbound pickup**, not
waiting in α. This matches the output-fill and AGV-saturation evidence.

Starved-machine hotspots:

- 10: M12 970 s, M8 524 s, M5 508 s.
- 09: M8 532 s, M11 295 s, M5 327 s; blocked M2 261 s, M3 264 s, M4 216 s.
- 08: M8 472 s, M5 413 s, M1 163 s plus blocked M1 243 s and M3 235 s.
- 07: M8 384 s, M6 311 s, M5 256 s.
- 06: M8 283 s, M5 224 s; blocked M3 126 s.

All AGVs on 09/10 carry similar load; there is no single lazy or overloaded
vehicle. Empty travel is spread almost evenly across the fleet.

### Why local approaches failed

- One-action changes cannot restructure cumulative batching/routing; Q0 found
  almost no improving alternatives.
- Joint assignment only matters when several AGVs are free; multi-free epochs
  were only 9–23%.
- Mixed routes delayed outputs and increased blocking.
- Learned and keyed scores only rerank prefixes produced by the existing greedy
  generator; they cannot change batch construction.
- More evaluations plateaued; 500k on 09 improved only about 1.6%.

## Decision options

### A. Freeze current results

Keep `phase-bucket`, accepted TXT and selector unchanged. Zero risk, zero
expected gain. Appropriate if deadline risk dominates.

### B. Structural AGV routing and batching (recommended)

Stop tuning local dispatch scores. Change route composition and assignment
globally:

- fill output batches, especially on 09/10;
- reduce empty travel via route starts and global assignment;
- target machine starvation and concentrated blocking;
- keep the existing simulator as the exact evaluator.

This is the only direction supported by the measured bottleneck. It is high
effort and requires new gates.

### C. Matheuristic or exact subproblem

Fix machine schedules and solve a bounded AGV routing/batching subproblem with
an exact or external solver. Risk: may violate standard-library-only and
explainability constraints. Only if the course permits it.

### D. Per-instance portfolio only

Select among existing strategies/seeds/budgets. Expected gain is small, usually
under 1–2%; it does not close the 30% gaps.

### E. Continue CEM/keys/rollout

Not recommended. The controlled gates already failed.

## Recommendation

Reject further local dispatch optimization. Keep `phase-bucket` as production.
Authorize one structural transport-routing phase focused on 09/10, with 06/08 as
transfer diagnostics.

Suggested gates:

- frozen-schedule improvement at least 1.5% on one of 09/10 without regression
  on the other;
- runtime at most 4× matched baseline;
- Stage C median at least 1% on 09 and 10, no more than 1% regression on 06/08;
- only then a full-ten benchmark and accepted-TXT update.

If a structural rewrite is not authorized, freeze the current accepted results
and stop algorithmic exploration.

## Observability and reproduction

The simulator supports `--debug-dir` reports:

- `summary.csv`: objective, percentiles, machine/AGV utilization, blocking,
  starvation, trips, fill and search counters;
- `lots.csv`: pickup, alpha, process, beta and delivery times per lot;
- `machines.csv`: busy, idle, gaps, blocked, starved and tail per machine;
- `agvs.csv`: loaded/empty travel, idle time, trips and batch fill per AGV;
- `events.csv`: full event log.

Q0 oracle:

```bash
python3 tools/bench_counterfactual.py --freeze-evals 5000 \
  --output experiments/local/q0_5k_final
python3 tools/bench_counterfactual.py --freeze-evals 20000 \
  --seeds 20261109,12345,55555 --output experiments/local/q0_20k_final --skip-build
```

K1 key ablation:

```bash
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -I. tools/key_ablation.cpp \
  -o /tmp/key_ablation
/tmp/key_ablation --instance 09 --seed 20261109 --freeze-evals 5000 \
  --mode 0 --samples 120 --strength 0.75
```

The accepted configurations were re-run with `--debug-dir` and reproduced the
accepted means exactly. Per-lot, per-machine, per-AGV and event-level reports for
all ten instances are under `experiments/local/observability/` (ignored by git).
Raw Q0/K1 diagnostics are also local under `experiments/local/`.
