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

struct RecordedRoute {
    bool output = false;
    std::vector<int> machines;
    long double score = 0;
};

struct Candidate {
    std::vector<std::vector<int>> sequence;
    bool adaptive = false;
    int bias = 0;
    std::vector<std::int8_t> priorityIn;
    std::vector<std::int8_t> priorityOut;
    bool mixedRoutes = false;
    bool phaseAware = false;
    bool phaseGlobal = false;
    bool phaseLookahead = false;
    bool phaseEnumerated = false;
    bool phaseJoint = false;
    bool phaseOutboundOnly = false;
    bool phaseHardUnblock = false;
    bool learnedPolicy = false;
    std::array<double, 9> dispatchWeights{1, 0, 0, 0, 0, 0, 0, 0, 0};
    bool collectFirst = false;
    bool forcedFirst = false;
    int forcedAgv = -1;
    bool forcedOutput = false;
    std::vector<int> forcedMachines;
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
    std::array<long double, 9> featureSum{};
    std::array<long double, 9> featureSquareSum{};
    long long featureCount = 0;
    long long learnedChoiceCount = 0;
    long long learnedChoiceChanges = 0;
    std::vector<RecordedRoute> firstRoutes;
    RecordedRoute firstChosen;
    int firstAgv = -1;
    bool hasFirst = false;
    long double mean = std::numeric_limits<long double>::infinity();
    long double makespan = 0;
    long double blockedTime = 0;
    int blockedCount = 0;
    int deliveries = 0;
    long double emptyTravel = 0;
    long double loadedTravel = 0;
    long double machineIdleTime = 0;
    int phaseInputTrips = 0;
    int phaseOutputTrips = 0;
    int phaseInputLots = 0;
    int phaseOutputLots = 0;
    std::vector<long double> emptyTravelByAgv;
    std::vector<long double> loadedTravelByAgv;
    std::vector<long double> idleVehicleTimeByAgv;
    std::vector<long double> machineStarvedByMachine;
    long long dispatchEpochCount = 0;
    long long multiFreeEpochCount = 0;
    int maxSimultaneouslyFree = 0;
    long long candidateRoutes = 0;
    long long dfsNodes = 0;
    int jointChoicesUsed = 0;
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
    long double travel = 0;
    std::uint32_t inputMask = 0;
    std::uint32_t outputMask = 0;
};

}

