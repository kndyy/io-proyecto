#include "transport.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace io1;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    Instance in;
    in.n = 3;
    in.m = 1;
    in.K = 2;
    in.C = 3;
    in.o = 0;
    in.f = 3;
    in.alpha = {1};
    in.beta = {2};
    in.lots = {{1, 'A', 10}, {2, 'A', 10}, {3, 'A', 10}};
    in.speed = {{1}, {1}, {1}};
    in.tau.assign(4, std::vector<long double>(4, 1));
    for (int i = 0; i < 4; ++i) in.tau[i][i] = 0;
    TransportPlan plan{{{0, 1, 2}}, {{0, 5, 10, 1, 6, 11}, {3, 8, 13, 4, 9, 14}}};
    TransportEvaluator evaluator(in);
    long double mean = evaluator.evaluate(plan);
    require(std::isfinite(mean), "basic plan rejected");
    require(std::fabs(mean - 32) < 1e-5L, "unexpected earliest timing");
    auto gradient = evaluator.durationWeights();
    require(gradient.size() == 3 && gradient[0] > 0, "critical duration weights missing");
    std::vector<long double> durations(3, 5);
    require(std::fabs(evaluator.evaluate(plan, false, &durations) - 17) < 1e-5L, "fractional timing incorrect");
    evaluator.evaluate(plan);
    auto matching = transportMatching(2, [](int row, int column) { return row == column ? 1.0L : 10.0L; });
    require(matching == std::vector<int>({0, 1}), "minimum cost matching incorrect");
    Candidate slots = buildSlotAssignment(in, 0, 0);
    require(slots.sequence[0].size() == 3, "slot construction lost lots");
    require(evaluator.times[6] >= evaluator.times[2], "alpha overwritten");
    require(evaluator.times[13] >= evaluator.times[8], "beta order violated");
    TransportPlan cyclic = plan;
    cyclic.vehicles[0] = {0, 5, 10, 6, 1, 11};
    require(!std::isfinite(evaluator.evaluate(cyclic)), "alpha deadlock accepted");
    TransportPlan reverseOutput = plan;
    reverseOutput.vehicles[1] = {8, 3, 13, 9, 4, 14};
    require(!std::isfinite(evaluator.evaluate(reverseOutput)), "beta deadlock accepted");
    TransportPlan wrongAgv = plan;
    wrongAgv.vehicles[0].pop_back();
    wrongAgv.vehicles[1].push_back(11);
    require(!std::isfinite(evaluator.evaluate(wrongAgv)), "unpaired transport accepted");
    Instance capacity = in;
    capacity.C = 2;
    TransportEvaluator capacityEvaluator(capacity);
    require(!std::isfinite(capacityEvaluator.evaluate(plan)), "capacity violation accepted");
    TransportPlan duplicate = plan;
    duplicate.vehicles[0].push_back(0);
    require(!std::isfinite(evaluator.evaluate(duplicate)), "duplicate pickup accepted");
    Options options;
    options.maxEvals = 1000;
    options.seed = 12345;
    Outcome first = solveTransport(in, plan, options);
    Outcome second = solveTransport(in, plan, options);
    require(first.result.feasible && first.result.mean <= mean + 1e-9L, "search lost its incumbent");
    require(first.evals == options.maxEvals, "evaluation budget not respected");
    require(first.result.completion == second.result.completion, "search is not reproducible");
    TransportSearchConfig fixed;
    fixed.fixedMachines = true;
    Outcome frozen = solveTransport(in, plan, options, fixed);
    require(frozen.candidate.sequence == plan.machines, "fixed-machine ablation changed machine orders");
    TransportPlan partial = plan;
    partial.vehicles[1].erase(partial.vehicles[1].begin());
    partial.vehicles[1].erase(std::find(partial.vehicles[1].begin(), partial.vehicles[1].end(), 4));
    require(std::isfinite(evaluator.evaluate(partial, true)), "partial repair state rejected");
    require(!std::isfinite(evaluator.evaluate(partial)), "partial plan exported as feasible");
    TransportSearchConfig lns;
    lns.lns = true;
    Outcome repaired = solveTransport(in, plan, options, lns);
    require(repaired.result.feasible && repaired.result.mean <= mean + 1e-9L, "ruin repair lost incumbent");
    require(repaired.evals == options.maxEvals, "ruin repair exceeded budget");
    TransportSearchConfig assignment;
    assignment.assignment = true;
    Outcome assigned = solveTransport(in, plan, options, assignment);
    require(assigned.result.feasible && assigned.result.mean <= mean + 1e-9L, "assignment search lost incumbent");
    require(assigned.evals == options.maxEvals, "assignment search exceeded budget");
    TransportSearchConfig jobs;
    jobs.jobs = true;
    Outcome relocated = solveTransport(in, plan, options, jobs);
    require(relocated.result.feasible && relocated.result.mean <= mean + 1e-9L, "job relocation lost incumbent");
    require(relocated.evals == options.maxEvals, "job relocation exceeded budget");
    TransportSearchConfig scan;
    scan.requestScan = true;
    Outcome scanned = solveTransport(in, plan, options, scan);
    require(scanned.result.feasible && scanned.result.mean <= mean + 1e-9L, "request scan lost incumbent");
    require(scanned.evals == options.maxEvals, "request scan exceeded budget");
    TransportSearchConfig fractional;
    fractional.fractional = true;
    Outcome relaxed = solveTransport(in, plan, options, fractional);
    require(relaxed.result.feasible && relaxed.result.mean <= mean + 1e-9L, "fractional assignment lost incumbent");
    require(relaxed.evals == options.maxEvals, "fractional assignment exceeded budget");
    for (int lag : {0, 1, 2}) {
        TransportPlan milk = buildMilkPlan(in, plan.machines, lag, 12345);
        require(std::isfinite(evaluator.evaluate(milk)), "milk run construction infeasible");
        milk = buildMilkPlan(in, plan.machines, lag, 12345, true);
        require(std::isfinite(evaluator.evaluate(milk)), "global milk run construction infeasible");
    }
    in.tau[0][2] = 100;
    TransportEvaluator blocked(in);
    require(std::isfinite(blocked.evaluate(plan)), "blocked plan rejected");
    require(blocked.times[12] >= blocked.times[3], "machine advanced while blocked");
    Instance windowInstance;
    windowInstance.n = 2;
    windowInstance.m = 2;
    windowInstance.K = 1;
    windowInstance.C = 2;
    windowInstance.o = 0;
    windowInstance.f = 5;
    windowInstance.alpha = {1, 3};
    windowInstance.beta = {2, 4};
    windowInstance.lots = {{1, 'A', 10}, {2, 'A', 10}};
    windowInstance.speed = {{1, 1}, {1, 1}};
    windowInstance.tau.assign(6, std::vector<long double>(6));
    for (int a = 0; a < 6; ++a) for (int b = 0; b < 6; ++b)
        windowInstance.tau[a][b] = std::abs(a - b);
    TransportPlan window{{{0}, {1}}, {{0, 5, 1, 6, 8, 3, 9, 4}}};
    TransportEvaluator windowEvaluator(windowInstance);
    long double before = windowEvaluator.evaluate(window);
    auto reference = windowEvaluator.times;
    std::mt19937_64 windowRng(12345);
    require(reorderTransportWindow(windowInstance, window, reference, windowRng, 8), "window DP missed reorder");
    require(windowEvaluator.evaluate(window) < before - 1, "window DP did not improve the inefficient route");
    std::cout << "TRANSPORT_TEST_OK\n";
}
