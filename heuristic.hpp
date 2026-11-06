#pragma once

#include "simulator.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <random>
#include <string>
#include <vector>

namespace io1 {

enum class Strategy { B1, B2, B3, S0, S1, S2, Vnd, VndS0, VndS1, VndS2 };

struct Options {
    std::uint64_t seed = 20261106;
    int maxEvals = 1000;
    int maxNeighbors = 40;
};

struct Outcome {
    Candidate candidate;
    Result result;
    int batchLimit = 1;
    long long evals = 0;
    std::string strategy;
};

enum class Assign { Fastest, Score, Balance };

inline bool strategyFromName(const std::string& name, Strategy& strategy) {
    if (name == "b1") strategy = Strategy::B1;
    else if (name == "b2") strategy = Strategy::B2;
    else if (name == "b3") strategy = Strategy::B3;
    else if (name == "s0") strategy = Strategy::S0;
    else if (name == "s1") strategy = Strategy::S1;
    else if (name == "s2") strategy = Strategy::S2;
    else if (name == "vnd") strategy = Strategy::Vnd;
    else if (name == "vnd-s0") strategy = Strategy::VndS0;
    else if (name == "vnd-s1") strategy = Strategy::VndS1;
    else if (name == "vnd-s2") strategy = Strategy::VndS2;
    else return false;
    return true;
}

inline std::string strategyName(Strategy strategy) {
    switch (strategy) {
        case Strategy::B1: return "b1";
        case Strategy::B2: return "b2";
        case Strategy::B3: return "b3";
        case Strategy::S0: return "s0";
        case Strategy::S1: return "s1";
        case Strategy::S2: return "s2";
        case Strategy::Vnd: return "vnd";
        case Strategy::VndS0: return "vnd-s0";
        case Strategy::VndS1: return "vnd-s1";
        case Strategy::VndS2: return "vnd-s2";
    }
    return "vnd";
}

inline std::vector<int> orderById(const Instance& in) {
    std::vector<int> order(in.n);
    std::iota(order.begin(), order.end(), 0);
    return order;
}

inline std::vector<int> orderByHeight(const Instance& in) {
    std::vector<int> order = orderById(in);
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        long double ha = lotHeight(in, a);
        long double hb = lotHeight(in, b);
        if (ha != hb) return ha < hb;
        return in.lots[a].id < in.lots[b].id;
    });
    return order;
}

inline std::vector<int> orderByFastest(const Instance& in) {
    std::vector<int> order = orderById(in);
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        long double fa = lotFastest(in, a);
        long double fb = lotFastest(in, b);
        if (fa != fb) return fa < fb;
        return in.lots[a].id < in.lots[b].id;
    });
    return order;
}

inline int chooseFastest(const Instance& in, int lot) {
    int best = -1;
    long double bestDuration = 0;
    for (int j = 0; j < in.m; ++j) {
        if (!in.compatible(lot, j)) continue;
        long double duration = in.duration(lot, j);
        if (best < 0 || duration < bestDuration) {
            best = j;
            bestDuration = duration;
        }
    }
    return best;
}

inline int chooseScore(const Instance& in, int lot, const std::vector<long double>& loads) {
    int best = -1;
    long double bestScore = 0;
    for (int j = 0; j < in.m; ++j) {
        if (!in.compatible(lot, j)) continue;
        long double score = loads[j] + in.duration(lot, j) +
                            in.tau[in.o][in.alpha[j]] + in.tau[in.beta[j]][in.f];
        if (best < 0 || score < bestScore) {
            best = j;
            bestScore = score;
        }
    }
    return best;
}

inline int chooseBalance(const Instance& in, int lot, const std::vector<long double>& loads) {
    int best = -1;
    long double bestLoad = 0;
    for (int j = 0; j < in.m; ++j) {
        if (!in.compatible(lot, j)) continue;
        long double value = loads[j] + in.duration(lot, j);
        if (best < 0 || value < bestLoad) {
            best = j;
            bestLoad = value;
        }
    }
    return best;
}

inline Candidate buildOrdered(const Instance& in, const std::vector<int>& order, Assign assign) {
    Candidate candidate;
    candidate.sequence.resize(in.m);
    std::vector<long double> loads(in.m, 0);
    for (int lot : order) {
        int machine = assign == Assign::Fastest ? chooseFastest(in, lot)
                    : assign == Assign::Score ? chooseScore(in, lot, loads)
                    : chooseBalance(in, lot, loads);
        candidate.sequence[machine].push_back(lot);
        loads[machine] += in.duration(lot, machine);
    }
    return candidate;
}

