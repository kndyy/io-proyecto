#include "heuristic.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <vector>

using namespace io1;

int main(int argc, char** argv) {
    std::string instanceNumber = "09";
    int seed = 20261109;
    int freezeEvals = 5000;
    int mode = 0;
    int samples = 100;
    double strength = 0.75;
    std::string out;
    for (int i = 1; i + 1 < argc; i += 2) {
        std::string key = argv[i];
        std::string value = argv[i + 1];
        if (key == "--instance") instanceNumber = value;
        else if (key == "--seed") seed = std::stoi(value);
        else if (key == "--freeze-evals") freezeEvals = std::stoi(value);
        else if (key == "--mode") mode = std::stoi(value);
        else if (key == "--samples") samples = std::stoi(value);
        else if (key == "--strength") strength = std::stod(value);
        else if (key == "--out") out = value;
    }
    std::string digits = instanceNumber;
    if (digits.size() == 1) digits = "0" + digits;
    Instance instance = loadInstance("instances/instancia_" + digits + ".io1");
    Options options;
    options.seed = seed;
    options.maxEvals = freezeEvals;
    options.maxNeighbors = 120;
    options.phaseGlobal = mode;
    options.phaseLookahead = 0;
    Outcome frozen = solvePhaseBucketMode(instance, options);
    Candidate base = frozen.candidate;
    base.phaseAware = true;
    base.phaseLookahead = false;
    base.transportKeysActive = false;
    Result baseline = simulate(instance, base, frozen.batchLimit, false);
    Candidate neutral = base;
    neutral.transportKeysActive = true;
    neutral.transportInputKeys.assign(instance.n, 0.5f);
    neutral.transportOutputKeys.assign(instance.n, 0.5f);
    neutral.transportKeyStrength = static_cast<float>(strength);
    Result neutralResult = simulate(instance, neutral, frozen.batchLimit, false);
    if (!neutralResult.feasible ||
        std::fabs(neutralResult.mean - baseline.mean) > 1e-9L) {
        std::cerr << "neutral keys changed baseline\n";
        return 2;
    }
    std::mt19937_64 rng(seed ^ 0xD6E8FEB86659FD93ULL);
    std::uniform_real_distribution<float> uniform(0.0f, 1.0f);
    long double bestMean = baseline.mean;
    long double bestGain = 0;
    int valid = 0;
    for (int sample = 0; sample < samples; ++sample) {
        Candidate candidate = base;
        candidate.transportKeysActive = true;
        candidate.transportInputKeys.resize(instance.n, 0.5f);
        candidate.transportOutputKeys.resize(instance.n, 0.5f);
        candidate.transportKeyStrength = static_cast<float>(strength);
        int changed = sample % 3 == 0 ? instance.n : (sample % 3 == 1 ? instance.n / 10 : instance.n * 3 / 10);
        for (int i = 0; i < changed; ++i) {
            int lot = static_cast<int>(rng() % instance.n);
            candidate.transportInputKeys[lot] = uniform(rng);
        }
        for (int i = 0; i < changed; ++i) {
            int lot = static_cast<int>(rng() % instance.n);
            candidate.transportOutputKeys[lot] = uniform(rng);
        }
        Result result = simulate(instance, candidate, frozen.batchLimit, false);
        if (!result.feasible) continue;
        ++valid;
        long double gain = 100.0L * (baseline.mean - result.mean) / baseline.mean;
        if (result.mean < bestMean) {
            bestMean = result.mean;
            bestGain = gain;
        }
    }
    std::cout << "instance=" << digits << " seed=" << seed << " mode=" << mode
              << " baseline=" << static_cast<double>(baseline.mean)
              << " best=" << static_cast<double>(bestMean)
              << " gain_pct=" << static_cast<double>(bestGain)
              << " valid=" << valid << "/" << samples << '\n';
    if (!out.empty()) {
        std::filesystem::create_directories(std::filesystem::path(out).parent_path());
        std::ofstream file(out, std::ios::app);
        file << digits << ',' << seed << ',' << mode << ',' << freezeEvals << ','
             << static_cast<double>(baseline.mean) << ',' << static_cast<double>(bestMean) << ','
             << static_cast<double>(bestGain) << ',' << valid << '/' << samples << '\n';
    }
    return 0;
}
