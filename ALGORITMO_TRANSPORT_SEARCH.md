# Direct transport search: model, algorithm, and qualified results

## 1. What is established

The published portfolio consists of ten complete, locally validated schedules in
`proposed_submissions/transport_search/`. Every candidate improves on the
previously accepted TXT objective. Six meet the user's target of at most
`1.01 × public reference` from the supplied screenshot.

**These are best-known heuristic results, not certified near-optimal solutions.**
They have not been submitted to IO1Lab. The public references are comparison
values, not proven optima. Plateaus in a neighborhood do not prove that only
marginal improvements remain: new constructions produced substantial gains
after earlier local searches had plateaued.

| Instance | Accepted TXT | Published candidate | Reduction vs accepted | Screenshot reference | Gap vs reference | Within 1% |
|---|---:|---:|---:|---:|---:|:---:|
| 01 | 337.409091 | 327.954546 | 2.80% | 326.6046 | 0.413% | Yes |
| 02 | 438.204410 | 407.247295 | 7.06% | 403.3484 | 0.967% | Yes |
| 03 | 520.894694 | 476.914535 | 8.44% | 473.0357 | 0.820% | Yes |
| 04 | 565.953301 | 528.676852 | 6.59% | 521.4219 | 1.391% | No |
| 05 | 654.515422 | 594.535453 | 9.16% | 590.3339 | 0.712% | Yes |
| 06 | 881.811905 | 752.169789 | 14.70% | 741.3287 | 1.462% | No |
| 07 | 1073.423397 | 888.025966 | 17.27% | 877.8130 | 1.163% | No |
| 08 | 1294.215185 | 1071.447522 | 17.21% | 1065.6438 | 0.545% | Yes |
| 09 | 1273.710443 | 996.341333 | 21.78% | 952.3094 | 4.624% | No |
| 10 | 1300.108288 | 976.200167 | 24.91% | 969.7967 | 0.660% | Yes |

All means and gaps are recomputed by `tools/report_transport.py` from the saved
TXT files. The authoritative full-precision data, hashes, and target flags are
in `results/transport_search.csv`; screenshot values are in
`results/public_targets.csv`. Remaining shortfalls to the 1% cutoffs are about
2.041, 3.428, 1.435, and 34.509 seconds on 04, 06, 07, and 09 respectively.

## 2. Why the representation changed

The previous search stored machine sequences and dispatcher settings. Every
evaluation greedily rebuilt all AGV routes, losing the ability to retain and
improve a complete vehicle schedule.

`transport-search` stores:

1. a compatible, ordered lot sequence for each machine;
2. an ordered pickup/delivery action sequence for each AGV.

Timing is calculated from those decisions by a precedence graph. The search
therefore changes transport requests, route composition, vehicle allocation,
and machine decisions directly. The application uses C++20 and the standard
library only. Python tools perform benchmarking, reporting, and independent
validation, not production optimization.

## 3. Physical model and timing

For each lot, create vertices for inbound pickup `I`, alpha delivery `A`, process
start `S`, beta pickup `B`, and final delivery `F`. Check each lot occurs once
on a compatible machine, each transport request is paired on one vehicle, and
the vehicle load remains between zero and capacity.

For consecutive vehicle actions `u,v`, add:

`t[v] >= t[u] + tau(location(u), location(v)) + guard`.

The first action includes travel from `o`. For a machine sequence numbered from
zero, add the following constraints whenever the referenced predecessor exists:

| Constraint | Meaning |
|---|---|
| `S[i] >= A[i] + guard` | Input has arrived before processing. |
| `B[i] >= S[i] + p[i] + guard` | Processing finishes before output pickup. |
| `S[i] >= S[i-1] + p[i-1] + guard` | The machine does not process two lots at once. |
| `A[i] >= S[i-1] + guard` | The previous input has left the unit alpha buffer. |
| `B[i] >= B[i-1] + guard` | Outputs leave beta in processing order. |
| `S[i] >= B[i-2] + guard` | A completed lot is not still blocking the machine behind a full beta buffer. |