inline Candidate buildRandomized(const Instance& in, std::mt19937_64& rng) {
    Candidate candidate;
    candidate.sequence.resize(in.m);
    std::vector<long double> loads(in.m, 0);
    std::vector<int> remaining = orderById(in);
    while (!remaining.empty()) {
        std::sort(remaining.begin(), remaining.end(), [&](int a, int b) {
            long double ha = lotHeight(in, a);
            long double hb = lotHeight(in, b);
            if (ha != hb) return ha < hb;
            return in.lots[a].id < in.lots[b].id;
        });
        std::size_t rcl = std::max<std::size_t>(1, (remaining.size() + 4) / 5);
        std::size_t pick = static_cast<std::size_t>(rng() % rcl);
        int lot = remaining[pick];
        remaining.erase(remaining.begin() + static_cast<std::ptrdiff_t>(pick));
        int machine = chooseScore(in, lot, loads);
        candidate.sequence[machine].push_back(lot);
        loads[machine] += in.duration(lot, machine);
    }
    return candidate;
}

struct Move {
    int source;
    int from;
    int destination;
    int to;
};

struct Swap {
    int firstMachine;
    int firstIndex;
    int secondMachine;
    int secondIndex;
};

inline std::vector<Move> relocateMoves(const Instance& in, const Candidate& candidate) {
    std::vector<Move> moves;
    for (int source = 0; source < in.m; ++source) {
        for (int from = 0; from < static_cast<int>(candidate.sequence[source].size()); ++from) {
            int lot = candidate.sequence[source][from];
            for (int destination = 0; destination < in.m; ++destination) {
                if (!in.compatible(lot, destination)) continue;
                int size = static_cast<int>(candidate.sequence[destination].size()) -
                           (destination == source ? 1 : 0);
                for (int to = 0; to <= size; ++to) {
                    if (destination == source && (to == from || to == from + 1)) continue;
                    moves.push_back({source, from, destination, to});
                }
            }
        }
    }
    return moves;
}

inline std::vector<Swap> swapMoves(const Instance& in, const Candidate& candidate) {
    std::vector<Swap> moves;
    for (int firstMachine = 0; firstMachine < in.m; ++firstMachine) {
        for (int firstIndex = 0;
             firstIndex < static_cast<int>(candidate.sequence[firstMachine].size());
             ++firstIndex) {
            int firstLot = candidate.sequence[firstMachine][firstIndex];
            for (int secondMachine = firstMachine; secondMachine < in.m; ++secondMachine) {
                int start = secondMachine == firstMachine ? firstIndex + 1 : 0;
                for (int secondIndex = start;
                     secondIndex < static_cast<int>(candidate.sequence[secondMachine].size());
                     ++secondIndex) {
                    int secondLot = candidate.sequence[secondMachine][secondIndex];
                    if (secondMachine != firstMachine &&
                        (!in.compatible(firstLot, secondMachine) ||
                         !in.compatible(secondLot, firstMachine))) {
                        continue;
                    }
                    moves.push_back({firstMachine, firstIndex, secondMachine, secondIndex});
                }
            }
        }
    }
    return moves;
}

inline Candidate applyMove(const Candidate& candidate, const Move& move) {
    Candidate next = candidate;
    int lot = next.sequence[move.source][move.from];
    next.sequence[move.source].erase(
        next.sequence[move.source].begin() + static_cast<std::ptrdiff_t>(move.from));
    next.sequence[move.destination].insert(
        next.sequence[move.destination].begin() + static_cast<std::ptrdiff_t>(move.to), lot);
    return next;
}

inline Candidate applySwap(const Candidate& candidate, const Swap& swap) {
    Candidate next = candidate;
    std::swap(next.sequence[swap.firstMachine][swap.firstIndex],
              next.sequence[swap.secondMachine][swap.secondIndex]);
    return next;
}

template <typename T>
inline void shuffleVector(std::vector<T>& values, std::mt19937_64& rng) {
    for (std::size_t i = values.size(); i > 1; --i) {
        std::size_t j = static_cast<std::size_t>(rng() % i);
        std::swap(values[i - 1], values[j]);
    }
}

struct SearchState {
    Candidate candidate;
    Result result;
    int batchLimit = 1;
    long long budget = 0;
    long long evals = 0;
};

inline bool tryRelocate(const Instance& in, SearchState& state, std::mt19937_64& rng,
                        int maxNeighbors) {
    auto moves = relocateMoves(in, state.candidate);
    shuffleVector(moves, rng);
    int count = std::min<int>(maxNeighbors, static_cast<int>(moves.size()));
    for (int i = 0; i < count && state.budget > 0; ++i) {
        Candidate next = applyMove(state.candidate, moves[i]);
        Result result = simulate(in, next, state.batchLimit);
        --state.budget;
        ++state.evals;
        long double eps = 1e-9L * std::max(1.0L, std::fabs(state.result.mean));
        if (result.feasible &&
            (!state.result.feasible || result.mean < state.result.mean - eps)) {
            state.candidate = std::move(next);
            state.result = result;
            return true;
        }
    }
    return false;
}

inline bool trySwap(const Instance& in, SearchState& state, std::mt19937_64& rng,
                    int maxNeighbors) {
    auto moves = swapMoves(in, state.candidate);
    shuffleVector(moves, rng);
    int count = std::min<int>(maxNeighbors, static_cast<int>(moves.size()));
    for (int i = 0; i < count && state.budget > 0; ++i) {
        Candidate next = applySwap(state.candidate, moves[i]);
        Result result = simulate(in, next, state.batchLimit);
        --state.budget;
        ++state.evals;
        long double eps = 1e-9L * std::max(1.0L, std::fabs(state.result.mean));
        if (result.feasible &&
            (!state.result.feasible || result.mean < state.result.mean - eps)) {
            state.candidate = std::move(next);
            state.result = result;
            return true;
        }
    }
    return false;
}

