#pragma once

#include "instance.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <queue>
#include <string>
#include <vector>

namespace io1 {

struct Candidate {
    std::vector<std::vector<int>> sequence;
    bool adaptive = false;
    int bias = 0;
    std::vector<std::int8_t> priorityIn;
    std::vector<std::int8_t> priorityOut;
    bool mixedRoutes = false;
    bool phaseAware = false;
    bool phaseGlobal = false;
    int phaseBias = 0;
};

enum class LogType { Process, Pickup, Deliver, Move };

struct LogEvent {
    LogType type;
    int resource;
    int lot;
    int from;
    int to;
    long double start;
};

struct Result {
    bool feasible = false;
    std::vector<long double> completion;
    std::vector<long double> processStart;
    std::vector<long double> processEnd;
    std::vector<LogEvent> log;
    std::vector<long double> blockedByMachine;
    std::array<int, 3> mixedRouteCounts{0, 0, 0};
    long double mean = std::numeric_limits<long double>::infinity();
    long double blockedTime = 0;
    int blockedCount = 0;
    int deliveries = 0;
    long double emptyTravel = 0;
    long double loadedTravel = 0;
    long double machineIdleTime = 0;
    int phaseInputTrips = 0;
    int phaseOutputTrips = 0;
};

namespace sim {

constexpr long double EPS = 1e-12L;
constexpr long double RELEASE_GUARD = 1e-8L;
constexpr long double MIXED_TRAVEL_WEIGHT = 0.35L;

enum class Status { Idle, Processing, Blocked };
enum class Action { PickIn, DropAlpha, PickBeta, DropFinal };

struct Step {
    int destination;
    Action action;
    int lot;
};

struct Machine {
    Status status = Status::Idle;
    int input = -1;
    int output = -1;
    int current = -1;
    std::size_t nextFeed = 0;
    bool inputReserved = false;
    bool outputReserved = false;
    long double outputSince = 0;
    long double blockStart = 0;
};

struct Agv {
    int position = 0;
    bool active = false;
    std::vector<int> load;
    std::vector<int> incoming;
    std::vector<Step> route;
    std::size_t step = 0;
};

struct Event {
    long double time;
    int kind;
    int id;
};

struct EventGreater {
    bool operator()(const Event& a, const Event& b) const {
        if (a.time != b.time) return a.time > b.time;
        if (a.kind != b.kind) return a.kind > b.kind;
        return a.id > b.id;
    }
};

struct MixedRoute {
    int type = 2;
    std::vector<int> inMachines;
    std::vector<int> inLots;
    std::vector<int> outMachines;
    std::vector<int> outLots;
    long double score = -std::numeric_limits<long double>::infinity();
    long double length = 0;
};

struct PhaseChoice {
    bool output = false;
    std::vector<int> machines;
    long double score = -std::numeric_limits<long double>::infinity();
};

}

inline Result simulate(const Instance& in, const Candidate& candidate, int batchLimit,
                       bool recordLog = true) {
    Result result;
    result.completion.assign(in.n, std::numeric_limits<long double>::infinity());
    result.processStart.assign(in.n, std::numeric_limits<long double>::infinity());
    result.processEnd.assign(in.n, std::numeric_limits<long double>::infinity());
    result.blockedByMachine.assign(in.m, 0);

    std::vector<sim::Machine> machines(in.m);
    std::vector<sim::Agv> agvs(in.K);
    std::vector<int> alphaMachine(in.locations, -1);
    std::vector<int> betaMachine(in.locations, -1);
    for (int j = 0; j < in.m; ++j) {
        alphaMachine[in.alpha[j]] = j;
        betaMachine[in.beta[j]] = j;
    }
    for (auto& agv : agvs) agv.position = in.o;

    std::priority_queue<sim::Event, std::vector<sim::Event>, sim::EventGreater> events;
    long double now = 0;
    int delivered = 0;
    std::vector<long double> idleSince(in.m, 0);

    auto startMachine = [&](int j) {
        auto& m = machines[j];
        int lot = m.input;
        m.input = -1;
        m.current = lot;
        m.status = sim::Status::Processing;
        result.machineIdleTime += now - idleSince[j];
        long double start = now + sim::RELEASE_GUARD;
        long double end = start + in.duration(lot, j);
        result.processStart[lot] = start;
        result.processEnd[lot] = end;
        if (recordLog) result.log.push_back({LogType::Process, j, lot, -1, -1, start});
        events.push({end, 0, j});
    };

    auto finishProcess = [&](int j) {
        auto& m = machines[j];
        int lot = m.current;
        if (m.output < 0) {
            m.output = lot;
            m.outputSince = now;
            m.current = -1;
            m.status = sim::Status::Idle;
            idleSince[j] = now;
        } else {
            m.status = sim::Status::Blocked;
            m.blockStart = now;
            ++result.blockedCount;
        }
    };

    auto hasOutput = [&]() {
        for (int j = 0; j < in.m; ++j) {
            if (machines[j].output >= 0 && !machines[j].outputReserved) return true;
        }
        return false;
    };

    auto hasInput = [&]() {
        for (int j = 0; j < in.m; ++j) {
            if (machines[j].input < 0 && !machines[j].inputReserved &&
                machines[j].nextFeed < candidate.sequence[j].size()) {
                return true;
            }
        }
        return false;
    };

    auto reserveInput = [&](std::vector<int>& chosen, std::vector<int>& lots, int j) {
        auto& m = machines[j];
        int lot = candidate.sequence[j][m.nextFeed++];
        m.inputReserved = true;
        chosen.push_back(j);
        lots.push_back(lot);
    };

    auto createInputRoute = [&](int k) {
        auto& agv = agvs[k];
        std::vector<int> chosen;
        std::vector<int> lots;
        int best = -1;
        for (int j = 0; j < in.m; ++j) {
            auto& m = machines[j];
            if (m.input >= 0 || m.inputReserved || m.nextFeed >= candidate.sequence[j].size()) continue;
            if (best < 0) {
                best = j;
                continue;
            }
            auto& b = machines[best];
            long double p = in.duration(candidate.sequence[j][m.nextFeed], j);
            long double bp = in.duration(candidate.sequence[best][b.nextFeed], best);
            bool idle = m.status == sim::Status::Idle;
            bool bestIdle = b.status == sim::Status::Idle;
            if (idle != bestIdle) {
                if (idle) best = j;
            } else if (p != bp) {
                if (p < bp) best = j;
            } else if (j < best) {
                best = j;
            }
        }
        if (best < 0) return false;
        reserveInput(chosen, lots, best);
        while (static_cast<int>(chosen.size()) < batchLimit) {
            int last = in.alpha[chosen.back()];
            int next = -1;
            long double nextDistance = 0;
            for (int j = 0; j < in.m; ++j) {
                auto& m = machines[j];
                if (m.input >= 0 || m.inputReserved || m.nextFeed >= candidate.sequence[j].size()) continue;
                long double distance = in.tau[last][in.alpha[j]];
                if (next < 0 || distance < nextDistance ||
                    (distance == nextDistance && j < next)) {
                    next = j;
                    nextDistance = distance;
                }
            }
            if (next < 0) break;
            reserveInput(chosen, lots, next);
        }
        agv.route.clear();
        agv.route.push_back({in.o, sim::Action::PickIn, -1});
        for (std::size_t i = 0; i < chosen.size(); ++i) {
            agv.route.push_back({in.alpha[chosen[i]], sim::Action::DropAlpha, lots[i]});
        }
        agv.incoming = lots;
        agv.load.clear();
        agv.step = 0;
        agv.active = true;
        if (recordLog && agv.position != in.o) {
            result.log.push_back({LogType::Move, k, -1, agv.position, in.o, now});
        }
        events.push({now + in.tau[agv.position][in.o] + sim::RELEASE_GUARD, 1, k});
        return true;
    };

    auto reserveOutput = [&](std::vector<int>& chosen, std::vector<int>& lots, int j) {
        auto& m = machines[j];
        m.outputReserved = true;
        chosen.push_back(j);
        lots.push_back(m.output);
    };

    auto createOutputRoute = [&](int k) {
        auto& agv = agvs[k];
        std::vector<int> chosen;
        std::vector<int> lots;
        int best = -1;
        for (int j = 0; j < in.m; ++j) {
            auto& m = machines[j];
            if (m.output < 0 || m.outputReserved) continue;
            if (best < 0) {
                best = j;
                continue;
            }
            auto& b = machines[best];
            bool blocked = m.status == sim::Status::Blocked;
            bool bestBlocked = b.status == sim::Status::Blocked;
            long double distance = in.tau[agv.position][in.beta[j]];
            long double bestDistance = in.tau[agv.position][in.beta[best]];
            if (blocked != bestBlocked) {
                if (blocked) best = j;
            } else if (m.outputSince != b.outputSince) {
                if (m.outputSince < b.outputSince) best = j;
            } else if (distance != bestDistance) {
                if (distance < bestDistance) best = j;
            } else if (j < best) {
                best = j;
            }
        }
        if (best < 0) return false;
        reserveOutput(chosen, lots, best);
        while (static_cast<int>(chosen.size()) < batchLimit) {
            int last = in.beta[chosen.back()];
            int next = -1;
            long double nextDistance = 0;
            for (int j = 0; j < in.m; ++j) {
                auto& m = machines[j];
                if (m.output < 0 || m.outputReserved) continue;
                long double distance = in.tau[last][in.beta[j]];
                if (next < 0 || distance < nextDistance ||
                    (distance == nextDistance && j < next)) {
                    next = j;
                    nextDistance = distance;
                }
            }
            if (next < 0) break;
            reserveOutput(chosen, lots, next);
        }
        agv.route.clear();
        for (std::size_t i = 0; i < chosen.size(); ++i) {
            agv.route.push_back({in.beta[chosen[i]], sim::Action::PickBeta, lots[i]});
        }
        agv.route.push_back({in.f, sim::Action::DropFinal, -1});
        agv.incoming.clear();
        agv.load.clear();
        agv.step = 0;
        agv.active = true;
        int first = in.beta[chosen.front()];
        if (recordLog && agv.position != first) {
            result.log.push_back({LogType::Move, k, -1, agv.position, first, now});
        }
        events.push({now + in.tau[agv.position][first] + sim::RELEASE_GUARD, 1, k});
        return true;
    };


    auto createMixedRoute = [&](int k) {
        auto& agv = agvs[k];
        const long double H = in.medianTau > 0 ? in.medianTau : 1.0L;
        auto priorityIn = [&](int lot) {
            return candidate.priorityIn.empty() ? 0.0L
                : static_cast<long double>(candidate.priorityIn[lot]);
        };
        auto priorityOut = [&](int lot) {
            return candidate.priorityOut.empty() ? 0.0L
                : static_cast<long double>(candidate.priorityOut[lot]);
        };
        auto eligibleInput = [&](int j) {
            return machines[j].input < 0 && !machines[j].inputReserved &&
                   machines[j].nextFeed < candidate.sequence[j].size();
        };
        auto eligibleOutput = [&](int j) {
            return machines[j].output >= 0 && !machines[j].outputReserved;
        };
        auto inputUrgency = [&](int j, int lot) {
            auto& m = machines[j];
            long double ready = m.status == sim::Status::Idle ? now
                : m.status == sim::Status::Processing ? result.processEnd[m.current]
                : now + H;
            long double distance = in.tau[agv.position][in.o] + in.tau[in.o][in.alpha[j]];
            long double value = (m.status == sim::Status::Idle ? 2 * H : 0)
                + std::max(0.0L, now + distance - ready)
                + 0.25L * H * priorityIn(lot) - 0.25L * distance;
            return value;
        };
        auto outputUrgency = [&](int j, int lot) {
            auto& m = machines[j];
            long double end = m.status == sim::Status::Processing
                ? result.processEnd[m.current] : now;
            long double distance = in.tau[agv.position][in.beta[j]];
            long double value = (m.status == sim::Status::Blocked ? 4 * H : 0)
                + 1.5L * std::max(0.0L, now + distance - end)
                + 0.25L * (now - m.outputSince)
                + 0.25L * H * priorityOut(lot) - 0.25L * distance;
            return value;
        };

        struct Seed { long double score; int machine; int lot; };
        std::vector<Seed> inputSeeds;
        std::vector<Seed> outputSeeds;
        for (int j = 0; j < in.m; ++j) {
            if (eligibleInput(j)) {
                int lot = candidate.sequence[j][machines[j].nextFeed];
                inputSeeds.push_back({inputUrgency(j, lot), j, lot});
            }
            if (eligibleOutput(j)) {
                int lot = machines[j].output;
                outputSeeds.push_back({outputUrgency(j, lot), j, lot});
            }
        }
        auto seedLess = [&](const Seed& a, const Seed& b) {
            if (a.score != b.score) return a.score > b.score;
            if (in.lots[a.lot].id != in.lots[b.lot].id)
                return in.lots[a.lot].id < in.lots[b.lot].id;
            return a.machine < b.machine;
        };
        std::sort(inputSeeds.begin(), inputSeeds.end(), seedLess);
        std::sort(outputSeeds.begin(), outputSeeds.end(), seedLess);
        if (inputSeeds.size() > 5) inputSeeds.resize(5);
        if (outputSeeds.size() > 5) outputSeeds.resize(5);

        auto buildInputPhase = [&](const Seed& seed, sim::MixedRoute& route) {
            route.type = 2;
            route.inMachines = {seed.machine};
            route.inLots = {seed.lot};
            std::vector<char> used(in.m, 0);
            used[seed.machine] = 1;
            long double sum = seed.score;
            long double length = in.tau[agv.position][in.o] +
                                 in.tau[in.o][in.alpha[seed.machine]];
            while (static_cast<int>(route.inLots.size()) < in.C) {
                int last = in.alpha[route.inMachines.back()];
                int best = -1;
                int bestLot = -1;
                long double bestValue = 0;
                for (int j = 0; j < in.m; ++j) {
                    if (used[j] || !eligibleInput(j)) continue;
                    int lot = candidate.sequence[j][machines[j].nextFeed];
                    long double extra = in.tau[last][in.alpha[j]];
                    long double value = inputUrgency(j, lot) - sim::MIXED_TRAVEL_WEIGHT * extra;
                    if (best < 0 || value > bestValue ||
                        (value == bestValue && j < best)) {
                        best = j;
                        bestLot = lot;
                        bestValue = value;
                    }
                }
                if (best < 0 || bestValue <= 0) break;
                used[best] = 1;
                route.inMachines.push_back(best);
                route.inLots.push_back(bestLot);
                sum += inputUrgency(best, bestLot);
                length += in.tau[last][in.alpha[best]];
            }
            route.length = length;
            route.score = sum - sim::MIXED_TRAVEL_WEIGHT * length;
        };

        auto buildOutputPhase = [&](const Seed& seed, sim::MixedRoute& route) {
            route.type = 1;
            route.outMachines = {seed.machine};
            route.outLots = {seed.lot};
            std::vector<char> used(in.m, 0);
            used[seed.machine] = 1;
            long double sum = seed.score;
            long double length = in.tau[agv.position][in.beta[seed.machine]];
            while (static_cast<int>(route.outLots.size()) < in.C) {
                int last = in.beta[route.outMachines.back()];
                int best = -1;
                int bestLot = -1;
                long double bestValue = 0;
                for (int j = 0; j < in.m; ++j) {
                    if (used[j] || !eligibleOutput(j)) continue;
                    int lot = machines[j].output;
                    long double extra = in.tau[last][in.beta[j]];
                    long double value = outputUrgency(j, lot) - sim::MIXED_TRAVEL_WEIGHT * extra;
                    if (best < 0 || value > bestValue ||
                        (value == bestValue && j < best)) {
                        best = j;
                        bestLot = lot;
                        bestValue = value;
                    }
                }
                if (best < 0 || bestValue <= 0) break;
                used[best] = 1;
                route.outMachines.push_back(best);
                route.outLots.push_back(bestLot);
                sum += outputUrgency(best, bestLot);
                length += in.tau[last][in.beta[best]];
            }
            length += in.tau[in.beta[route.outMachines.back()]][in.f];
            route.length = length;
            route.score = sum - sim::MIXED_TRAVEL_WEIGHT * length;
        };

        auto buildMixedPhase = [&](const Seed& seed, sim::MixedRoute& route) {
            buildInputPhase(seed, route);
            std::vector<char> used(in.m, 0);
            int lastLocation = in.alpha[route.inMachines.back()];
            long double sum = route.score + sim::MIXED_TRAVEL_WEIGHT * route.length;
            long double length = route.length;
            while (static_cast<int>(route.outLots.size()) < in.C) {
                int best = -1;
                int bestLot = -1;
                long double bestValue = 0;
                for (int j = 0; j < in.m; ++j) {
                    if (used[j] || !eligibleOutput(j)) continue;
                    int lot = machines[j].output;
                    long double extra = in.tau[lastLocation][in.beta[j]];
                    long double value = outputUrgency(j, lot) - sim::MIXED_TRAVEL_WEIGHT * extra;
                    if (best < 0 || value > bestValue ||
                        (value == bestValue && j < best)) {
                        best = j;
                        bestLot = lot;
                        bestValue = value;
                    }
                }
                if (best < 0 || bestValue <= 0) break;
                used[best] = 1;
                route.outMachines.push_back(best);
                route.outLots.push_back(bestLot);
                sum += outputUrgency(best, bestLot);
                length += in.tau[lastLocation][in.beta[best]];
                lastLocation = in.beta[best];
            }
            if (route.outLots.empty()) {
                route.score = -std::numeric_limits<long double>::infinity();
                return;
            }
            length += in.tau[lastLocation][in.f];
            route.type = 0;
            route.length = length;
            route.score = sum - sim::MIXED_TRAVEL_WEIGHT * length;
        };

        auto routeBetter = [&](const sim::MixedRoute& a, const sim::MixedRoute& b) {
            if (a.score > b.score + sim::EPS) return true;
            if (b.score > a.score + sim::EPS) return false;
            int ta = static_cast<int>(a.inLots.size() + a.outLots.size());
            int tb = static_cast<int>(b.inLots.size() + b.outLots.size());
            if (ta != tb) return ta > tb;
            if (a.length != b.length) return a.length < b.length;
            if (a.type != b.type) return a.type < b.type;
            if (a.inMachines != b.inMachines) return a.inMachines < b.inMachines;
            if (a.inLots != b.inLots) return a.inLots < b.inLots;
            if (a.outMachines != b.outMachines) return a.outMachines < b.outMachines;
            return a.outLots < b.outLots;
        };

        std::vector<sim::MixedRoute> routes;
        for (const Seed& seed : inputSeeds) {
            sim::MixedRoute input;
            buildInputPhase(seed, input);
            routes.push_back(input);
            sim::MixedRoute mixed;
            buildMixedPhase(seed, mixed);
            if (std::isfinite(mixed.score)) routes.push_back(mixed);
        }
        for (const Seed& seed : outputSeeds) {
            sim::MixedRoute output;
            buildOutputPhase(seed, output);
            routes.push_back(output);
        }
        if (routes.empty()) return false;
        sim::MixedRoute best = routes.front();
        for (const auto& route : routes) {
            if (routeBetter(route, best)) best = route;
        }
        ++result.mixedRouteCounts[best.type];

        for (int j : best.inMachines) {
            machines[j].inputReserved = true;
            ++machines[j].nextFeed;
        }
        for (int j : best.outMachines) {
            machines[j].outputReserved = true;
        }
        agv.route.clear();
        if (!best.inLots.empty()) {
            agv.route.push_back({in.o, sim::Action::PickIn, -1});
            for (std::size_t i = 0; i < best.inLots.size(); ++i) {
                agv.route.push_back({in.alpha[best.inMachines[i]], sim::Action::DropAlpha,
                                     best.inLots[i]});
            }
        }
        for (std::size_t i = 0; i < best.outLots.size(); ++i) {
            agv.route.push_back({in.beta[best.outMachines[i]], sim::Action::PickBeta,
                                 best.outLots[i]});
        }
        if (!best.outLots.empty()) {
            agv.route.push_back({in.f, sim::Action::DropFinal, -1});
        }
        agv.incoming = best.inLots;
        agv.load.clear();
        agv.step = 0;
        agv.active = true;
        int first = agv.route.front().destination;
        if (recordLog && agv.position != first) {
            result.log.push_back({LogType::Move, k, -1, agv.position, first, now});
        }
        events.push({now + in.tau[agv.position][first] + sim::RELEASE_GUARD, 1, k});
        return true;
    };

    auto inputUrgency = [&](int k, int j) {
        const auto& machine = machines[j];
        if (machine.input >= 0 || machine.inputReserved ||
            machine.nextFeed >= candidate.sequence[j].size()) {
            return -std::numeric_limits<long double>::infinity();
        }
        if (machine.status == sim::Status::Blocked) {
            return -std::numeric_limits<long double>::infinity();
        }
        long double travel = in.tau[agvs[k].position][in.o] +
                             in.tau[in.o][in.alpha[j]];
        long double ready = machine.status == sim::Status::Idle
            ? now : result.processEnd[machine.current];
        long double value = std::max(0.0L, now + travel - ready) - 0.10L * travel;
        if (machine.status == sim::Status::Idle) {
            int lot = candidate.sequence[j][machine.nextFeed];
            value += 0.15L * in.duration(lot, j);
        }
        return value;
    };

    auto outputUrgency = [&](int k, int j) {
        const auto& machine = machines[j];
        if (machine.output < 0 || machine.outputReserved) {
            return -std::numeric_limits<long double>::infinity();
        }
        long double travel = in.tau[agvs[k].position][in.beta[j]];
        long double deadline = machine.status == sim::Status::Processing
            ? result.processEnd[machine.current] : now;
        long double scale = std::max(1.0L, in.tau[in.o][in.f] / 10.0L);
        return std::max(0.0L, now + travel - deadline)
            + 0.25L * (now - machine.outputSince) - 0.10L * travel
            + candidate.bias * scale;
    };

    auto preferOutput = [&](int k) {
        long double inScore = -std::numeric_limits<long double>::infinity();
        long double outScore = -std::numeric_limits<long double>::infinity();
        for (int j = 0; j < in.m; ++j) {
            inScore = std::max(inScore, inputUrgency(k, j));
            outScore = std::max(outScore, outputUrgency(k, j));
        }
        return outScore >= inScore;
    };


    auto phaseBestChoice = [&](int k) {
        auto& agv = agvs[k];
        const long double H = std::max(1.0L, in.medianTau);
        sim::PhaseChoice best;
        auto eligible = [&](int j, bool output) {
            const auto& m = machines[j];
            return output ? (m.output >= 0 && !m.outputReserved)
                : (m.input < 0 && !m.inputReserved &&
                   m.nextFeed < candidate.sequence[j].size());
        };
        auto location = [&](int j, bool output) {
            return output ? in.beta[j] : in.alpha[j];
        };
        auto scoreRoute = [&](const sim::PhaseChoice& route) {
            long double t = route.output ? 0 : in.tau[agv.position][in.o];
            int last = route.output ? agv.position : in.o;
            long double benefit = 0;
            for (int j : route.machines) {
                t += in.tau[last][location(j, route.output)];
                const auto& m = machines[j];
                if (route.output) {
                    bool work = m.nextFeed < candidate.sequence[j].size() ||
                                m.input >= 0 || m.inputReserved;
                    benefit += (m.status == sim::Status::Blocked ? 16.0L : 6.0L) * H
                             + (work ? 2.0L : 0.0L) * H
                             + 0.20L * (now - m.outputSince)
                             + static_cast<long double>(candidate.phaseBias) * H;
                    benefit -= 0.30L * t;
                } else {
                    int lot = candidate.sequence[j][m.nextFeed];
                    long double ready = m.status == sim::Status::Idle ? 0
                        : m.status == sim::Status::Processing
                            ? std::max(0.0L, result.processEnd[m.current] - now)
                            : 2 * H;
                    benefit += (m.status == sim::Status::Idle ? 10.0L : 5.0L) * H
                             + H / (1.0L + in.duration(lot, j) / H)
                             - static_cast<long double>(candidate.phaseBias) * H;
                    benefit -= 0.35L * std::max(0.0L, t - ready);
                    if (m.output >= 0) benefit -= 4 * H;
                }
                last = location(j, route.output);
            }
            if (route.output) t += in.tau[last][in.f];
            return (benefit - 1.5L * t) / (H + t);
        };
        for (bool output : {false, true}) {
            for (int seed = 0; seed < in.m; ++seed) {
                if (!eligible(seed, output)) continue;
                sim::PhaseChoice route;
                route.output = output;
                route.machines.push_back(seed);
                long double score = scoreRoute(route);
                if (score > best.score) {
                    best = route;
                    best.score = score;
                }
                while (static_cast<int>(route.machines.size()) < batchLimit) {
                    int next = -1;
                    long double nextScore = -std::numeric_limits<long double>::infinity();
                    for (int j = 0; j < in.m; ++j) {
                        if (!eligible(j, output) ||
                            std::find(route.machines.begin(), route.machines.end(), j)
                                != route.machines.end()) continue;
                        sim::PhaseChoice trial = route;
                        trial.machines.push_back(j);
                        long double value = scoreRoute(trial);
                        if (value > nextScore) {
                            nextScore = value;
                            next = j;
                        }
                    }
                    if (next < 0) break;
                    route.machines.push_back(next);
                    if (nextScore > best.score) {
                        best = route;
                        best.score = nextScore;
                    }
                }
            }
        }
        return best;
    };

    auto applyPhaseChoice = [&](int k, const sim::PhaseChoice& best) {
        auto& agv = agvs[k];
        agv.route.clear();
        agv.incoming.clear();
        agv.load.clear();
        if (best.output) {
            ++result.phaseOutputTrips;
            for (int j : best.machines) {
                int lot = machines[j].output;
                machines[j].outputReserved = true;
                agv.route.push_back({in.beta[j], sim::Action::PickBeta, lot});
            }
            agv.route.push_back({in.f, sim::Action::DropFinal, -1});
        } else {
            ++result.phaseInputTrips;
            agv.route.push_back({in.o, sim::Action::PickIn, -1});
            for (int j : best.machines) {
                auto& m = machines[j];
                int lot = candidate.sequence[j][m.nextFeed++];
                m.inputReserved = true;
                agv.incoming.push_back(lot);
                agv.route.push_back({in.alpha[j], sim::Action::DropAlpha, lot});
            }
        }
        agv.step = 0;
        agv.active = true;
        int first = agv.route.front().destination;
        if (recordLog && agv.position != first)
            result.log.push_back({LogType::Move, k, -1, agv.position, first, now});
        result.emptyTravel += in.tau[agv.position][first];
        events.push({now + in.tau[agv.position][first] + sim::RELEASE_GUARD, 1, k});
    };

    auto settle = [&]() {
        for (int j = 0; j < in.m; ++j) {
            if (machines[j].status == sim::Status::Idle && machines[j].input >= 0) {
                startMachine(j);
            }
        }
        for (int k = 0; k < in.K; ++k) {
            if (agvs[k].active) continue;
            if (candidate.phaseAware) {
                if (candidate.phaseGlobal) {
                    std::vector<char> free(in.K, 0);
                    for (int q = 0; q < in.K; ++q) free[q] = agvs[q].active ? 0 : 1;
                    while (true) {
                        int bestK = -1;
                        sim::PhaseChoice bestChoice;
                        for (int q = 0; q < in.K; ++q) {
                            if (!free[q]) continue;
                            sim::PhaseChoice choice = phaseBestChoice(q);
                            if (choice.machines.empty()) continue;
                            bool tie = std::fabs(choice.score - bestChoice.score) <= sim::EPS;
                            if (bestK < 0 || choice.score > bestChoice.score + sim::EPS ||
                                (tie && q < bestK)) {
                                bestK = q;
                                bestChoice = std::move(choice);
                            }
                        }
                        if (bestK < 0) break;
                        applyPhaseChoice(bestK, bestChoice);
                        free[bestK] = 0;
                    }
                } else {
                    for (int q = 0; q < in.K; ++q) {
                        if (agvs[q].active) continue;
                        sim::PhaseChoice choice = phaseBestChoice(q);
                        if (!choice.machines.empty()) applyPhaseChoice(q, choice);
                    }
                }
                continue;
            }
            if (candidate.mixedRoutes) {
                createMixedRoute(k);
                continue;
            }
            bool out = hasOutput(), input = hasInput();
            if (!out && !input) continue;
            bool takeOut = out && (!input || !candidate.adaptive || preferOutput(k));
            if (takeOut) createOutputRoute(k);
            else createInputRoute(k);
        }
    };

    auto arrive = [&](int k) {
        auto& agv = agvs[k];
        sim::Step step = agv.route[agv.step];
        if (step.action == sim::Action::PickIn) {
            if (recordLog) {
                for (int lot : agv.incoming) {
                    result.log.push_back({LogType::Pickup, k, lot, in.o, -1, now});
                }
            }
            agv.load = agv.incoming;
            agv.incoming.clear();
        } else if (step.action == sim::Action::DropAlpha) {
            int j = alphaMachine[step.destination];
            machines[j].input = step.lot;
            machines[j].inputReserved = false;
            agv.load.erase(std::find(agv.load.begin(), agv.load.end(), step.lot));
            if (recordLog) result.log.push_back({LogType::Deliver, k, step.lot, step.destination, -1, now});
        } else if (step.action == sim::Action::PickBeta) {
            int j = betaMachine[step.destination];
            auto& m = machines[j];
            m.output = -1;
            m.outputReserved = false;
            if (m.status == sim::Status::Blocked) {
                m.output = m.current;
                m.current = -1;
                m.status = sim::Status::Idle;
                idleSince[j] = now;
                m.outputSince = now;
                result.blockedTime += now - m.blockStart;
                result.blockedByMachine[j] += now - m.blockStart;
            }
            agv.load.push_back(step.lot);
            if (recordLog) result.log.push_back({LogType::Pickup, k, step.lot, step.destination, -1, now});
        } else {
            for (int lot : agv.load) {
                result.completion[lot] = now;
                if (recordLog) result.log.push_back({LogType::Deliver, k, lot, in.f, -1, now});
            }
            delivered += static_cast<int>(agv.load.size());
            agv.load.clear();
        }
        agv.position = step.destination;
        ++agv.step;
        if (agv.step < agv.route.size()) {
            int destination = agv.route[agv.step].destination;
            if (recordLog && agv.position != destination) {
                result.log.push_back({LogType::Move, k, -1, agv.position, destination, now});
            }
            if (!agv.load.empty()) result.loadedTravel += in.tau[agv.position][destination];
            else result.emptyTravel += in.tau[agv.position][destination];
            events.push({now + in.tau[agv.position][destination] + sim::RELEASE_GUARD, 1, k});
        } else {
            agv.active = false;
            agv.route.clear();
            agv.step = 0;
        }
    };

    settle();
    while (delivered < in.n && !events.empty()) {
        now = events.top().time;
        std::vector<sim::Event> batch;
        while (!events.empty() && events.top().time <= now + sim::EPS) {
            batch.push_back(events.top());
            events.pop();
        }
        std::sort(batch.begin(), batch.end(), [](const sim::Event& a, const sim::Event& b) {
            if (a.kind != b.kind) return a.kind < b.kind;
            return a.id < b.id;
        });
        for (const auto& event : batch) {
            if (event.kind == 0) finishProcess(event.id);
            else arrive(event.id);
        }
        settle();
    }

    if (delivered != in.n) return result;
    long double total = 0;
    for (long double value : result.completion) total += value;
    result.deliveries = delivered;
    result.mean = total / in.n;
    result.feasible = true;
    return result;
}

}
