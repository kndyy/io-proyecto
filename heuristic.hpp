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

enum class Strategy { B1, B2, B3, S0, S1, S2, Vnd, VndS0, VndS1, VndS2, Integrated, AlnsJoint, PhaseBucket, PhaseJoint };

struct Options {
    std::uint64_t seed = 20261106;
    int maxEvals = 1000;
    int maxNeighbors = 40;
    int phaseGlobal = -1;
    int phaseLookahead = 0;
    int phaseOutboundOnly = 0;
    int phaseHardUnblock = 0;
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
    else if (name == "integrated") strategy = Strategy::Integrated;
    else if (name == "alns-joint") strategy = Strategy::AlnsJoint;
    else if (name == "phase-bucket") strategy = Strategy::PhaseBucket;
    else if (name == "phase-joint") strategy = Strategy::PhaseJoint;
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
        case Strategy::Integrated: return "integrated";
        case Strategy::AlnsJoint: return "alns-joint";
        case Strategy::PhaseBucket: return "phase-bucket";
        case Strategy::PhaseJoint: return "phase-joint";
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
                        int maxNeighbors, bool recordLog = true) {
    auto moves = relocateMoves(in, state.candidate);
    shuffleVector(moves, rng);
    int count = std::min<int>(maxNeighbors, static_cast<int>(moves.size()));
    for (int i = 0; i < count && state.budget > 0; ++i) {
        Candidate next = applyMove(state.candidate, moves[i]);
        Result result = simulate(in, next, state.batchLimit, recordLog);
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
                    int maxNeighbors, bool recordLog = true) {
    auto moves = swapMoves(in, state.candidate);
    shuffleVector(moves, rng);
    int count = std::min<int>(maxNeighbors, static_cast<int>(moves.size()));
    for (int i = 0; i < count && state.budget > 0; ++i) {
        Candidate next = applySwap(state.candidate, moves[i]);
        Result result = simulate(in, next, state.batchLimit, recordLog);
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


struct DispatchMove {
    bool adaptive;
    int bias;
    int batch;
};

inline bool considerIntegrated(const Instance& in, SearchState& state,
                               Candidate next, int batch) {
    if (state.budget <= 0) return false;
    Result tested = simulate(in, next, batch);
    --state.budget;
    ++state.evals;
    long double eps = 1e-9L * std::max(1.0L, std::fabs(state.result.mean));
    if (!tested.feasible || tested.mean >= state.result.mean - eps) return false;
    state.candidate = std::move(next);
    state.result = std::move(tested);
    state.batchLimit = batch;
    return true;
}

inline bool tryDispatch(const Instance& in, SearchState& state,
                        std::mt19937_64& rng) {
    std::vector<DispatchMove> moves;
    for (int batch = 1; batch <= in.C; ++batch) {
        moves.push_back({false, 0, batch});
        for (int bias = -4; bias <= 4; ++bias)
            moves.push_back({true, bias, batch});
    }
    shuffleVector(moves, rng);
    for (const auto& move : moves) {
        if (state.budget <= 0) break;
        if (move.adaptive == state.candidate.adaptive &&
            (!move.adaptive || move.bias == state.candidate.bias) &&
            move.batch == state.batchLimit) continue;
        Candidate next = state.candidate;
        next.adaptive = move.adaptive;
        next.bias = move.bias;
        if (considerIntegrated(in, state, std::move(next), move.batch)) return true;
    }
    return false;
}

inline void improveIntegrated(const Instance& in, SearchState& state,
                              const Options& options, std::mt19937_64& rng) {
    int neighborhood = 1;
    while (state.budget > 0 && neighborhood <= 3) {
        bool improved = neighborhood == 1
            ? tryRelocate(in, state, rng, options.maxNeighbors)
            : neighborhood == 2
                ? trySwap(in, state, rng, options.maxNeighbors)
                : tryDispatch(in, state, rng);
        neighborhood = improved ? 1 : neighborhood + 1;
    }
}

inline void perturbIntegrated(const Instance& in, SearchState& state,
                              std::mt19937_64& rng) {
    int changes = std::max(2, std::min(6, (in.n + 39) / 40));
    for (int i = 0; i < changes; ++i) {
        bool relocate = (rng() % 10) < 6;
        if (relocate) {
            auto moves = relocateMoves(in, state.candidate);
            if (!moves.empty()) state.candidate =
                applyMove(state.candidate, moves[rng() % moves.size()]);
        } else {
            auto moves = swapMoves(in, state.candidate);
            if (!moves.empty()) state.candidate =
                applySwap(state.candidate, moves[rng() % moves.size()]);
        }
    }
    state.candidate.adaptive = true;
    if ((rng() % 2) == 0)
        state.candidate.bias = std::clamp(state.candidate.bias +
                                   ((rng() % 2) ? 1 : -1), -4, 4);
    if ((rng() % 4) == 0)
        state.batchLimit = 1 + static_cast<int>(rng() % in.C);
}

inline bool betterIntegrated(const SearchState& a, const SearchState& b) {
    return a.result.feasible &&
           (!b.result.feasible || a.result.mean < b.result.mean - 1e-9L);
}

inline Outcome solveIntegrated(const Instance& in, const Options& options) {
    std::mt19937_64 rng(options.seed);
    SearchState current;
    current.candidate = buildOrdered(in, orderByHeight(in), Assign::Score);
    current.batchLimit = in.C;
    current.result = simulate(in, current.candidate, current.batchLimit);
    current.evals = 1;
    current.budget = std::max(1, options.maxEvals) - 1;
    for (int batch : {in.C, 1}) {
        Candidate variant = current.candidate;
        variant.adaptive = true;
        considerIntegrated(in, current, std::move(variant), batch);
    }
    improveIntegrated(in, current, options, rng);
    SearchState best = current;
    int stagnation = 0;
    while (current.budget > 0) {
        SearchState trial = current;
        perturbIntegrated(in, trial, rng);
        trial.result = simulate(in, trial.candidate, trial.batchLimit);
        --trial.budget;
        ++trial.evals;
        if (trial.result.feasible) improveIntegrated(in, trial, options, rng);
        bool changedBest = betterIntegrated(trial, best);
        stagnation = changedBest ? 0 : stagnation + 1;
        if (changedBest) best = trial;
        bool accept = changedBest || betterIntegrated(trial, current)
                      || stagnation % 4 == 0;
        long long remaining = trial.budget;
        long long count = trial.evals;
        current = accept && trial.result.feasible ? trial : best;
        current.budget = remaining;
        current.evals = count;
    }
    Outcome outcome;
    outcome.candidate = best.candidate;
    outcome.result = best.result;
    outcome.batchLimit = best.batchLimit;
    outcome.evals = current.evals;
    outcome.strategy = "integrated";
    return outcome;
}


struct AlnsState {
    Candidate candidate;
    Result result;
};

inline long double alnsRandom01(std::mt19937_64& rng) {
    return static_cast<long double>(rng() >> 11) * (1.0L / 9007199254740992.0L);
}

inline Candidate makeMixedCandidate(const Instance& in, Candidate candidate) {
    candidate.mixedRoutes = true;
    candidate.priorityIn.assign(in.n, 0);
    candidate.priorityOut.assign(in.n, 0);
    return candidate;
}

inline std::vector<long double> lowerCompletion(const Instance& in) {
    std::vector<long double> lower(in.n, std::numeric_limits<long double>::infinity());
    for (int lot = 0; lot < in.n; ++lot) {
        for (int j = 0; j < in.m; ++j) {
            if (!in.compatible(lot, j)) continue;
            long double value = in.tau[in.o][in.alpha[j]] + in.duration(lot, j)
                + in.tau[in.beta[j]][in.f];
            lower[lot] = std::min(lower[lot], value);
        }
    }
    return lower;
}

inline std::vector<int> allLots(const Instance& in) {
    std::vector<int> lots(in.n);
    std::iota(lots.begin(), lots.end(), 0);
    return lots;
}

inline std::vector<int> criticalPool(const Instance& in, const Result& result) {
    std::vector<int> lots = allLots(in);
    if (!result.feasible) return lots;
    std::vector<long double> lower = lowerCompletion(in);
    std::sort(lots.begin(), lots.end(), [&](int a, int b) {
        long double da = result.completion[a] - lower[a];
        long double db = result.completion[b] - lower[b];
        if (da != db) return da > db;
        return in.lots[a].id < in.lots[b].id;
    });
    std::size_t keep = std::max<std::size_t>(1, (in.n + 3) / 4);
    if (lots.size() > keep) lots.resize(keep);
    return lots;
}

inline void removeLots(Candidate& candidate, const std::vector<int>& lots) {
    for (int lot : lots) {
        for (int j = 0; j < static_cast<int>(candidate.sequence.size()); ++j) {
            auto& sequence = candidate.sequence[j];
            auto it = std::find(sequence.begin(), sequence.end(), lot);
            if (it != sequence.end()) {
                sequence.erase(it);
                break;
            }
        }
    }
}

inline std::vector<long double> sequenceLoads(const Instance& in, const Candidate& candidate) {
    std::vector<long double> loads(in.m, 0);
    for (int j = 0; j < in.m; ++j) {
        for (int lot : candidate.sequence[j]) loads[j] += in.duration(lot, j);
    }
    return loads;
}

inline std::vector<int> insertionPositions(const Instance& in, const Candidate& candidate,
                                           int lot, int machine) {
    int length = static_cast<int>(candidate.sequence[machine].size());
    std::vector<int> positions{0, length / 4, length / 2, (3 * length) / 4, length};
    int spt = length;
    long double duration = in.duration(lot, machine);
    for (int h = 0; h < length; ++h) {
        if (duration <= in.duration(candidate.sequence[machine][h], machine)) {
            spt = h;
            break;
        }
    }
    for (int delta = -1; delta <= 1; ++delta) {
        int position = spt + delta;
        if (position >= 0 && position <= length) positions.push_back(position);
    }
    std::sort(positions.begin(), positions.end());
    positions.erase(std::unique(positions.begin(), positions.end()), positions.end());
    return positions;
}

inline long double proxyScore(const Instance& in, const Candidate& candidate,
                              const std::vector<long double>& loads, long double meanLoad,
                              int lot, int machine, int position) {
    long double prefix = 0;
    for (int h = 0; h < position; ++h) {
        prefix += in.duration(candidate.sequence[machine][h], machine);
    }
    long double duration = in.duration(lot, machine);
    int length = static_cast<int>(candidate.sequence[machine].size());
    return prefix + duration + in.tau[in.o][in.alpha[machine]]
         + in.tau[in.beta[machine]][in.f] + duration * (length - position)
         + 0.5L * std::max(0.0L, loads[machine] + duration - meanLoad);
}

struct InsertionOption {
    int lot = -1;
    int machine = -1;
    int position = -1;
    long double score = std::numeric_limits<long double>::infinity();
};

inline void insertOption(Candidate& candidate, const Instance& in,
                         const InsertionOption& option, std::vector<long double>& loads,
                         long double& sumLoad) {
    candidate.sequence[option.machine].insert(
        candidate.sequence[option.machine].begin() + option.position, option.lot);
    long double duration = in.duration(option.lot, option.machine);
    loads[option.machine] += duration;
    sumLoad += duration;
}

inline void repairCheapest(const Instance& in, Candidate& candidate,
                           std::vector<int> remaining) {
    std::vector<long double> loads = sequenceLoads(in, candidate);
    long double sumLoad = std::accumulate(loads.begin(), loads.end(), 0.0L);
    while (!remaining.empty()) {
        long double meanLoad = sumLoad / in.m;
        InsertionOption best;
        for (int lot : remaining) {
            for (int j = 0; j < in.m; ++j) {
                if (!in.compatible(lot, j)) continue;
                for (int position : insertionPositions(in, candidate, lot, j)) {
                    long double score = proxyScore(in, candidate, loads, meanLoad,
                                                   lot, j, position);
                    bool better = score < best.score
                        || (score == best.score && in.lots[lot].id < in.lots[best.lot].id)
                        || (score == best.score && in.lots[lot].id == in.lots[best.lot].id
                            && j < best.machine)
                        || (score == best.score && in.lots[lot].id == in.lots[best.lot].id
                            && j == best.machine && position < best.position);
                    if (best.lot < 0 || better) best = {lot, j, position, score};
                }
            }
        }
        insertOption(candidate, in, best, loads, sumLoad);
        remaining.erase(std::find(remaining.begin(), remaining.end(), best.lot));
    }
}

inline void repairRegret(const Instance& in, Candidate& candidate,
                         std::vector<int> remaining) {
    const long double H = in.medianTau > 0 ? in.medianTau : 1.0L;
    std::vector<long double> loads = sequenceLoads(in, candidate);
    long double sumLoad = std::accumulate(loads.begin(), loads.end(), 0.0L);
    while (!remaining.empty()) {
        long double meanLoad = sumLoad / in.m;
        int chosenLot = -1;
        InsertionOption chosen;
        long double chosenRegret = -std::numeric_limits<long double>::infinity();
        for (int lot : remaining) {
            long double first = std::numeric_limits<long double>::infinity();
            long double second = std::numeric_limits<long double>::infinity();
            InsertionOption best;
            for (int j = 0; j < in.m; ++j) {
                if (!in.compatible(lot, j)) continue;
                for (int position : insertionPositions(in, candidate, lot, j)) {
                    long double score = proxyScore(in, candidate, loads, meanLoad,
                                                   lot, j, position);
                    if (score < first) {
                        second = first;
                        first = score;
                        best = {lot, j, position, score};
                    } else if (score < second) {
                        second = score;
                    }
                }
            }
            if (!std::isfinite(first)) continue;
            if (!std::isfinite(second)) second = first + H;
            long double regret = second - first;
            bool better = chosenLot < 0 || regret > chosenRegret
                || (regret == chosenRegret && first < chosen.score)
                || (regret == chosenRegret && first == chosen.score
                    && in.lots[lot].id < in.lots[chosenLot].id);
            if (better) {
                chosenLot = lot;
                chosenRegret = regret;
                chosen = best;
            }
        }
        insertOption(candidate, in, chosen, loads, sumLoad);
        remaining.erase(std::find(remaining.begin(), remaining.end(), chosenLot));
    }
}

inline std::vector<int> pickRandomLots(const Instance& in, int count,
                                       std::mt19937_64& rng) {
    std::vector<int> lots = allLots(in);
    shuffleVector(lots, rng);
    if (static_cast<int>(lots.size()) > count) lots.resize(count);
    return lots;
}

inline std::vector<int> pickFromPool(const Instance& in, const std::vector<int>& pool,
                                     int count, std::mt19937_64& rng) {
    std::vector<int> selected = pool;
    shuffleVector(selected, rng);
    if (static_cast<int>(selected.size()) > count) selected.resize(count);
    if (static_cast<int>(selected.size()) < count) {
        std::vector<int> rest = allLots(in);
        shuffleVector(rest, rng);
        for (int lot : rest) {
            if (static_cast<int>(selected.size()) >= count) break;
            if (std::find(selected.begin(), selected.end(), lot) == selected.end()) {
                selected.push_back(lot);
            }
        }
    }
    return selected;
}

inline std::vector<int> pickBlockedLots(const Instance& in, const Candidate& candidate,
                                        const Result& result, int count,
                                        std::mt19937_64& rng) {
    int machine = 0;
    long double worst = -1;
    for (int j = 0; j < in.m; ++j) {
        long double blocked = result.blockedByMachine.empty() ? 0 : result.blockedByMachine[j];
        if (blocked > worst) {
            worst = blocked;
            machine = j;
        }
    }
    if (worst <= 0) {
        return pickFromPool(in, criticalPool(in, result), count, rng);
    }
    std::vector<int> selected = candidate.sequence[machine];
    shuffleVector(selected, rng);
    if (static_cast<int>(selected.size()) > count) selected.resize(count);
    if (static_cast<int>(selected.size()) < count) {
        std::vector<int> rest = allLots(in);
        shuffleVector(rest, rng);
        for (int lot : rest) {
            if (static_cast<int>(selected.size()) >= count) break;
            if (std::find(selected.begin(), selected.end(), lot) == selected.end()) {
                selected.push_back(lot);
            }
        }
    }
    return selected;
}

inline Candidate destroyRepair(const Instance& in, const Candidate& current,
                               const Result& result, int op, std::mt19937_64& rng,
                               int& d4Count) {
    Candidate trial = current;
    int count = std::max(2, (12 * in.n + 99) / 100);
    std::vector<int> critical = criticalPool(in, result);
    if (op == 3) {
        ++d4Count;
        std::vector<int> selected = pickFromPool(in, critical, count, rng);
        std::vector<int> modified;
        for (int lot : selected) {
            int deltaIn = (rng() % 2) == 0 ? 2 : -2;
            int deltaOut = (rng() % 2) == 0 ? 2 : -2;
            trial.priorityIn[lot] = static_cast<std::int8_t>(std::clamp(
                static_cast<int>(trial.priorityIn[lot]) + deltaIn, -8, 8));
            trial.priorityOut[lot] = static_cast<std::int8_t>(std::clamp(
                static_cast<int>(trial.priorityOut[lot]) + deltaOut, -8, 8));
            modified.push_back(lot);
        }
        if (d4Count % 5 == 0) {
            for (std::size_t i = 0; i < modified.size(); i += 4) {
                trial.priorityIn[modified[i]] = 0;
                trial.priorityOut[modified[i]] = 0;
            }
        }
        return trial;
    }
    std::vector<int> removed;
    if (op == 0) removed = pickRandomLots(in, count, rng);
    else if (op == 1) removed = pickFromPool(in, critical, count, rng);
    else removed = pickBlockedLots(in, current, result, count, rng);
    removeLots(trial, removed);
    if (op == 0) repairCheapest(in, trial, removed);
    else repairRegret(in, trial, removed);
    return trial;
}

inline bool betterAlns(const Result& a, const Result& b) {
    return a.feasible && (!b.feasible || a.mean < b.mean - 1e-9L);
}

inline long long intensifyMixed(const Instance& in, AlnsState& state, long long limit,
                                const Options& options, std::mt19937_64& rng) {
    SearchState local;
    local.candidate = state.candidate;
    local.result = state.result;
    local.batchLimit = in.C;
    local.budget = limit;
    local.evals = 0;
    int neighborhood = 1;
    while (local.budget > 0 && neighborhood <= 2) {
        bool improved = neighborhood == 1
            ? tryRelocate(in, local, rng, options.maxNeighbors, false)
            : trySwap(in, local, rng, options.maxNeighbors, false);
        neighborhood = improved ? 1 : neighborhood + 1;
    }
    if (betterAlns(local.result, state.result)) {
        state.candidate = local.candidate;
        state.result = local.result;
    }
    return local.evals;
}

inline int weightedPick(const std::vector<long double>& weights, std::mt19937_64& rng) {
    long double total = std::accumulate(weights.begin(), weights.end(), 0.0L);
    if (total <= 0) return static_cast<int>(rng() % weights.size());
    long double value = alnsRandom01(rng) * total;
    for (std::size_t i = 0; i < weights.size(); ++i) {
        if (value < weights[i]) return static_cast<int>(i);
        value -= weights[i];
    }
    return static_cast<int>(weights.size() - 1);
}

inline Outcome solveAlnsJoint(const Instance& in, const Options& options) {
    long long E = std::max<long long>(1, options.maxEvals);
    long long e0 = std::max<long long>(1, E / 10);
    Outcome integrated = solveIntegrated(in, Options{options.seed,
        static_cast<int>(std::min<long long>(e0, std::numeric_limits<int>::max())),
        options.maxNeighbors});
    long long spent = integrated.evals;
    AlnsState initial;
    bool haveInitial = false;
    auto consider = [&](Candidate candidate) {
        candidate = makeMixedCandidate(in, std::move(candidate));
        Result result = simulate(in, candidate, in.C, false);
        ++spent;
        if (result.feasible && (!haveInitial || result.mean < initial.result.mean)) {
            initial = {std::move(candidate), std::move(result)};
            haveInitial = true;
        }
    };
    consider(integrated.candidate);
    consider(buildOrdered(in, orderByHeight(in), Assign::Score));
    consider(buildOrdered(in, orderByFastest(in), Assign::Score));
    std::mt19937_64 startRng(options.seed);
    consider(buildRandomized(in, startRng));
    if (!haveInitial) {
        integrated.strategy = "alns-joint";
        return integrated;
    }

    AlnsState bestGlobal = initial;
    long long remaining = std::max(0LL, E - spent);
    long long budget0 = remaining / 3;
    long long budget1 = remaining / 3;
    long long budget2 = remaining - budget0 - budget1;
    long long budgets[3] = {budget0, budget1, budget2};
    std::uint64_t seed0 = options.seed;
    std::uint64_t seed1 = seed0 ^ 0x9E3779B97F4A7C15ULL;
    std::uint64_t seed2 = seed0 ^ 0xD1B54A32D192ED03ULL;
    std::uint64_t seeds[3] = {seed0, seed1, seed2};
    long double F0 = std::isfinite(initial.result.mean) ? initial.result.mean : 1;
    long double T0 = -0.02L * F0 / std::log(0.5L);
    long double Tmin = 0.0002L * F0;
    long long lastIntensify = spent;

    for (int run = 0; run < 3; ++run) {
        AlnsState current = initial;
        AlnsState bestRun = initial;
        std::vector<long double> weights(4, 1.0L);
        std::vector<long long> usage(4, 0);
        std::vector<long long> reward(4, 0);
        std::mt19937_64 rng(seeds[run]);
        long long runSpent = 0;
        int attempts = 0;
        int d4Count = 0;
        while (runSpent < budgets[run] && spent < E) {
            int op = weightedPick(weights, rng);
            ++usage[op];
            Candidate trial = destroyRepair(in, current.candidate, current.result,
                                            op, rng, d4Count);
            Result trialResult = simulate(in, trial, in.C, false);
            ++runSpent;
            ++spent;
            ++attempts;
            bool improvesCurrent = betterAlns(trialResult, current.result);
            bool improvesBest = betterAlns(trialResult, bestGlobal.result);
            bool accepted = false;
            if (trialResult.feasible) {
                long double ratio = static_cast<long double>(spent) / E;
                long double temperature = T0 * std::pow(Tmin / T0, ratio);
                bool accept = improvesCurrent || trialResult.mean <= current.result.mean
                    || alnsRandom01(rng) < std::exp(-(trialResult.mean - current.result.mean)
                                                    / temperature);
                if (accept) {
                    current = {trial, trialResult};
                    accepted = true;
                }
                if (improvesBest) bestGlobal = {trial, trialResult};
                if (betterAlns(trialResult, bestRun.result)) bestRun = {trial, trialResult};
            }
            if (improvesBest) reward[op] += 5;
            else if (improvesCurrent) reward[op] += 2;
            else if (accepted) reward[op] += 1;
            if (attempts % 50 == 0) {
                for (int g = 0; g < 4; ++g) {
                    long double rate = static_cast<long double>(reward[g]) /
                                       std::max<long long>(1, usage[g]);
                    weights[g] = 0.8L * weights[g] + 0.2L * std::max(0.1L, rate);
                    usage[g] = 0;
                    reward[g] = 0;
                }
            }
            if (improvesBest || spent - lastIntensify >= 2500) {
                long long limit = std::min(250LL,
                    std::min(budgets[run] - runSpent, E - spent));
                if (limit > 0) {
                    long long used = intensifyMixed(in, current, limit, options, rng);
                    runSpent += used;
                    spent += used;
                    if (betterAlns(current.result, bestRun.result)) bestRun = current;
                    if (betterAlns(current.result, bestGlobal.result)) bestGlobal = current;
                    lastIntensify = spent;
                }
            }
        }
        if (betterAlns(bestRun.result, bestGlobal.result)) bestGlobal = bestRun;
    }

    Result final = simulate(in, bestGlobal.candidate, in.C, true);
    Outcome outcome;
    outcome.candidate = bestGlobal.candidate;
    outcome.result = final;
    outcome.batchLimit = in.C;
    outcome.evals = spent;
    outcome.strategy = "alns-joint";
    return outcome;
}


inline bool phaseTest(const Instance& in, SearchState& state,
                      const Candidate& next, int batch) {
    if (state.budget <= 0) return false;
    Result result = simulate(in, next, batch, false);
    --state.budget;
    ++state.evals;
    if (!result.feasible) return false;
    if (state.result.feasible && result.mean >= state.result.mean - 1e-9L)
        return false;
    state.candidate = next;
    state.result = std::move(result);
    state.batchLimit = batch;
    return true;
}

inline bool phaseImprove(const Instance& in, SearchState& state,
                         const Options& options, std::mt19937_64& rng) {
    int neighborhood = 0;
    bool changed = false;
    while (state.budget > 0 && neighborhood < 3) {
        bool improved = false;
        if (neighborhood == 0)
            improved = tryRelocate(in, state, rng, options.maxNeighbors, false);
        else if (neighborhood == 1)
            improved = trySwap(in, state, rng, options.maxNeighbors, false);
        else {
            for (int batch = 1; batch <= in.C && !improved; ++batch) {
                for (int bias = -5; bias <= 5 && !improved; ++bias) {
                    for (bool global : {false, true}) {
                        if (improved) break;
                        if (options.phaseGlobal >= 0 &&
                            global != (options.phaseGlobal == 1)) continue;
                        Candidate variant = state.candidate;
                        variant.phaseAware = true;
                        variant.phaseGlobal = global;
                        variant.phaseBias = bias;
                        if (variant.phaseBias == state.candidate.phaseBias &&
                            variant.phaseGlobal == state.candidate.phaseGlobal &&
                            batch == state.batchLimit) continue;
                        improved = phaseTest(in, state, variant, batch);
                    }
                }
            }
        }
        changed = changed || improved;
        neighborhood = improved ? 0 : neighborhood + 1;
    }
    return changed;
}

inline void runPhaseSearch(const Instance& in, SearchState& best, const Options& options) {
    SearchState current = best;
    for (int batch = 1; batch <= in.C; ++batch) {
        for (int bias = -5; bias <= 5; ++bias) {
            for (bool global : {false, true}) {
                if (options.phaseGlobal >= 0 &&
                    global != (options.phaseGlobal == 1)) continue;
                Candidate variant = best.candidate;
                variant.phaseAware = true;
                variant.mixedRoutes = false;
                variant.phaseGlobal = global;
                variant.phaseLookahead = options.phaseLookahead == 1;
                variant.phaseOutboundOnly = options.phaseOutboundOnly == 1;
                variant.phaseHardUnblock = options.phaseHardUnblock == 1;
                variant.phaseBias = bias;
                phaseTest(in, current, variant, batch);
            }
        }
    }
    if (betterIntegrated(current, best)) best = current;
    std::mt19937_64 rng(options.seed ^ 0xA0761D6478BD642FULL);
    int attempts = 0;
    while (current.budget > 0) {
        SearchState trial = current;
        if (attempts++ % 4 == 0) {
            int count = 0;
            trial.candidate = destroyRepair(in, trial.candidate, trial.result,
                                            static_cast<int>(rng() % 3), rng, count);
        } else {
            perturbIntegrated(in, trial, rng);
        }
        trial.candidate.phaseAware = true;
        trial.candidate.mixedRoutes = false;
        if (options.phaseGlobal < 0 && rng() % 3 == 0)
            trial.candidate.phaseGlobal = !trial.candidate.phaseGlobal;
        trial.candidate.phaseBias = std::clamp(
            trial.candidate.phaseBias + static_cast<int>(rng() % 3) - 1, -5, 5);
        trial.result = simulate(in, trial.candidate, trial.batchLimit, false);
        --trial.budget;
        ++trial.evals;
        if (trial.result.feasible)
            phaseImprove(in, trial, options, rng);
        if (betterIntegrated(trial, best)) best = trial;
        bool accept = betterIntegrated(trial, current) || attempts % 5 == 0;
        long long remaining = trial.budget, evaluated = trial.evals;
        current = accept && trial.result.feasible ? trial : best;
        current.budget = remaining;
        current.evals = evaluated;
    }
    best.evals = current.evals;
}

inline Outcome solvePhaseBucketMode(const Instance& in, const Options& options) {
    int budget = std::max(1, options.maxEvals);
    int initial = std::max(1, budget / 3);
    Outcome integrated = solveIntegrated(in,
        Options{options.seed, initial, options.maxNeighbors});
    Candidate start = integrated.candidate;
    start.phaseAware = true;
    start.mixedRoutes = false;
    start.phaseBias = 0;
    start.phaseGlobal = options.phaseGlobal == 1;
    start.phaseLookahead = options.phaseLookahead == 1;
    start.phaseOutboundOnly = options.phaseOutboundOnly == 1;
    start.phaseHardUnblock = options.phaseHardUnblock == 1;
    SearchState best;
    best.candidate = start;
    best.result = simulate(in, start, integrated.batchLimit, false);
    best.batchLimit = integrated.batchLimit;
    best.evals = integrated.evals + 1;
    best.budget = budget - std::min(budget, initial);
    if (best.budget > 0) --best.budget;
    runPhaseSearch(in, best, options);
    Outcome outcome;
    outcome.candidate = best.candidate;
    outcome.batchLimit = best.batchLimit;
    outcome.result = simulate(in, best.candidate, best.batchLimit, true);
    outcome.evals = best.evals;
    outcome.strategy = "phase-bucket";
    return outcome;
}

inline Outcome solvePhaseBucket(const Instance& in, const Options& options) {
    if (options.phaseGlobal < 0) {
        Options first = options;
        first.phaseGlobal = 0;
        Options second = options;
        second.phaseGlobal = 1;
        Outcome a = solvePhaseBucketMode(in, first);
        Outcome b = solvePhaseBucketMode(in, second);
        if (b.result.feasible && (!a.result.feasible || b.result.mean < a.result.mean)) {
            b.evals += a.evals;
            return b;
        }
        a.evals += b.evals;
        return a;
    }
    return solvePhaseBucketMode(in, options);
}

inline Outcome solvePhaseJoint(const Instance& in, const Options& options) {
    int total = std::max(1, options.maxEvals);
    Outcome outcome;
    outcome.strategy = "phase-joint";
    if (total < 4) {
        Candidate start = buildOrdered(in, orderByHeight(in), Assign::Score);
        start.phaseAware = true;
        start.mixedRoutes = false;
        start.phaseGlobal = false;
        start.phaseLookahead = false;
        start.phaseEnumerated = true;
        start.phaseJoint = true;
        outcome.candidate = start;
        outcome.batchLimit = in.C;
        outcome.result = simulate(in, start, in.C, true);
        outcome.evals = 1;
        return outcome;
    }
    int initial = std::max(1, total / 4);
    Outcome integrated = solveIntegrated(in,
        Options{options.seed, initial, options.maxNeighbors, 0, 0});
    Candidate start = integrated.candidate;
    start.phaseAware = true;
    start.mixedRoutes = false;
    start.phaseGlobal = false;
    start.phaseLookahead = false;
    start.phaseOutboundOnly = false;
    start.phaseHardUnblock = false;
    start.phaseEnumerated = true;
    start.phaseJoint = true;
    SearchState best;
    best.candidate = start;
    best.result = simulate(in, start, integrated.batchLimit, false);
    best.batchLimit = integrated.batchLimit;
    best.evals = integrated.evals + 1;
    best.budget = std::max(0, total - static_cast<int>(best.evals));
    Options searchOptions = options;
    searchOptions.phaseGlobal = 0;
    searchOptions.phaseLookahead = 0;
    runPhaseSearch(in, best, searchOptions);
    outcome.candidate = best.candidate;
    outcome.batchLimit = best.batchLimit;
    outcome.result = simulate(in, best.candidate, best.batchLimit, true);
    outcome.evals = best.evals;
    return outcome;
}

inline Outcome solve(const Instance& in, Strategy strategy, const Options& options) {
    Outcome outcome;
    outcome.strategy = strategyName(strategy);
    if (strategy == Strategy::Integrated) return solveIntegrated(in, options);
    if (strategy == Strategy::AlnsJoint) return solveAlnsJoint(in, options);
    if (strategy == Strategy::PhaseBucket) return solvePhaseBucket(in, options);
    if (strategy == Strategy::PhaseJoint) return solvePhaseJoint(in, options);

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