struct SearchResult {
    Candidate candidate;
    Result result;
    int batchLimit = 1;
    long long evals = 0;
};

inline SearchResult searchFromStart(const Instance& in, const Candidate& start,
                                    const Options& options, std::mt19937_64& rng) {
    SearchState state;
    state.candidate = start;
    state.result = simulate(in, start, 1);
    state.evals = 1;
    state.batchLimit = 1;
    if (in.C > 1) {
        Result full = simulate(in, start, in.C);
        ++state.evals;
        if (full.feasible &&
            (!state.result.feasible || full.mean < state.result.mean)) {
            state.result = full;
            state.batchLimit = in.C;
        }
    }
    state.budget = options.maxEvals;
    int neighborhood = 1;
    while (neighborhood <= 2 && state.budget > 0) {
        bool improved = neighborhood == 1
            ? tryRelocate(in, state, rng, options.maxNeighbors)
            : trySwap(in, state, rng, options.maxNeighbors);
        if (improved) neighborhood = 1;
        else ++neighborhood;
    }
    return {state.candidate, state.result, state.batchLimit, state.evals};
}

struct LimitsResult {
    Result result;
    int batchLimit = 1;
    long long evals = 1;
};

inline LimitsResult evaluateLimits(const Instance& in, const Candidate& candidate) {
    LimitsResult limits;
    limits.result = simulate(in, candidate, 1);
    if (in.C > 1) {
        Result full = simulate(in, candidate, in.C);
        ++limits.evals;
        if (full.feasible &&
            (!limits.result.feasible || full.mean < limits.result.mean)) {
            limits.result = full;
            limits.batchLimit = in.C;
        }
    }
    return limits;
}

inline Outcome solve(const Instance& in, Strategy strategy, const Options& options) {
    Outcome outcome;
    outcome.strategy = strategyName(strategy);

    if (strategy == Strategy::B1 || strategy == Strategy::B2 || strategy == Strategy::B3) {
        std::vector<int> order = strategy == Strategy::B2 ? orderByFastest(in) : orderById(in);
        Assign assign = strategy == Strategy::B3 ? Assign::Balance : Assign::Fastest;
        outcome.candidate = buildOrdered(in, order, assign);
        outcome.batchLimit = in.C;
        outcome.result = simulate(in, outcome.candidate, in.C);
        outcome.evals = 1;
        return outcome;
    }

    if (strategy == Strategy::S0 || strategy == Strategy::S1 || strategy == Strategy::S2) {
        std::mt19937_64 rng(options.seed);
        if (strategy == Strategy::S0) outcome.candidate = buildOrdered(in, orderByHeight(in), Assign::Score);
        else if (strategy == Strategy::S1) outcome.candidate = buildOrdered(in, orderByFastest(in), Assign::Score);
        else outcome.candidate = buildRandomized(in, rng);
        LimitsResult limits = evaluateLimits(in, outcome.candidate);
        outcome.result = limits.result;
        outcome.batchLimit = limits.batchLimit;
        outcome.evals = limits.evals;
        return outcome;
    }

    if (strategy == Strategy::VndS0 || strategy == Strategy::VndS1 || strategy == Strategy::VndS2) {
        std::mt19937_64 startRng(options.seed);
        std::mt19937_64 searchRng(options.seed);
        Candidate start;
        if (strategy == Strategy::VndS0) start = buildOrdered(in, orderByHeight(in), Assign::Score);
        else if (strategy == Strategy::VndS1) start = buildOrdered(in, orderByFastest(in), Assign::Score);
        else start = buildRandomized(in, startRng);
        SearchResult search = searchFromStart(in, start, options, searchRng);
        outcome.candidate = search.candidate;
        outcome.result = search.result;
        outcome.batchLimit = search.batchLimit;
        outcome.evals = search.evals;
        return outcome;
    }

    std::mt19937_64 startRng(options.seed);
    std::vector<Candidate> starts;
    starts.push_back(buildOrdered(in, orderByHeight(in), Assign::Score));
    starts.push_back(buildOrdered(in, orderByFastest(in), Assign::Score));
    starts.push_back(buildRandomized(in, startRng));

    bool haveBest = false;
    SearchResult best;
    for (const auto& start : starts) {
        std::mt19937_64 searchRng(options.seed);
        SearchResult search = searchFromStart(in, start, options, searchRng);
        outcome.evals += search.evals;
        if (!haveBest ||
            (search.result.feasible &&
             (!best.result.feasible || search.result.mean < best.result.mean))) {
            best = search;
            haveBest = true;
        }
    }
    outcome.candidate = best.candidate;
    outcome.result = best.result;
    outcome.batchLimit = best.batchLimit;
    return outcome;
}

}
