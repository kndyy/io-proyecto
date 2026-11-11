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
};

std::array<double, 9> baselineWeights() {
    return {1, 0, 0, 0, 0, 0, 0, 0, 0};
}

std::vector<Snapshot> generateSnapshots(int number, int evals) {
    std::vector<Snapshot> snapshots;
    const std::array<int, 5> seeds{20261109, 12345, 55555, 10101, 98989};
    for (int seed : seeds) {
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
        Candidate candidate = outcome.candidate;
        candidate.phaseAware = true;
        candidate.mixedRoutes = false;
        candidate.phaseLookahead = false;
        candidate.phaseEnumerated = false;
        candidate.phaseJoint = false;
        candidate.phaseOutboundOnly = false;
        candidate.phaseHardUnblock = false;
        candidate.learnedPolicy = false;
        Result base = simulate(instance, candidate, outcome.batchLimit, false);
        snapshots.push_back({instance, candidate, outcome.batchLimit, base.mean});
    }
    return snapshots;
}

struct Scales {
    std::array<double, 9> mean{};
    std::array<double, 9> sd{1, 1, 1, 1, 1, 1, 1, 1, 1};
};

Scales featureScales(const std::vector<Snapshot>& train) {
    Scales scales;
    std::array<long double, 9> sum{};
    std::array<long double, 9> square{};
    long long count = 0;
    for (const auto& snapshot : train) {
        Candidate candidate = snapshot.candidate;
        candidate.learnedPolicy = true;
        candidate.dispatchWeights = baselineWeights();
        Result result = simulate(snapshot.instance, candidate, snapshot.batchLimit, false);
        for (int d = 0; d < 9; ++d) {
            sum[d] += result.featureSum[d];
            square[d] += result.featureSquareSum[d];
        }
        count += result.featureCount;
    }
    for (int d = 1; d < 9; ++d) {
        if (count == 0) continue;
        long double m = sum[d] / count;
        long double variance = square[d] / count - m * m;
        scales.mean[d] = static_cast<double>(m);
        double sd = std::sqrt(std::max(1e-12L, variance));
        scales.sd[d] = sd < 1e-3 ? 1.0 : sd;
    }
    return scales;
}

std::array<double, 9> rawWeights(const std::array<double, 9>& theta, const Scales& scales) {
    std::array<double, 9> raw = theta;
    for (int d = 1; d < 9; ++d) raw[d] = theta[d] / scales.sd[d];
    return raw;
}

long double evaluate(const std::vector<Snapshot>& snapshots,
                     const std::array<double, 9>& theta, const Scales& scales) {
    std::array<double, 9> raw = rawWeights(theta, scales);
    long double total = 0;
    for (const auto& snapshot : snapshots) {
        Candidate candidate = snapshot.candidate;
        candidate.learnedPolicy = true;
        candidate.dispatchWeights = raw;
        Result result = simulate(snapshot.instance, candidate, snapshot.batchLimit, false);
        if (!result.feasible) return 1e9L;
        total += result.mean / snapshot.baseline;
    }
    return total / snapshots.size();
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
    int number = argc > 1 ? std::stoi(argv[1]) : 9;
    int evals = argc > 2 ? std::stoi(argv[2]) : 20000;
    std::filesystem::create_directories("experiments/local");
    std::vector<Snapshot> all = generateSnapshots(number, evals);
    std::vector<Snapshot> train;
    std::vector<Snapshot> holdout;
    for (std::size_t i = 0; i < all.size(); ++i) {
        if (i < 3) train.push_back(all[i]);
        else holdout.push_back(all[i]);
    }
    Scales scales = featureScales(train);
    std::array<double, 9> best = baselineWeights();
    long double bestFitness = evaluate(train, best, scales);
    std::cout << "baseline fitness " << static_cast<double>(bestFitness) << '\n';
    std::array<double, 9> coordinateBest = best;
    long double coordinateFitness = bestFitness;
    const std::array<double, 7> deltas{-0.2, -0.05, -0.01, 0.0, 0.01, 0.05, 0.2};
    std::ofstream coordinateCsv("experiments/local/coordinate_" + std::to_string(number) + ".csv");
    coordinateCsv << "coordinate,value,fitness\n";
    for (int d = 1; d < 9; ++d) {
        for (double delta : deltas) {
            std::array<double, 9> candidate = coordinateBest;
            candidate[d] = delta;
            long double fitness = evaluate(train, candidate, scales);
            coordinateCsv << d << ',' << delta << ',' << static_cast<double>(fitness) << '\n';
            if (fitness < coordinateFitness) {
                coordinateFitness = fitness;
                coordinateBest = candidate;
            }
        }
    }
    std::cout << "coordinate fitness " << static_cast<double>(coordinateFitness) << '\n';
    std::array<double, 8> mu{};
    std::array<double, 8> sigma{};
    sigma.fill(0.5);
    for (int i = 0; i < 8; ++i) mu[i] = coordinateBest[i + 1];
    std::mt19937_64 rng(20261109);
    std::array<double, 9> cemBest = coordinateBest;
    long double cemFitness = coordinateFitness;
    std::ofstream cemCsv("experiments/local/cem_" + std::to_string(number) + ".csv");
    cemCsv << "generation,fitness,theta0,theta1,theta2,theta3,theta4,theta5,theta6,theta7,theta8\n";
    const int population = 64;
    const int eliteCount = 10;
    const int generations = 40;
    for (int generation = 1; generation <= generations; ++generation) {
        std::vector<std::array<double, 9>> candidates;
        candidates.push_back(baselineWeights());
        candidates.push_back(coordinateBest);
        for (int i = 2; i < population; ++i) candidates.push_back(sample(mu, sigma, rng));
        std::vector<std::pair<long double, int>> scored;
        for (int i = 0; i < population; ++i)
            scored.push_back({evaluate(train, candidates[i], scales), i});
        std::sort(scored.begin(), scored.end());
        if (scored.front().first < cemFitness) {
            cemFitness = scored.front().first;
            cemBest = candidates[scored.front().second];
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
        cemCsv << generation << ',' << static_cast<double>(cemFitness);
        for (double value : cemBest) cemCsv << ',' << value;
        cemCsv << '\n';
        std::cout << "generation " << generation << " best "
                  << static_cast<double>(cemFitness) << '\n';
    }
    std::array<double, 9> raw = rawWeights(cemBest, scales);
    std::ofstream policy("experiments/local/policy_" + std::to_string(number) + ".txt");
    for (double value : raw) policy << value << ' ';
    policy << '\n';
    std::ofstream summary("experiments/local/policy_" + std::to_string(number) + ".csv");
    summary << "feature,mean,sd,cem_theta,raw_weight\n";
    for (int d = 0; d < 9; ++d)
        summary << d << ',' << scales.mean[d] << ',' << scales.sd[d] << ','
                << cemBest[d] << ',' << raw[d] << '\n';
    std::cout << "holdout\n";
    long double baseHold = evaluate(holdout, baselineWeights(), scales);
    long double coordHold = evaluate(holdout, coordinateBest, scales);
    long double cemHold = evaluate(holdout, cemBest, scales);
    std::cout << "baseline " << static_cast<double>(baseHold)
              << " coordinate " << static_cast<double>(coordHold)
              << " cem " << static_cast<double>(cemHold) << '\n';
    return 0;
}
