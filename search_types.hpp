#pragma once

#include "simulator.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace io1 {

struct Options {
    std::uint64_t seed = 20261106;
    int maxEvals = 1000;
    int maxNeighbors = 40;
    int phaseGlobal = -1;
    int phaseLookahead = 0;
    int phaseOutboundOnly = 0;
    int phaseHardUnblock = 0;
    bool learnedPolicy = false;
    std::array<double, 9> dispatchWeights{1, 0, 0, 0, 0, 0, 0, 0, 0};
};

struct Outcome {
    Candidate candidate;
    Result result;
    int batchLimit = 1;
    long long evals = 0;
    std::string strategy;
};

}