The final constraint permits one lot in beta and a second completed lot held
inside the machine, matching the original simulator. It does not require every
output to be picked up before the next process starts.

A topological pass rejects cycles/deadlocks and computes earliest times by
longest paths. Earliest timing minimizes mean final delivery **for this fixed
plan**; it does not optimize all possible plans. `RELEASE_GUARD=1e-8` and the
existing 12-decimal TXT writer are retained.

## 4. Construction and improvement

### Starting schedules

- **Phase warm start:** retain all machine and vehicle decisions from a prior
  feasible TXT, or generate a fresh `phase-bucket` schedule.
- **Slot assignment:** a rectangular Hungarian matching assigns lots to
  compatible machine positions. A slot of reverse rank `r` has cost
  `r × (p[i,j] + w × travel[j]/C) + travel[j]`. Sorting by slot rank gives the
  machine sequence; the travel weight controls the processing/logistics tradeoff.
- **Mixed milk runs:** feed machines and collect earlier outputs on the same
  visit. Mode 1 collects the preceding output, using earliest-available vehicles.
  Mode 6 adaptively collects ready outputs, forces pickups needed to avoid
  machine blocking, and compares bounded visit sequences across vehicles.
  SPT sorting can precede reconstruction. Modes 2–5 and scoring profiles are
  retained as experimental construction variants.

The processing-only slot model, with travel terms removed, gives a lower bound
for a relaxed unrelated-machine problem. It omits transport and buffers and is
too loose to certify near-optimality here: on 09 it is approximately 766.443,
versus the published feasible mean of 996.341. The screenshot gap is a different
quantity and must not be presented as a proven optimality gap.

The slot construction follows from the identity
`sum(C[r]) = sum((k-r+1) * p[r])` for `k` consecutively processed jobs on one
machine. A matching assigns each lot to one compatible reverse-rank slot;
positive costs fill the lowest ranks, and sorting those slots reconstructs the
relaxed processing schedule. Adding travel penalties changes this into an
initialization heuristic, rather than an exact model of the logistics problem.

### Direct search

The base search uses action relocations/swaps, paired request relocations,
request exchanges, compatible lot exchanges, machine moves, and empty-boundary
vehicle excursion/suffix exchanges. Simulated annealing explores worse plans;
periodic restarts return to the best incumbent. Only the best complete feasible
plan is exported.

Optional intensification used during the campaign includes:

- **Assignment matching:** near-critical graph sensitivities and sequence ranks
  guide compatible whole-lot permutations, checked against the real objective.
- **Request scans:** evaluate a broad capacity-feasible paired insertion
  neighborhood, capped at 20,000 or the requested repair width and subject to a
  timing filter. This is not an exhaustive search over all complete schedules.
- **Ruin/repair and whole-job moves:** temporarily remove complete requests,
  sometimes reassign jobs and release neighboring input requests, then repair
  using exact graph evaluations of shortlisted insertions.
- **Window DP:** reorder up to 16 actions using capacity, pairing, and estimated
  release/deadline constraints, then verify the result in the full graph.
- **Fractional assignment experiment:** mix compatible assignments using a
  conditional-gradient heuristic and randomized rounding. This is an optional
  intensifier, not an optimality certificate for the published schedules.

Partial/fractional plans exist only inside repair/assignment calculations. They
cannot be exported as feasible schedules: final export rechecks the full plan
with the actual lot durations and all requests present.

## 5. What the experiments support

A paired one-million-evaluation comparison started from identical accepted TXT
files, with seed 20261109, temperature 0.5, and 50k cooling cycles:

| Instance | Direct routes, machines fixed | Joint routes and machines |
|---|---:|---:|
| 09 | 1155.182494 | 1072.653206 |
| 10 | 1180.446695 | 1097.687544 |

Both passed independent buffer replay. This demonstrates improvements from
direct transport optimization even with machine decisions frozen. Joint search
improved further in these paired runs. The single-seed comparison is not a
statistical estimate across seeds. See `results/transport_ablation.csv`.

