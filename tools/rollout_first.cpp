#include "heuristic.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace io1;

int main(int argc, char** argv) {
    int number = argc > 1 ? std::stoi(argv[1]) : 9;
    int seed = argc > 2 ? std::stoi(argv[2]) : 20261109;
    int evals = argc > 3 ? std::stoi(argv[3]) : 5000;
    std::filesystem::create_directories("experiments/local");
    std::string name = "instances/instancia_";
    if (number < 10) name += "0";
    name += std::to_string(number) + ".io1";
    Instance instance = loadInstance(name);
    Options options;
    options.seed = seed;
    options.maxEvals = evals;
    options.maxNeighbors = 120;
    options.phaseGlobal = number == 9 ? 1 : -1;
    options.phaseLookahead = 0;
    Outcome outcome;
    if (number == 9) outcome = solvePhaseBucketMode(instance, options);
    else outcome = solvePhaseBucket(instance, options);
    Candidate base = outcome.candidate;
    base.phaseAware = true;
    base.mixedRoutes = false;
    base.phaseLookahead = false;
    base.phaseEnumerated = false;
    base.phaseJoint = false;
    base.phaseOutboundOnly = false;
    base.phaseHardUnblock = false;
    base.learnedPolicy = false;
    Candidate collect = base;
    collect.collectFirst = true;
    Result baseline = simulate(instance, collect, outcome.batchLimit, false);
    std::cout << "BASE instance=" << number << " seed=" << seed
              << " mean=" << static_cast<double>(baseline.mean)
              << " feasible=" << baseline.feasible
              << " first_agv=" << baseline.firstAgv
              << " routes=" << baseline.firstRoutes.size() << '\n';
    std::vector<RecordedRoute> alternatives;
    for (const auto& route : baseline.firstRoutes) {
        if (route.output == baseline.firstChosen.output &&
            route.machines == baseline.firstChosen.machines) continue;
        bool duplicate = false;
        for (const auto& other : alternatives)
            if (other.output == route.output && other.machines == route.machines) duplicate = true;
        if (!duplicate) alternatives.push_back(route);
    }
    std::sort(alternatives.begin(), alternatives.end(), [](const RecordedRoute& a, const RecordedRoute& b) {
        if (a.score != b.score) return a.score > b.score;
        if (a.output != b.output) return a.output;
        return a.machines < b.machines;
    });
    if (alternatives.size() > 2) alternatives.resize(2);
    for (std::size_t i = 0; i < alternatives.size(); ++i) {
        Candidate forced = base;
        forced.forcedFirst = true;
        forced.forcedAgv = baseline.firstAgv;
        forced.forcedOutput = alternatives[i].output;
        forced.forcedMachines = alternatives[i].machines;
        Result result = simulate(instance, forced, outcome.batchLimit, false);
        std::cout << "ALT" << i << " instance=" << number << " seed=" << seed
                  << " output=" << alternatives[i].output
                  << " machines=";
        for (int j : alternatives[i].machines) std::cout << (j + 1) << '/';
        std::cout << " mean=" << static_cast<double>(result.mean)
                  << " feasible=" << result.feasible
                  << " gain=" << static_cast<double>((baseline.mean - result.mean) / baseline.mean * 100)
                  << '\n';
    }
    return 0;
}
