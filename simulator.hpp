#pragma once

#include "instance.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <string>
#include <vector>

namespace io1 {

struct Candidate {
    std::vector<std::vector<int>> sequence;
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
    long double mean = std::numeric_limits<long double>::infinity();
    long double blockedTime = 0;
    int blockedCount = 0;
    int deliveries = 0;
};

namespace sim {

constexpr long double EPS = 1e-12L;
constexpr long double RELEASE_GUARD = 1e-8L;

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

}

inline Result simulate(const Instance& in, const Candidate& candidate, int batchLimit) {
    Result result;
    result.completion.assign(in.n, std::numeric_limits<long double>::infinity());
    result.processStart.assign(in.n, std::numeric_limits<long double>::infinity());
    result.processEnd.assign(in.n, std::numeric_limits<long double>::infinity());

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

    auto startMachine = [&](int j) {
        auto& m = machines[j];
        int lot = m.input;
        m.input = -1;
        m.current = lot;
        m.status = sim::Status::Processing;
        long double start = now + sim::RELEASE_GUARD;
        long double end = start + in.duration(lot, j);
        result.processStart[lot] = start;
        result.processEnd[lot] = end;
        result.log.push_back({LogType::Process, j, lot, -1, -1, start});
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
        if (agv.position != in.o) {
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
        if (agv.position != first) {
            result.log.push_back({LogType::Move, k, -1, agv.position, first, now});
        }
        events.push({now + in.tau[agv.position][first] + sim::RELEASE_GUARD, 1, k});
        return true;
    };

    auto settle = [&]() {
        for (int j = 0; j < in.m; ++j) {
            if (machines[j].status == sim::Status::Idle && machines[j].input >= 0) {
                startMachine(j);
            }
        }
        for (int k = 0; k < in.K; ++k) {
            if (agvs[k].active) continue;
            if (hasOutput()) createOutputRoute(k);
            else if (hasInput()) createInputRoute(k);
        }
    };

    auto arrive = [&](int k) {
        auto& agv = agvs[k];
        sim::Step step = agv.route[agv.step];
        if (step.action == sim::Action::PickIn) {
            for (int lot : agv.incoming) {
                result.log.push_back({LogType::Pickup, k, lot, in.o, -1, now});
            }
            agv.load = agv.incoming;
            agv.incoming.clear();
        } else if (step.action == sim::Action::DropAlpha) {
            int j = alphaMachine[step.destination];
            machines[j].input = step.lot;
            machines[j].inputReserved = false;
            agv.load.erase(std::find(agv.load.begin(), agv.load.end(), step.lot));
            result.log.push_back({LogType::Deliver, k, step.lot, step.destination, -1, now});
        } else if (step.action == sim::Action::PickBeta) {
            int j = betaMachine[step.destination];
            auto& m = machines[j];
            m.output = -1;
            m.outputReserved = false;
            if (m.status == sim::Status::Blocked) {
                m.output = m.current;
                m.current = -1;
                m.status = sim::Status::Idle;
                m.outputSince = now;
                result.blockedTime += now - m.blockStart;
            }
            agv.load.push_back(step.lot);
            result.log.push_back({LogType::Pickup, k, step.lot, step.destination, -1, now});
        } else {
            for (int lot : agv.load) {
                result.completion[lot] = now;
                result.log.push_back({LogType::Deliver, k, lot, in.f, -1, now});
            }
            delivered += static_cast<int>(agv.load.size());
            agv.load.clear();
        }
        agv.position = step.destination;
        ++agv.step;
        if (agv.step < agv.route.size()) {
            int destination = agv.route[agv.step].destination;
            if (agv.position != destination) {
                result.log.push_back({LogType::Move, k, -1, agv.position, destination, now});
            }
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