SPT, mixed-route reconstruction, and alternative slot starts subsequently
produced further gains. Earlier Q0/K1 experiments only tested restricted
dispatcher changes and did not rule out this representation. Similarly, the
poor earlier mixed-route decoder did not establish that mixed routes are bad.
High utilization, empty share, or summed machine tail alone is not a causal
bottleneck proof.

The final table is the best-of-campaign portfolio. It uses different starts,
seeds, budgets, and continuations per instance. **It is not a single-seed,
equal-budget algorithm comparison.** The implementation evolved during the
campaign, and ignored intermediate warm starts are not available in a fresh
clone. `results/transport_settings.csv` describes the selected histories and
last successful settings; `results/transport_portfolio.csv` records promotions.
The complete published TXT files make feasibility and objective claims directly
reproducible independently of those intermediate search trajectories.

## 6. Build, validate, and reproduce

```bash
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic main.cpp -o io1
./io1 --self-test
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -I. tools/test_transport.cpp -o /tmp/test-transport
/tmp/test-transport
python3 tools/report_transport.py --check
```

The last command validates all ten baselines and candidates, independently
replays buffers, recomputes metrics and hashes, and checks the committed report.
It reports target coverage without pretending all ten targets were achieved.
The same checks run in `.github/workflows/transport-checks.yml`.

A complete cold-start example, corresponding to the selected 03 construction:

```bash
./io1 --input instances/instancia_03.io1 --strategy transport-search \
  --transport-start slots --transport-start-weight 1 --seed 202 \
  --max-evals 1500000 --transport-temperature 0.3 \
  --transport-assignment --transport-window 12 --txt /tmp/transport_03.txt
python3 tools/validate_transport.py instances/instancia_03.io1 /tmp/transport_03.txt
```

An example continuation from a published incumbent:

```bash
./io1 --input instances/instancia_09.io1 --strategy transport-search \
  --from-txt proposed_submissions/transport_search/instancia_09.txt \
  --seed 20261123 --max-evals 1000000 --transport-temperature 0.3 \
  --transport-assignment --transport-request-scan --txt /tmp/transport_09_next.txt
python3 tools/validate_transport.py instances/instancia_09.io1 /tmp/transport_09_next.txt
```

`tools/target_campaign.py` runs independent portfolio trials with a bounded
worker pool; `tools/bench_transport.py` runs sequential incumbent continuations.
They record source/binary hashes, settings, timing, traces, and validated
objectives under an explicit output directory. `tools/merge_transport.py`
promotes only validated improvements and archives the replaced incumbent locally.

`--max-evals` is an operational search budget, not a common CPU budget across
strategies. It counts candidate/repair/relaxation evaluations, including
infeasible trials; final verification and state-refresh evaluations are not all
charged. The cold-start total also includes seed construction. Matching and DP
work differ in cost from ordinary graph passes. The CLI's historical `cpu_ms`
field measures elapsed wall time, including parallel contention.

Dispatcher reservation-starvation and homogeneous phase-trip/fill statistics
do not apply to arbitrary mixed direct routes. They are omitted from the new
strategy's CLI or marked unavailable in debug CSVs. Process waits, actual
travel, blocking, and completion metrics remain available.

## 7. Code map and references

- `search_types.hpp`: shared search options and outcomes, without a circular
  dependency between search implementations.
- `transport.hpp`: plan parsing/construction, `TransportEvaluator`, direct
  neighborhoods, matching/repair/DP intensifiers, and `solveTransport`.
- `heuristic.hpp`: historical strategies and the cold-start strategy adapter.
- `tools/validate_transport.py`: existing TXT checks plus independent
  chronological replay of machine processing and buffer occupancy.
- `tools/test_transport.cpp`: hand-calculated timing, capacity, deadlocks,
  blocking, request completeness, deterministic search, and budget checks.

Method references:

- [Job-shop scheduling and disjunctive-graph representation](https://en.wikipedia.org/wiki/Job-shop_scheduling#Problem_representation).
- [Pickup/delivery constraints: pairing, same vehicle, and precedence](https://developers.google.com/optimization/routing/pickup_delivery).

These informed the representation. No external optimization library is used.
