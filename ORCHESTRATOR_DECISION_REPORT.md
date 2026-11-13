# Decision report: publish the qualified transport portfolio

## Decision

Publish the ten best-known, locally validated candidate schedules and the
direct-search implementation. Describe them as a heuristic portfolio, not as
certified near-optimal solutions. The user explicitly approved publication with
the remaining gaps disclosed.

## Current evidence

- All ten candidates improve on the previously accepted TXT objectives, by
  **2.80% to 24.91%**.
- **6/10** are within 1% of the public values in the supplied screenshot:
  **01, 02, 03, 05, 08, 10**.
- Remaining reference gaps: **04 1.391%, 06 1.462%, 07 1.163%, 09 4.624%**.
- Both the existing TXT checks and an independent chronological machine/buffer
  replay pass for all ten candidates.
- No new IO1Lab acceptance or leaderboard position is claimed.

The authoritative machine-readable evidence is `results/transport_search.csv`,
including full-precision objectives, hashes, and target flags. The target is
`candidate_mean <= 1.01 * screenshot_public_mean`.

## Explanation of the improvement

The previous representation stored machine sequences and regenerated vehicle
routes greedily at every evaluation. The new representation retains complete
AGV action sequences and optimizes them directly with a buffer-aware precedence
graph. Slot-assignment starts, shortest-processing-time sorting, and mixed
milk-run reconstructions diversify the global schedule structure.

A controlled paired test demonstrated substantial direct-route gains with
machine decisions frozen; joint route/machine optimization improved further.
Later local intensifiers helped some instances, but the largest additional
gains often came from new constructions after a local search had plateaued.

## Limits of the claim

There is no tight lower bound proving near-optimality. Screenshot values are
public references, not certified optima. A processing-only relaxation exists
but is too loose to close the optimality gap. Local plateaus do not establish
that all remaining improvements are marginal.

The final results combine offline trials with unequal budgets and different
per-instance histories. They are not an equal-budget strategy comparison.
Intermediate development-stage warm starts remain local and ignored by git;
the published complete TXT files allow independent reproduction of feasibility
and objectives. Selected settings and promotion history are documented in
`results/transport_settings.csv` and `results/transport_portfolio.csv`.

## Reproduce the published evidence

```bash
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic main.cpp -o io1
./io1 --self-test
python3 tools/report_transport.py --check
```

See `ALGORITMO_TRANSPORT_SEARCH.md` for the physical constraints, search methods,
controlled comparison, tests, and cold-start/continuation examples. Earlier
dispatcher-only diagnoses are historical and have been superseded by these
measured direct-schedule results.
