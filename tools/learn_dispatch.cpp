#include "heuristic.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <vector>

using namespace io1;

struct Snapshot {
    Instance instance;
    Candidate candidate;
    int batchLimit = 1;
    long double baseline = 1;
    int instanceNumber = 0;
};

std::array<double, 9> baselineWeights() {
    return {1, 0, 0, 0, 0, 0, 0, 0, 0};
}

std::vector<Snapshot> freezeSnapshots(int evals, int neighbors) {
    std::vector<Snapshot> snapshots;
    const std::array<int, 5> seeds{20261109, 12345, 55555, 10101, 98989};
    for (int number : {9, 10}) {
        Instance instance = loadInstance("instances/instancia_" +
            std::string(number < 10 ? "0" : "") + std::to_string(number) + ".io1");
        for (int seed : seeds) {
            Options options;
            options.seed = seed;
            options.maxEvals = evals;
            options.maxNeighbors = neighbors;
            options.phaseGlobal = 0;
            options.phaseLookahead = 0;
            Outcome outcome = solvePhaseBucketMode(instance, options);
            Candidate candidate = outcome.candidate;
            candidate.phaseAware = true;
            candidate.mixedRoutes = false;
            candidate.phaseGlobal = false;
            candidate.phaseLookahead = false;
            candidate.phaseEnumerated = false;
            candidate.phaseJoint = false;
            candidate.phaseOutboundOnly = false;
            candidate.phaseHardUnblock = false;
            candidate.learnedPolicy = false;
            Result base = simulate(instance, candidate, outcome.batchLimit, false);
            snapshots.push_back({instance, candidate, outcome.batchLimit, base.mean, number});
        }
    }
    return snapshots;
}

long double evaluate(const std::vector<Snapshot>& snapshots,
                     const std::array<double, 9>& weights) {
    long double total = 0;
    for (const auto& snapshot : snapshots) {
        Candidate candidate = snapshot.candidate;
        candidate.learnedPolicy = true;
        candidate.dispatchWeights = weights;
        Result result = simulate(snapshot.instance, candidate, snapshot.batchLimit, false);
        if (!result.feasible) return 1e9L;
        total += result.mean / snapshot.baseline;
    }
    return total / snapshots.size();
}

long double evaluateInstance(const std::vector<Snapshot>& snapshots,
                             const std::array<double, 9>& weights, int number) {
    long double total = 0;
    int count = 0;
    for (const auto& snapshot : snapshots) {
        if (snapshot.instanceNumber != number) continue;
        Candidate candidate = snapshot.candidate;
        candidate.learnedPolicy = true;
        candidate.dispatchWeights = weights;
        Result result = simulate(snapshot.instance, candidate, snapshot.batchLimit, false);
        if (!result.feasible) return 1e9L;
        total += result.mean / snapshot.baseline;
        ++count;
    }
    return total / count;
}

std::array<double, 9> sample(const std::array<double, 8>& mu,
                             const std::array<double, 8>& sigma,
                             std::mt19937_64& rng) {
    std::normal_distribution<double> normal(0.0, 1.0);
    std::array<double, 9> theta = baselineWeights();
    for (int i = 0; i < 8; ++i)
        theta[i + 1] = std::clamp(mu[i] + sigma[i] * normal(rng), -3.0, 3.0);
    return theta;
}

int main(int argc, char** argv) {
    int evals = 20000;
    if (argc > 1) evals = std::stoi(argv[1]);
    std::filesystem::create_directories("experiments/local");
    std::vector<Snapshot> all = freezeSnapshots(evals, 120);
    std::vector<Snapshot> trainSet;
    std::vector<Snapshot> holdoutSet;
    const std::array<int, 5> seeds{20261109, 12345, 55555, 10101, 98989};
    for (std::size_t i = 0; i < all.size(); ++i) {
        int seed = seeds[i % seeds.size()];
        if (seed == 10101 || seed == 98989) holdoutSet.push_back(all[i]);
        else trainSet.push_back(all[i]);
    }
    std::array<double, 8> mu{};
    std::array<double, 8> sigma{};
    sigma.fill(0.5);
    std::mt19937_64 rng(20261109);
    std::array<double, 9> best = baselineWeights();
    long double bestFitness = evaluate(trainSet, best);
    std::ofstream csv("experiments/local/policy_global.csv");
    csv << "generation,fitness,theta0,theta1,theta2,theta3,theta4,theta5,theta6,theta7,theta8\n";
    const int population = 32;
    const int eliteCount = 8;
    const int generations = 25;
    for (int generation = 1; generation <= generations; ++generation) {
        std::vector<std::array<double, 9>> candidates;
        candidates.push_back(baselineWeights());
        for (int i = 1; i < population; ++i) candidates.push_back(sample(mu, sigma, rng));
        std::vector<std::pair<long double, int>> scored;
        for (int i = 0; i < population; ++i)
            scored.push_back({evaluate(trainSet, candidates[i]), i});
        std::sort(scored.begin(), scored.end());
        if (scored.front().first < bestFitness) {
            bestFitness = scored.front().first;
            best = candidates[scored.front().second];
        }
        std::array<double, 8> eliteMean{};
        std::array<double, 8> eliteSd{};
        for (int e = 0; e < eliteCount; ++e) {
            const auto& theta = candidates[scored[e].second];
            for (int i = 0; i < 8; ++i) eliteMean[i] += theta[i + 1] / eliteCount;
        }
        for (int e = 0; e < eliteCount; ++e) {
            const auto& theta = candidates[scored[e].second];
            for (int i = 0; i < 8; ++i)
                eliteSd[i] += (theta[i + 1] - eliteMean[i]) *
                              (theta[i + 1] - eliteMean[i]) / eliteCount;
        }
        for (int i = 0; i < 8; ++i) {
            eliteSd[i] = std::sqrt(eliteSd[i]);
            mu[i] = std::clamp(0.7 * mu[i] + 0.3 * eliteMean[i], -3.0, 3.0);
            sigma[i] = std::max(0.05, 0.7 * sigma[i] + 0.3 * eliteSd[i]);
        }
        csv << generation << ',' << static_cast<double>(bestFitness);
        for (double value : best) csv << ',' << value;
        csv << '\n';
        std::cout << "generation " << generation << " best " << static_cast<double>(bestFitness)
                  << '\n';
    }
    std::ofstream policy("experiments/local/policy_global.txt");
    for (double value : best) policy << value << ' ';
    policy << '\n';
    std::cout << "holdout\n";
    for (int number : {9, 10}) {
        std::array<double, 9> baseline = baselineWeights();
        long double baseRatio = evaluateInstance(holdoutSet, baseline, number);
        long double learnedRatio = evaluateInstance(holdoutSet, best, number);
        std::cout << number << " baseline " << static_cast<double>(baseRatio)
                  << " learned " << static_cast<double>(learnedRatio)
                  << " ratio " << static_cast<double>(learnedRatio / baseRatio) << '\n';
    }
    return 0;
}