inline Result simulate(const Instance& in, const Candidate& candidate, int batchLimit,
                       bool recordLog = true) {
    Result result;
    result.completion.assign(in.n, std::numeric_limits<long double>::infinity());
    result.processStart.assign(in.n, std::numeric_limits<long double>::infinity());
    result.processEnd.assign(in.n, std::numeric_limits<long double>::infinity());
    result.blockedByMachine.assign(in.m, 0);
    result.emptyTravelByAgv.assign(in.K, 0);
    result.loadedTravelByAgv.assign(in.K, 0);
    result.idleVehicleTimeByAgv.assign(in.K, 0);
    result.machineStarvedByMachine.assign(in.m, 0);

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
    std::vector<long double> vehicleIdleSince(in.K, 0);
    std::vector<long double> starvedSince(in.m, 0);
    int dispatchEpoch = 0;
    bool firstDispatchDone = false;

    auto markAgvBusy = [&](int k) {
        if (vehicleIdleSince[k] >= 0) {
            result.idleVehicleTimeByAgv[k] += now - vehicleIdleSince[k];
            vehicleIdleSince[k] = -1;
        }
    };
    auto markAgvIdle = [&](int k) {
        vehicleIdleSince[k] = now;
    };

    auto startMachine = [&](int j) {
        auto& m = machines[j];
        int lot = m.input;
        m.input = -1;
        m.current = lot;
        m.status = sim::Status::Processing;
        result.machineIdleTime += now - idleSince[j];
        if (starvedSince[j] >= 0) {
            result.machineStarvedByMachine[j] += now - starvedSince[j];
            starvedSince[j] = -1;
        }
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
            starvedSince[j] = (m.input < 0 && m.nextFeed < candidate.sequence[j].size())
                ? now : -1;
        } else {
            m.status = sim::Status::Blocked;
            m.blockStart = now;
            starvedSince[j] = -1;
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
        markAgvBusy(k);
        ++result.phaseInputTrips;
        result.phaseInputLots += static_cast<int>(lots.size());
        if (recordLog && agv.position != in.o) {
            result.log.push_back({LogType::Move, k, -1, agv.position, in.o, now});
        }
        result.emptyTravel += in.tau[agv.position][in.o];
        result.emptyTravelByAgv[k] += in.tau[agv.position][in.o];
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
        markAgvBusy(k);
        ++result.phaseOutputTrips;
        result.phaseOutputLots += static_cast<int>(lots.size());
        int first = in.beta[chosen.front()];
        if (recordLog && agv.position != first) {
            result.log.push_back({LogType::Move, k, -1, agv.position, first, now});
        }
        result.emptyTravel += in.tau[agv.position][first];
        result.emptyTravelByAgv[k] += in.tau[agv.position][first];
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
        markAgvBusy(k);
        if (recordLog && agv.position != first) {
            result.log.push_back({LogType::Move, k, -1, agv.position, first, now});
        }
        result.emptyTravel += in.tau[agv.position][first];
        result.emptyTravelByAgv[k] += in.tau[agv.position][first];
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


    auto phaseEligible = [&](int j, bool output, bool allowFuture) {
        const auto& m = machines[j];
        if (output) {
            if (m.output >= 0) return !m.outputReserved;
            return allowFuture && !m.outputReserved && m.status == sim::Status::Processing;
        }
        return m.input < 0 && !m.inputReserved &&
               m.nextFeed < candidate.sequence[j].size();
    };
    auto phaseLocation = [&](int j, bool output) {
        return output ? in.beta[j] : in.alpha[j];
    };
    auto phaseTravel = [&](int k, const sim::PhaseChoice& route) {
        long double t = route.output ? 0 : in.tau[agvs[k].position][in.o];
        int last = route.output ? agvs[k].position : in.o;
        for (int j : route.machines) {
            t += in.tau[last][phaseLocation(j, route.output)];
            last = phaseLocation(j, route.output);
        }
        if (route.output) t += in.tau[last][in.f];
        return t;
    };
    auto phaseMasks = [&](sim::PhaseChoice& route) {
        route.inputMask = 0;
        route.outputMask = 0;
        for (int j : route.machines) {
            if (route.output) route.outputMask |= (1u << j);
            else route.inputMask |= (1u << j);
        }
    };
    auto phaseScoreRoute = [&](int k, const sim::PhaseChoice& route) {
        (void)k;
        const long double H = std::max(1.0L, in.medianTau);
        const auto& agv = agvs[k];
        long double t = route.output ? 0 : in.tau[agv.position][in.o];
        int last = route.output ? agv.position : in.o;
        long double benefit = 0;
        for (int j : route.machines) {
            t += in.tau[last][phaseLocation(j, route.output)];
            const auto& m = machines[j];
            if (route.output) {
                bool future = m.output < 0;
                bool work = m.nextFeed < candidate.sequence[j].size() ||
                            m.input >= 0 || m.inputReserved;
                long double ready = future ? result.processEnd[m.current] : now;
                long double wait = std::max(0.0L, ready - (now + t));
                benefit += (m.status == sim::Status::Blocked ? 16.0L : 6.0L) * H
                         + (work ? 2.0L : 0.0L) * H
                         + (future ? 0.0L : 0.20L * (now - m.outputSince))
                         + static_cast<long double>(candidate.phaseBias) * H;
                benefit -= 0.30L * t + 0.75L * wait;
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
            last = phaseLocation(j, route.output);
        }
        if (route.output) t += in.tau[last][in.f];
        return (benefit - 1.5L * t) / (H + t);
    };
    auto phaseTaskLot = [&](int j, bool output) {
        return output ? machines[j].output : candidate.sequence[j][machines[j].nextFeed];
    };
    auto phaseRouteLess = [&](const sim::PhaseChoice& a, const sim::PhaseChoice& b) {
        if (a.score != b.score) return a.score > b.score;
        if (a.travel != b.travel) return a.travel < b.travel;
        if (a.output != b.output) return a.output;
        return a.machines < b.machines;
    };

    auto phaseBestChoice = [&](int k) {
        sim::PhaseChoice best;
        bool anyCurrentOutput = false;
        bool anyInput = false;
        for (int j = 0; j < in.m; ++j) {
            anyCurrentOutput = anyCurrentOutput || phaseEligible(j, true, false);
            anyInput = anyInput || phaseEligible(j, false, false);
        }
        const bool allowFuture = candidate.phaseLookahead && !anyCurrentOutput && !anyInput;
        for (bool output : {false, true}) {
            for (int seed = 0; seed < in.m; ++seed) {
                if (!phaseEligible(seed, output, output && allowFuture)) continue;
                sim::PhaseChoice route;
                route.output = output;
                route.machines.push_back(seed);
                long double score = phaseScoreRoute(k, route);
                if (score > best.score) {
                    best = route;
                    best.score = score;
                    best.travel = phaseTravel(k, best);
                }
                while (static_cast<int>(route.machines.size()) < batchLimit) {
                    int next = -1;
                    long double nextScore = -std::numeric_limits<long double>::infinity();
                    for (int j = 0; j < in.m; ++j) {
                        if (!phaseEligible(j, output, false) ||
                            std::find(route.machines.begin(), route.machines.end(), j)
                                != route.machines.end()) continue;
                        sim::PhaseChoice trial = route;
                        trial.machines.push_back(j);
                        long double value = phaseScoreRoute(k, trial);
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
                        best.travel = phaseTravel(k, best);
                    }
                }
            }
        }
        phaseMasks(best);
        return best;
    };

    auto phaseGeneratedChoices = [&](int k) {
        std::vector<sim::PhaseChoice> choices;
        for (bool output : {false, true}) {
            for (int seed = 0; seed < in.m; ++seed) {
                if (!phaseEligible(seed, output, false)) continue;
                sim::PhaseChoice route;
                route.output = output;
                route.machines.push_back(seed);
                route.score = phaseScoreRoute(k, route);
                route.travel = phaseTravel(k, route);
                phaseMasks(route);
                choices.push_back(route);
                while (static_cast<int>(route.machines.size()) < batchLimit) {
                    int next = -1;
                    long double nextScore = -std::numeric_limits<long double>::infinity();
                    for (int j = 0; j < in.m; ++j) {
                        if (!phaseEligible(j, output, false) ||
                            std::find(route.machines.begin(), route.machines.end(), j)
                                != route.machines.end()) continue;
                        sim::PhaseChoice trial = route;
                        trial.machines.push_back(j);
                        long double value = phaseScoreRoute(k, trial);
                        if (value > nextScore) {
                            nextScore = value;
                            next = j;
                        }
                    }
                    if (next < 0) break;
                    route.machines.push_back(next);
                    route.score = phaseScoreRoute(k, route);
                    route.travel = phaseTravel(k, route);
                    phaseMasks(route);
                    choices.push_back(route);
                }
            }
        }
        return choices;
    };

    auto phaseFeatures = [&](int k, const sim::PhaseChoice& route) {
        std::array<long double, 9> phi{};
        const long double H = std::max(1.0L, in.medianTau);
        const long double C = static_cast<long double>(in.C);
        phi[0] = phaseScoreRoute(k, route);
        long double empty = route.output
            ? in.tau[agvs[k].position][in.beta[route.machines.front()]]
            : in.tau[agvs[k].position][in.o];
        long double loaded = std::max(0.0L, route.travel - empty);
        long double age = 0;
        long double wait = 0;
        long double exposure = 0;
        long double t = route.output ? 0 : in.tau[agvs[k].position][in.o];
        int last = route.output ? agvs[k].position : in.o;
        for (int j : route.machines) {
            t += in.tau[last][phaseLocation(j, route.output)];
            const auto& m = machines[j];
            long double eta = now + t;
            if (route.output) {
                if (m.status == sim::Status::Blocked) ++phi[1];
                age += std::max(0.0L, now - m.outputSince);
                if (m.status == sim::Status::Blocked) exposure += 2;
                if (m.status == sim::Status::Processing)
                    exposure += std::max(0.0L, result.processEnd[m.current] - now) / H;
            } else {
                if (m.status == sim::Status::Idle) ++phi[2];
                long double ready = m.status == sim::Status::Idle ? now
                    : m.status == sim::Status::Processing ? result.processEnd[m.current]
                    : now + 2 * H;
                wait += std::max(0.0L, ready - eta);
                if (m.status == sim::Status::Idle) exposure += (eta - now) / H;
                if (m.output >= 0) exposure += 2;
            }
            last = phaseLocation(j, route.output);
        }
        phi[3] = -empty / H;
        phi[4] = -loaded / H;
        phi[5] = static_cast<long double>(route.machines.size()) / C;
        phi[6] = route.output ? std::clamp(age / (H * C), -5.0L, 5.0L) : 0;
        phi[7] = -wait / H;
        phi[8] = -exposure;
        return phi;
    };

    auto phaseLearnedChoice = [&](int k) {
        sim::PhaseChoice best;
        sim::PhaseChoice oldBest;
        long double bestScore = -std::numeric_limits<long double>::infinity();
        long double oldBestScore = -std::numeric_limits<long double>::infinity();
        auto choices = phaseGeneratedChoices(k);
        for (const auto& route : choices) {
            auto phi = phaseFeatures(k, route);
            long double score = 0;
            for (int d = 0; d < 9; ++d)
                score += static_cast<long double>(candidate.dispatchWeights[d]) * phi[d];
            for (int d = 0; d < 9; ++d) {
                result.featureSum[d] += phi[d];
                result.featureSquareSum[d] += phi[d] * phi[d];
            }
            ++result.featureCount;
            if (route.score > oldBestScore) {
                oldBestScore = route.score;
                oldBest = route;
            }
            if (score > bestScore) {
                bestScore = score;
                best = route;
                best.score = score;
            }
        }
        if (!best.machines.empty()) {
            ++result.learnedChoiceCount;
            if (best.output != oldBest.output || best.machines != oldBest.machines)
                ++result.learnedChoiceChanges;
        }
        return best;
    };

    auto phaseCandidates = [&](int k, bool onlyOutput = false) {
        struct Seed {
            long double score = -std::numeric_limits<long double>::infinity();
            long double travel = 0;
            int machine = -1;
        };
        std::vector<sim::PhaseChoice> all;
        auto addUnique = [&](sim::PhaseChoice route) {
            phaseMasks(route);
            for (auto& existing : all) {
                if (existing.output == route.output && existing.machines == route.machines) {
                    if (route.score > existing.score) existing = route;
                    return;
                }
            }
            all.push_back(std::move(route));
        };
        for (bool output : {false, true}) {
            if (onlyOutput && !output) continue;
            std::vector<Seed> seeds;
            for (int j = 0; j < in.m; ++j) {
                if (!phaseEligible(j, output, false)) continue;
                sim::PhaseChoice route;
                route.output = output;
                route.machines.push_back(j);
                route.score = phaseScoreRoute(k, route);
                route.travel = phaseTravel(k, route);
                seeds.push_back({route.score, route.travel, j});
            }
            std::sort(seeds.begin(), seeds.end(), [&](const Seed& a, const Seed& b) {
                if (a.score != b.score) return a.score > b.score;
                int la = phaseTaskLot(a.machine, output);
                int lb = phaseTaskLot(b.machine, output);
                if (in.lots[la].id != in.lots[lb].id) return in.lots[la].id < in.lots[lb].id;
                return a.machine < b.machine;
            });
            if (seeds.size() > 6) seeds.resize(6);
            for (const Seed& seed : seeds) {
                sim::PhaseChoice seedRoute;
                seedRoute.output = output;
                seedRoute.machines.push_back(seed.machine);
                seedRoute.score = seed.score;
                seedRoute.travel = seed.travel;
                addUnique(seedRoute);
                if (batchLimit <= 1) continue;
                std::vector<Seed> add;
                for (int j = 0; j < in.m; ++j) {
                    if (j == seed.machine || !phaseEligible(j, output, false)) continue;
                    sim::PhaseChoice route;
                    route.output = output;
                    route.machines = {seed.machine, j};
                    route.score = phaseScoreRoute(k, route);
                    route.travel = phaseTravel(k, route);
                    add.push_back({route.score, route.travel, j});
                }
                std::sort(add.begin(), add.end(), [&](const Seed& a, const Seed& b) {
                    if (a.score != b.score) return a.score > b.score;
                    int la = phaseTaskLot(a.machine, output);
                    int lb = phaseTaskLot(b.machine, output);
                    if (in.lots[la].id != in.lots[lb].id) return in.lots[la].id < in.lots[lb].id;
                    return a.machine < b.machine;
                });
                if (add.size() > 5) add.resize(5);
                for (const Seed& first : add) {
                    sim::PhaseChoice two;
                    two.output = output;
                    two.machines = {seed.machine, first.machine};
                    two.score = phaseScoreRoute(k, two);
                    two.travel = phaseTravel(k, two);
                    addUnique(two);
                    if (batchLimit <= 2) continue;
                    for (const Seed& second : add) {
                        if (second.machine == first.machine) continue;
                        sim::PhaseChoice three = two;
                        three.machines.push_back(second.machine);
                        three.score = phaseScoreRoute(k, three);
                        three.travel = phaseTravel(k, three);
                        addUnique(three);
                    }
                }
            }
        }
        result.candidateRoutes += static_cast<long long>(all.size());
        std::sort(all.begin(), all.end(), phaseRouteLess);
        std::vector<sim::PhaseChoice> kept;
        int keptOut = 0;
        int keptIn = 0;
        for (auto& route : all) {
            if (route.output && keptOut >= 2) continue;
            if (!route.output && keptIn >= 1) continue;
            route.travel = phaseTravel(k, route);
            phaseMasks(route);
            kept.push_back(route);
            if (route.output) ++keptOut;
            else ++keptIn;
        }
        sim::PhaseChoice fallback = phaseBestChoice(k);
        if (!fallback.machines.empty() && !(onlyOutput && !fallback.output)) {
            fallback.travel = phaseTravel(k, fallback);
            phaseMasks(fallback);
            bool duplicate = false;
            for (const auto& route : kept) {
                if (route.output == fallback.output && route.machines == fallback.machines) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate) kept.push_back(fallback);
        }
        std::sort(kept.begin(), kept.end(), phaseRouteLess);
        return kept;
    };

    auto chooseJointAssignment = [&](const std::vector<int>& freeAgvs,
                                     const std::vector<std::vector<sim::PhaseChoice>>& options) {
        int count = static_cast<int>(freeAgvs.size());
        std::vector<long double> upper(count + 1, 0);
        for (int d = count - 1; d >= 0; --d) {
            long double bestPositive = 0;
            for (const auto& route : options[d])
                bestPositive = std::max(bestPositive, route.score);
            upper[d] = upper[d + 1] + bestPositive;
        }
        std::vector<int> chosen(count, -1);
        std::vector<int> bestChosen(count, -1);
        long double bestScore = 0;
        long double bestTravel = 0;
        int bestIdle = count;
        std::vector<int> bestSignature;
        long double accScore = 0;
        long double accTravel = 0;
        int idleCount = 0;
        auto leaf = [&]() {
            std::vector<int> signature;
            for (int d = 0; d < count; ++d) {
                int idx = chosen[d];
                if (idx < 0) {
                    signature.push_back(-1);
                } else {
                    const auto& route = options[d][idx];
                    signature.push_back(route.output ? 0 : 1);
                    for (int j : route.machines) signature.push_back(100 + j);
                }
            }
            bool better = accScore > bestScore + 1e-12L;
            if (!better && std::fabs(accScore - bestScore) <= 1e-12L) {
                if (accTravel < bestTravel - 1e-12L) better = true;
                else if (std::fabs(accTravel - bestTravel) <= 1e-12L) {
                    if (idleCount < bestIdle) better = true;
                    else if (idleCount == bestIdle && signature < bestSignature) better = true;
                }
            }
            if (better) {
                bestScore = accScore;
                bestTravel = accTravel;
                bestIdle = idleCount;
                bestChosen = chosen;
                bestSignature = std::move(signature);
            }
        };
        auto dfs = [&](auto&& self, int depth, std::uint32_t usedIn,
                       std::uint32_t usedOut) -> void {
            ++result.dfsNodes;
            if (accScore + upper[depth] < bestScore - 1e-12L) return;
            if (depth == count) {
                leaf();
                return;
            }
            for (int idx = 0; idx < static_cast<int>(options[depth].size()); ++idx) {
                const auto& route = options[depth][idx];
                if ((route.inputMask & usedIn) || (route.outputMask & usedOut)) continue;
                chosen[depth] = idx;
                accScore += route.score;
                accTravel += route.travel;
                self(self, depth + 1, usedIn | route.inputMask, usedOut | route.outputMask);
                accScore -= route.score;
                accTravel -= route.travel;
                chosen[depth] = -1;
            }
            ++idleCount;
            self(self, depth + 1, usedIn, usedOut);
            --idleCount;
        };
        if (count == 1) {
            if (!options[0].empty()) bestChosen[0] = 0;
        } else {
            std::fill(bestSignature.begin(), bestSignature.end(), 0);
            dfs(dfs, 0, 0, 0);
        }
        bool any = false;
        for (int idx : bestChosen) if (idx >= 0) any = true;
        if (!any) {
            int bestDepth = -1;
            int bestIdx = -1;
            long double bestVal = -std::numeric_limits<long double>::infinity();
            for (int d = 0; d < count; ++d) {
                for (int idx = 0; idx < static_cast<int>(options[d].size()); ++idx) {
                    if (options[d][idx].score > bestVal) {
                        bestVal = options[d][idx].score;
                        bestDepth = d;
                        bestIdx = idx;
                    }
                }
            }
            if (bestDepth >= 0) {
                bestChosen[bestDepth] = bestIdx;
                any = true;
            }
        }
        if (any) ++result.jointChoicesUsed;
        return bestChosen;
    };

    auto applyPhaseChoice = [&](int k, const sim::PhaseChoice& best) {
        auto& agv = agvs[k];
        agv.route.clear();
        agv.incoming.clear();
        agv.load.clear();
        long double departure = now;
        markAgvBusy(k);
        if (best.output) {
            ++result.phaseOutputTrips;
            result.phaseOutputLots += static_cast<int>(best.machines.size());
            int seed = best.machines.front();
            bool future = machines[seed].output < 0;
            if (future) {
                long double ready = result.processEnd[machines[seed].current];
                departure = std::max(now, ready - in.tau[agv.position][in.beta[seed]]);
            }
            for (int j : best.machines) {
                int lot = machines[j].output >= 0 ? machines[j].output : machines[j].current;
                machines[j].outputReserved = true;
                agv.route.push_back({in.beta[j], sim::Action::PickBeta, lot});
            }
            agv.route.push_back({in.f, sim::Action::DropFinal, -1});
        } else {
            ++result.phaseInputTrips;
            result.phaseInputLots += static_cast<int>(best.machines.size());
            agv.route.push_back({in.o, sim::Action::PickIn, -1});
            for (int j : best.machines) {
                auto& m = machines[j];
                int lot = candidate.sequence[j][m.nextFeed++];
                m.inputReserved = true;
                if (starvedSince[j] >= 0) {
                    result.machineStarvedByMachine[j] += now - starvedSince[j];
                    starvedSince[j] = -1;
                }
                agv.incoming.push_back(lot);
                agv.route.push_back({in.alpha[j], sim::Action::DropAlpha, lot});
            }
        }
        agv.step = 0;
        agv.active = true;
        int first = agv.route.front().destination;
        if (recordLog && agv.position != first)
            result.log.push_back({LogType::Move, k, -1, agv.position, first, departure});
        result.emptyTravel += in.tau[agv.position][first];
        result.emptyTravelByAgv[k] += in.tau[agv.position][first];
        events.push({departure + in.tau[agv.position][first] + sim::RELEASE_GUARD, 1, k});
    };

    auto settle = [&]() {
        int freeCount = 0;
        for (int k = 0; k < in.K; ++k) if (!agvs[k].active) ++freeCount;
        if (freeCount > 0) {
            ++result.dispatchEpochCount;
            ++dispatchEpoch;
            if (freeCount >= 2) ++result.multiFreeEpochCount;
            result.maxSimultaneouslyFree = std::max(result.maxSimultaneouslyFree, freeCount);
        }
        if (candidate.phaseAware && freeCount > 0 && !firstDispatchDone &&
            (candidate.collectFirst || candidate.forcedFirst)) {
            int first = -1;
            for (int q = 0; q < in.K; ++q) {
                if (!agvs[q].active) { first = q; break; }
            }
            if (candidate.collectFirst && first >= 0) {
                result.firstRoutes.clear();
                for (const auto& route : phaseGeneratedChoices(first)) {
                    result.firstRoutes.push_back({route.output, route.machines, route.score});
                }
                sim::PhaseChoice chosen = phaseBestChoice(first);
                result.firstChosen = {chosen.output, chosen.machines, chosen.score};
                result.firstAgv = first;
                result.hasFirst = true;
                firstDispatchDone = true;
            }
            if (candidate.forcedFirst && candidate.forcedAgv >= 0 &&
                candidate.forcedAgv < in.K && !agvs[candidate.forcedAgv].active) {
                sim::PhaseChoice forced;
                forced.output = candidate.forcedOutput;
                forced.machines = candidate.forcedMachines;
                bool valid = !forced.machines.empty() &&
                    static_cast<int>(forced.machines.size()) <= batchLimit;
                for (std::size_t i = 0; valid && i < forced.machines.size(); ++i) {
                    int j = forced.machines[i];
                    if (j < 0 || j >= in.m || !phaseEligible(j, forced.output, false)) valid = false;
                    for (std::size_t h = 0; h < i; ++h)
                        if (forced.machines[h] == j) valid = false;
                }
                if (valid) applyPhaseChoice(candidate.forcedAgv, forced);
                firstDispatchDone = true;
            }
        }
        for (int j = 0; j < in.m; ++j) {
            if (machines[j].status == sim::Status::Idle && machines[j].input >= 0) {
                startMachine(j);
            }
        }
        for (int k = 0; k < in.K; ++k) {
            if (agvs[k].active) continue;
            if (candidate.phaseAware) {
                if (candidate.learnedPolicy) {
                    if (candidate.phaseGlobal) {
                        std::vector<char> free(in.K, 0);
                        for (int q = 0; q < in.K; ++q) free[q] = agvs[q].active ? 0 : 1;
                        while (true) {
                            int bestK = -1;
                            sim::PhaseChoice bestChoice;
                            for (int q = 0; q < in.K; ++q) {
                                if (!free[q]) continue;
                                sim::PhaseChoice choice = phaseLearnedChoice(q);
                                if (choice.machines.empty()) continue;
                                if (bestK < 0 || choice.score > bestChoice.score + sim::EPS ||
                                    (std::fabs(choice.score - bestChoice.score) <= sim::EPS && q < bestK)) {
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
                            sim::PhaseChoice choice = phaseLearnedChoice(q);
                            if (!choice.machines.empty()) applyPhaseChoice(q, choice);
                        }
                    }
                    continue;
                }
                if (candidate.phaseHardUnblock) {
                    bool anyBlocked = false;
                    for (int j = 0; j < in.m; ++j) {
                        if (machines[j].status == sim::Status::Blocked &&
                            machines[j].output >= 0 && !machines[j].outputReserved) {
                            anyBlocked = true;
                            break;
                        }
                    }
                    if (anyBlocked) {
                        for (int q = 0; q < in.K; ++q) {
                            if (agvs[q].active) continue;
                            std::vector<sim::PhaseChoice> options = phaseCandidates(q, true);
                            int pick = -1;
                            for (int idx = 0; idx < static_cast<int>(options.size()); ++idx) {
                                for (int j : options[idx].machines) {
                                    if (machines[j].status == sim::Status::Blocked) {
                                        pick = idx;
                                        break;
                                    }
                                }
                                if (pick >= 0) break;
                            }
                            if (pick >= 0) {
                                applyPhaseChoice(q, options[pick]);
                            } else {
                                sim::PhaseChoice signal = phaseBestChoice(q);
                                if (signal.output) {
                                    std::vector<sim::PhaseChoice> out = phaseCandidates(q, true);
                                    if (!out.empty()) applyPhaseChoice(q, out.front());
                                    else if (hasInput()) createInputRoute(q);
                                } else if (hasInput()) {
                                    createInputRoute(q);
                                }
                            }
                        }
                        continue;
                    }
                }
                if (candidate.phaseOutboundOnly) {
                    for (int q = 0; q < in.K; ++q) {
                        if (agvs[q].active) continue;
                        sim::PhaseChoice signal = phaseBestChoice(q);
                        if (signal.output) {
                            std::vector<sim::PhaseChoice> options = phaseCandidates(q, true);
                            if (!options.empty()) applyPhaseChoice(q, options.front());
                            else if (hasInput()) createInputRoute(q);
                        } else if (hasInput()) {
                            createInputRoute(q);
                        }
                    }
                    continue;
                }
                if (candidate.phaseJoint) {
                    std::vector<int> freeAgvs;
                    std::vector<std::vector<sim::PhaseChoice>> options;
                    for (int q = 0; q < in.K; ++q) {
                        if (agvs[q].active) continue;
                        freeAgvs.push_back(q);
                        options.push_back(phaseCandidates(q));
                    }
                    if (!freeAgvs.empty()) {
                        std::vector<int> chosen = chooseJointAssignment(freeAgvs, options);
                        for (int d = 0; d < static_cast<int>(freeAgvs.size()); ++d) {
                            int idx = chosen[d];
                            if (idx >= 0) applyPhaseChoice(freeAgvs[d], options[d][idx]);
                        }
                    }
                    continue;
                }
                if (candidate.phaseEnumerated) {
                    for (int q = 0; q < in.K; ++q) {
                        if (agvs[q].active) continue;
                        std::vector<sim::PhaseChoice> options = phaseCandidates(q);
                        if (!options.empty()) applyPhaseChoice(q, options.front());
                    }
                    continue;
                }
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
            if (starvedSince[j] >= 0) {
                result.machineStarvedByMachine[j] += now - starvedSince[j];
                starvedSince[j] = -1;
            }
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
                starvedSince[j] = (m.input < 0 && m.nextFeed < candidate.sequence[j].size())
                    ? now : -1;
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
            long double leg = in.tau[agv.position][destination];
            if (!agv.load.empty()) {
                result.loadedTravel += leg;
                result.loadedTravelByAgv[k] += leg;
            } else {
                result.emptyTravel += leg;
                result.emptyTravelByAgv[k] += leg;
            }
            events.push({now + leg + sim::RELEASE_GUARD, 1, k});
        } else {
            agv.active = false;
            agv.route.clear();
            agv.step = 0;
            markAgvIdle(k);
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

    for (int k = 0; k < in.K; ++k) {
        if (vehicleIdleSince[k] >= 0) {
            result.idleVehicleTimeByAgv[k] += now - vehicleIdleSince[k];
            vehicleIdleSince[k] = -1;
        }
    }
    for (int j = 0; j < in.m; ++j) {
        if (starvedSince[j] >= 0) {
            result.machineStarvedByMachine[j] += now - starvedSince[j];
            starvedSince[j] = -1;
        }
    }
    if (delivered != in.n) return result;
    long double total = 0;
    long double makespan = 0;
    for (long double value : result.completion) {
        total += value;
        makespan = std::max(makespan, value);
    }
    result.makespan = makespan;
    result.deliveries = delivered;
    result.mean = total / in.n;
    result.feasible = true;
    return result;
}

}
