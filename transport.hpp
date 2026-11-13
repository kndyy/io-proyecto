#pragma once

#include "search_types.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <numeric>
#include <ostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace io1 {

struct TransportPlan {
    std::vector<std::vector<int>> machines;
    std::vector<std::vector<int>> vehicles;
};

inline Candidate buildSlotAssignment(const Instance& in, long double travelWeight,
                                      long double initialWeight = 1) {
    if (!std::isfinite(travelWeight) || travelWeight < 0 ||
        !std::isfinite(initialWeight) || initialWeight < 0)
        throw std::runtime_error("pesos de asignacion incorrectos");
    int rows = in.n, columns = in.m * in.n;
    std::vector<long double> u(rows + 1), v(columns + 1), minimum(columns + 1);
    std::vector<int> owner(columns + 1), way(columns + 1), used(columns + 1);
    auto cost = [&](int lot, int column) {
        int j = column / in.n, slot = column % in.n + 1;
        if (!in.compatible(lot, j)) return 1e15L;
        long double travel = in.tau[in.o][in.alpha[j]] + in.tau[in.beta[j]][in.f];
        return slot * (in.duration(lot, j) + travelWeight * travel / in.C) + initialWeight * travel;
    };
    for (int row = 1; row <= rows; ++row) {
        owner[0] = row;
        int column = 0;
        std::fill(minimum.begin(), minimum.end(), std::numeric_limits<long double>::infinity());
        std::fill(used.begin(), used.end(), 0);
        do {
            used[column] = 1;
            int currentRow = owner[column], next = 0;
            long double delta = std::numeric_limits<long double>::infinity();
            for (int c = 1; c <= columns; ++c) if (!used[c]) {
                long double value = cost(currentRow - 1, c - 1) - u[currentRow] - v[c];
                if (value < minimum[c]) { minimum[c] = value; way[c] = column; }
                if (minimum[c] < delta) { delta = minimum[c]; next = c; }
            }
            for (int c = 0; c <= columns; ++c) {
                if (used[c]) { u[owner[c]] += delta; v[c] -= delta; }
                else minimum[c] -= delta;
            }
            column = next;
        } while (owner[column] != 0);
        do {
            int previous = way[column];
            owner[column] = owner[previous];
            column = previous;
        } while (column != 0);
    }
    Candidate candidate;
    candidate.sequence.resize(in.m);
    std::vector<std::vector<std::pair<int, int>>> slots(in.m);
    for (int c = 1; c <= columns; ++c) if (owner[c] != 0) {
        int j = (c - 1) / in.n, slot = (c - 1) % in.n;
        int lot = owner[c] - 1;
        if (!in.compatible(lot, j)) throw std::runtime_error("asignacion incompatible");
        slots[j].push_back({slot, lot});
    }
    for (int j = 0; j < in.m; ++j) {
        std::sort(slots[j].rbegin(), slots[j].rend());
        for (auto [slot, lot] : slots[j]) {
            (void)slot;
            candidate.sequence[j].push_back(lot);
        }
    }
    return candidate;
}

inline Outcome slotTransportSeed(const Instance& in, const Options& options, long double weight,
                                  int forcedBias = 99, int forcedBatch = 0) {
    Candidate candidate = buildSlotAssignment(in, weight);
    candidate.phaseAware = true;
    candidate.phaseLookahead = options.phaseLookahead == 1;
    Outcome best;
    long long evaluations = 0;
    for (int batch = forcedBatch > 0 ? forcedBatch : std::max(1, in.C - 1); batch <= in.C; ++batch) {
        if (forcedBatch > 0 && batch != forcedBatch) continue;
        for (int bias : {-5, -2, 0, 2, 5}) for (bool global : {false, true}) {
            if (forcedBias != 99 && bias != forcedBias) continue;
            if (options.phaseGlobal >= 0 && global != (options.phaseGlobal == 1)) continue;
            candidate.phaseBias = bias;
            candidate.phaseGlobal = global;
            Result result = simulate(in, candidate, batch, true);
            ++evaluations;
            if (result.feasible && result.mean < best.result.mean) {
                best.candidate = candidate;
                best.result = std::move(result);
                best.batchLimit = batch;
            }
        }
    }
    if (!best.result.feasible) throw std::runtime_error("inicio por slots no factible");
    best.evals = evaluations;
    return best;
}

inline TransportPlan buildMilkPlan(const Instance& in, const std::vector<std::vector<int>>& sequences,
                                    int lag, std::uint64_t seed, bool global = false, int profile = 0) {
    if (lag < 0 || lag > 2 || profile < 0 || profile > 2) throw std::runtime_error("configuracion de rutas incorrecta");
    TransportPlan plan{sequences, std::vector<std::vector<int>>(in.K)};
    std::vector<int> next(in.m), nextOutput(in.m), position(in.K, in.o);
    std::vector<long double> available(in.K), start(in.n), end(in.n), pickup(in.n);
    std::mt19937_64 rng(seed);
    long double H = std::max(1.0L, in.medianTau);
    long double inputWeight = 1.0L + (rng() % 101) / 100.0L;
    long double outputWeight = 0.8L + (rng() % 101) / 100.0L;
    long double waitWeight = 0.5L + (rng() % 101) / 100.0L;
    long double allowedWait = (rng() % 101) / 100.0L * H;
    if (profile > 0) {
        inputWeight = 0.25L + (rng() % 376) / 100.0L;
        outputWeight = 0.25L + (rng() % 376) / 100.0L;
        waitWeight = (rng() % 201) / 100.0L;
        allowedWait = (rng() % 201) / 100.0L * H;
    }
    auto active = [&](int j) { return next[j] < static_cast<int>(sequences[j].size()) || nextOutput[j] < static_cast<int>(sequences[j].size()); };
    auto feedLot = [&](int j) { return next[j] < static_cast<int>(sequences[j].size()) ? sequences[j][next[j]] : -1; };
    auto outLot = [&](int j) {
        int fed = std::min(next[j], static_cast<int>(sequences[j].size()));
        if (nextOutput[j] >= fed || (feedLot(j) >= 0 && fed < lag)) return -1;
        return sequences[j][nextOutput[j]];
    };
    auto evaluate = [&](int k, const std::vector<int>& route, bool commit) {
        bool hasInput = false;
        std::vector<int> outputs;
        for (int j : route) hasInput = hasInput || feedLot(j) >= 0;
        long double time = available[k], waited = 0, benefit = 0;
        int at = position[k];
        if (hasInput) {
            time += in.tau[at][in.o];
            at = in.o;
            if (commit) for (int j : route) if (feedLot(j) >= 0) plan.vehicles[k].push_back(5 * feedLot(j));
        }
        for (int j : route) {
            int feed = feedLot(j), output = outLot(j), index = next[j];
            long double alpha = 0, beta = 0;
            if (feed >= 0) {
                time += in.tau[at][in.alpha[j]];
                at = in.alpha[j];
                if (index > 0) {
                    long double ready = start[sequences[j][index - 1]];
                    waited += std::max(0.0L, ready - time);
                    time = std::max(time, ready);
                }
                alpha = time;
                benefit += inputWeight + H / (H + in.duration(feed, j));
                if (profile >= 2) {
                    long double remaining = static_cast<long double>(sequences[j].size() - index) / std::max(1.0L, static_cast<long double>(in.n) / in.m);
                    long double ready = index > 0 ? end[sequences[j][index - 1]] : 0;
                    benefit += inputWeight * remaining * (1 + std::min(2.0L, std::max(0.0L, alpha - ready) / H));
                }
                if (commit) plan.vehicles[k].push_back(5 * feed + 1);
            }
            if (lag == 0 && output >= 0 && feed >= 0 && nextOutput[j] > index - 2 &&
                end[output] > time + in.tau[at][in.beta[j]] + allowedWait) output = -1;
            if (output >= 0) {
                time += in.tau[at][in.beta[j]];
                at = in.beta[j];
                long double ready = end[output];
                int outputIndex = static_cast<int>(std::find(sequences[j].begin(), sequences[j].end(), output) - sequences[j].begin());
                if (outputIndex > 0) ready = std::max(ready, pickup[sequences[j][outputIndex - 1]]);
                waited += std::max(0.0L, ready - time);
                time = std::max(time, ready);
                beta = time;
                benefit += outputWeight;
                if (profile >= 2 && feed >= 0 && nextOutput[j] <= index - 2)
                    benefit += outputWeight * (sequences[j].size() - index) / std::max(1.0L, static_cast<long double>(in.n) / in.m);
                outputs.push_back(output);
                if (commit) { pickup[output] = beta; plan.vehicles[k].push_back(5 * output + 3); ++nextOutput[j]; }
            }
            if (feed >= 0 && commit) {
                long double ready = index > 0 ? end[sequences[j][index - 1]] : 0;
                if (index > 1) ready = std::max(ready, pickup[sequences[j][index - 2]]);
                start[feed] = std::max(alpha, ready);
                end[feed] = start[feed] + in.duration(feed, j);
            }
        }
        if (!outputs.empty()) {
            time += in.tau[at][in.f];
            at = in.f;
            if (commit) for (int output : outputs) plan.vehicles[k].push_back(5 * output + 4);
        }
        long double origin = global ? *std::min_element(available.begin(), available.end()) : available[k];
        if (commit) {
            available[k] = time;
            position[k] = at;
            for (int j : route) ++next[j];
        }
        return benefit / (H + time - origin + waitWeight * waited);
    };
    while (true) {
        bool work = false;
        for (int j = 0; j < in.m; ++j) work = work || active(j);
        if (!work) break;
        int k = static_cast<int>(std::min_element(available.begin(), available.end()) - available.begin());
        int bestVehicle = k;
        std::vector<int> best;
        long double bestScore = -1;
        if (global) {
            std::vector<int> route;
            for (int vehicle = 0; vehicle < in.K; ++vehicle) {
                auto visit = [&](auto&& self) -> void {
                    if (!route.empty()) {
                        long double score = evaluate(vehicle, route, false) * (0.98L + (rng() % 401) / 10000.0L);
                        if (score > bestScore) { best = route; bestScore = score; bestVehicle = vehicle; }
                    }
                    if (static_cast<int>(route.size()) >= in.C) return;
                    for (int j = 0; j < in.m; ++j) {
                        if (!active(j) || std::find(route.begin(), route.end(), j) != route.end()) continue;
                        route.push_back(j);
                        self(self);
                        route.pop_back();
                    }
                };
                visit(visit);
            }
        } else {
            for (int seedMachine = 0; seedMachine < in.m; ++seedMachine) {
                if (!active(seedMachine)) continue;
                std::vector<int> route{seedMachine};
                for (int size = 1; size <= in.C; ++size) {
                    long double score = evaluate(k, route, false) * (0.98L + (rng() % 401) / 10000.0L);
                    if (score > bestScore) { best = route; bestScore = score; }
                    if (size == in.C) break;
                    int chosen = -1;
                    long double nextScore = -1;
                    for (int j = 0; j < in.m; ++j) {
                        if (!active(j) || std::find(route.begin(), route.end(), j) != route.end()) continue;
                        route.push_back(j);
                        long double value = evaluate(k, route, false);
                        route.pop_back();
                        if (value > nextScore) { nextScore = value; chosen = j; }
                    }
                    if (chosen < 0) break;
                    route.push_back(chosen);
                }
            }
        }
        if (best.empty()) throw std::runtime_error("construccion de rutas incompleta");
        evaluate(bestVehicle, best, true);
    }
    return plan;
}

inline TransportPlan transportPlan(const Instance& in, const Outcome& outcome) {
    TransportPlan plan;
    plan.machines = outcome.candidate.sequence;
    plan.vehicles.resize(in.K);
    for (const auto& event : outcome.result.log) {
        if (event.type == LogType::Process || event.type == LogType::Move) continue;
        int stage = event.type == LogType::Pickup
            ? (event.from == in.o ? 0 : 3) : (event.from == in.f ? 4 : 1);
        plan.vehicles[event.resource].push_back(5 * event.lot + stage);
    }
    return plan;
}

inline TransportPlan loadTransportPlan(const Instance& in, const std::string& path) {
    std::ifstream file(path);
    if (!file) throw std::runtime_error("no se pudo abrir " + path);
    struct Entry { long double time; int order; int node; };
    std::vector<std::vector<Entry>> machines(in.m), vehicles(in.K);
    std::string line;
    std::getline(file, line);
    if (line != "recurso;orden;accion;lote;ubicacion;destino;inicio")
        throw std::runtime_error("cabecera TXT incorrecta");
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::stringstream stream(line);
        std::vector<std::string> fields;
        std::string field;
        while (std::getline(stream, field, ';')) fields.push_back(field);
        if (fields.size() != 7) throw std::runtime_error("fila TXT incorrecta");
        if (fields[2] == "MOVE") continue;
        int lot = std::stoi(fields[3]) - 1;
        if (lot < 0 || lot >= in.n) throw std::runtime_error("lote TXT incorrecto");
        Entry e{std::stold(fields[6]), std::stoi(fields[1]), lot};
        if (!std::isfinite(e.time) || e.time < 0) throw std::runtime_error("tiempo TXT incorrecto");
        if (fields[2] == "PROCESS") {
            int j = std::stoi(fields[0].substr(1)) - 1;
            machines.at(j).push_back(e);
        } else {
            int k = std::stoi(fields[0].substr(3)) - 1;
            int stage;
            if (fields[2] == "PICKUP") stage = fields[4] == "o" ? 0 : 3;
            else if (fields[2] == "DELIVER") stage = fields[4] == "f" ? 4 : 1;
            else throw std::runtime_error("accion TXT incorrecta");
            e.node = 5 * lot + stage;
            vehicles.at(k).push_back(e);
        }
    }
    TransportPlan plan;
    auto unpack = [](auto& entries, auto& sequences) {
        sequences.resize(entries.size());
        for (std::size_t i = 0; i < entries.size(); ++i) {
            std::sort(entries[i].begin(), entries[i].end(), [](const Entry& a, const Entry& b) {
                return a.time != b.time ? a.time < b.time : a.order < b.order;
            });
            for (const auto& e : entries[i]) sequences[i].push_back(e.node);
        }
    };
    unpack(machines, plan.machines);
    unpack(vehicles, plan.vehicles);
    return plan;
}

class TransportEvaluator {
    struct Arc { int to; long double lag; };
    const Instance& in;
    std::vector<std::array<Arc, 4>> arcs;
    std::vector<int> degree, indegree, queue, seen, carried;

    void edge(int a, int b, long double lag) {
        arcs[a][degree[a]++] = {b, lag + sim::RELEASE_GUARD};
        ++indegree[b];
    }

public:
    std::vector<int> machineOf;
    std::vector<long double> times;

    explicit TransportEvaluator(const Instance& instance)
        : in(instance), arcs(5 * in.n), degree(5 * in.n), indegree(5 * in.n),
          queue(5 * in.n), seen(5 * in.n), carried(2 * in.n), machineOf(in.n), times(5 * in.n) {}

    int location(int node) const {
        int stage = node % 5;
        if (stage == 0) return in.o;
        if (stage == 4) return in.f;
        return stage == 1 ? in.alpha[machineOf[node / 5]] : in.beta[machineOf[node / 5]];
    }

    long double evaluate(const TransportPlan& plan, bool partial = false,
                          const std::vector<long double>* durations = nullptr) {
        const long double invalid = std::numeric_limits<long double>::infinity();
        if (static_cast<int>(plan.machines.size()) != in.m ||
            static_cast<int>(plan.vehicles.size()) != in.K) return invalid;
        if (durations && static_cast<int>(durations->size()) != in.n) return invalid;
        auto duration = [&](int lot, int j) { return durations ? (*durations)[lot] : in.duration(lot, j); };
        std::fill(machineOf.begin(), machineOf.end(), -1);
        std::fill(degree.begin(), degree.end(), 0);
        std::fill(indegree.begin(), indegree.end(), 0);
        std::fill(times.begin(), times.end(), 0);
        std::fill(seen.begin(), seen.end(), 0);
        for (int j = 0; j < in.m; ++j) {
            const auto& sequence = plan.machines[j];
            for (std::size_t p = 0; p < sequence.size(); ++p) {
                int lot = sequence[p];
                if (lot < 0 || lot >= in.n || machineOf[lot] >= 0 || !in.compatible(lot, j)) return invalid;
                machineOf[lot] = j;
                edge(5 * lot + 1, 5 * lot + 2, 0);
                if (!std::isfinite(duration(lot, j)) || duration(lot, j) <= 0) return invalid;
                edge(5 * lot + 2, 5 * lot + 3, duration(lot, j));
                if (p > 0) {
                    int prev = sequence[p - 1];
                    edge(5 * prev + 2, 5 * lot + 2, duration(prev, j));
                    edge(5 * prev + 2, 5 * lot + 1, 0);
                    edge(5 * prev + 3, 5 * lot + 3, 0);
                }
                if (p > 1) edge(5 * sequence[p - 2] + 3, 5 * lot + 2, 0);
            }
        }
        for (int j : machineOf) if (j < 0) return invalid;
        for (int k = 0; k < in.K; ++k) {
            std::fill(carried.begin(), carried.end(), 0);
            int load = 0, prev = -1, position = in.o;
            for (int node : plan.vehicles[k]) {
                if (node < 0 || node >= 5 * in.n || node % 5 == 2 || seen[node]++) return invalid;
                int stage = node % 5;
                int request = 2 * (node / 5) + (stage >= 3 ? 1 : 0);
                if (stage == 0 || stage == 3) {
                    if (carried[request] || ++load > in.C) return invalid;
                    carried[request] = 1;
                } else {
                    if (!carried[request]) return invalid;
                    carried[request] = 0;
                    --load;
                }
                int loc = location(node);
                if (prev >= 0) edge(prev, node, in.tau[position][loc]);
                else times[node] = in.tau[position][loc] + sim::RELEASE_GUARD;
                prev = node;
                position = loc;
            }
            if (load != 0) return invalid;
        }
        for (int lot = 0; lot < in.n; ++lot) {
            for (int stage : {0, 3}) {
                int node = 5 * lot + stage;
                if (seen[node] && seen[node + 1]) continue;
                if (!partial || seen[node] || seen[node + 1]) return invalid;
                edge(node, node + 1, 0);
            }
        }
        int first = 0, last = 0;
        for (int i = 0; i < 5 * in.n; ++i) if (indegree[i] == 0) queue[last++] = i;
        while (first < last) {
            int u = queue[first++];
            for (int p = 0; p < degree[u]; ++p) {
                const auto& a = arcs[u][p];
                times[a.to] = std::max(times[a.to], times[u] + a.lag);
                if (--indegree[a.to] == 0) queue[last++] = a.to;
            }
        }
        if (last != 5 * in.n) return invalid;
        long double sum = 0;
        for (int lot = 0; lot < in.n; ++lot) sum += times[5 * lot + 4];
        return sum / in.n;
    }

    std::vector<long double> durationWeights() const {
        std::vector<long double> flow(5 * in.n), weights(in.n);
        std::vector<int> tight(5 * in.n);
        for (int u = 0; u < 5 * in.n; ++u) for (int p = 0; p < degree[u]; ++p) {
            const auto& a = arcs[u][p];
            if (std::fabs(times[a.to] - times[u] - a.lag) < 1e-7L) ++tight[a.to];
        }
        for (int lot = 0; lot < in.n; ++lot) flow[5 * lot + 4] = 1;
        for (int i = 5 * in.n - 1; i >= 0; --i) {
            int u = queue[i];
            for (int p = 0; p < degree[u]; ++p) {
                const auto& a = arcs[u][p];
                if (std::fabs(times[a.to] - times[u] - a.lag) >= 1e-7L || tight[a.to] == 0) continue;
                long double share = flow[a.to] / tight[a.to];
                flow[u] += share;
                if (u % 5 == 2 && a.lag > 2 * sim::RELEASE_GUARD) weights[u / 5] += share;
            }
        }
        return weights;
    }

    Outcome outcome(const TransportPlan& plan, long long evaluations) {
        Outcome out;
        out.strategy = "transport-search";
        out.evals = evaluations;
        out.batchLimit = in.C;
        out.candidate.sequence = plan.machines;
        Result& r = out.result;
        r.mean = evaluate(plan);
        r.feasible = std::isfinite(r.mean);
        if (!r.feasible) return out;
        r.completion.resize(in.n);
        r.processStart.resize(in.n);
        r.processEnd.resize(in.n);
        r.inboundPickup.resize(in.n);
        r.alphaDelivery.resize(in.n);
        r.betaPickup.resize(in.n);
        r.blockedByMachine.assign(in.m, 0);
        r.machineStarvedByMachine.assign(in.m, std::numeric_limits<long double>::quiet_NaN());
        r.emptyTravelByAgv.assign(in.K, 0);
        r.loadedTravelByAgv.assign(in.K, 0);
        r.idleVehicleTimeByAgv.assign(in.K, 0);
        r.inputTripsByAgv.assign(in.K, -1);
        r.outputTripsByAgv.assign(in.K, -1);
        r.inputLotsByAgv.assign(in.K, 0);
        r.outputLotsByAgv.assign(in.K, 0);
        for (int lot = 0; lot < in.n; ++lot) {
            r.inboundPickup[lot] = times[5 * lot];
            r.alphaDelivery[lot] = times[5 * lot + 1];
            r.processStart[lot] = times[5 * lot + 2];
            r.processEnd[lot] = times[5 * lot + 2] + in.duration(lot, machineOf[lot]);
            r.betaPickup[lot] = times[5 * lot + 3];
            r.completion[lot] = times[5 * lot + 4];
            r.makespan = std::max(r.makespan, r.completion[lot]);
        }
        for (int j = 0; j < in.m; ++j) {
            long double ready = 0;
            int prev = -1;
            for (int lot : plan.machines[j]) {
                r.log.push_back({LogType::Process, j, lot, -1, -1, r.processStart[lot]});
                long double blocked = prev >= 0 ? std::max(0.0L, r.betaPickup[prev] - r.processEnd[lot]) : 0;
                r.blockedByMachine[j] += blocked;
                if (blocked > sim::RELEASE_GUARD) ++r.blockedCount;
                r.machineIdleTime += std::max(0.0L, r.processStart[lot] - ready);
                ready = r.processEnd[lot] + blocked;
                prev = lot;
            }
        }
        r.blockedTime = std::accumulate(r.blockedByMachine.begin(), r.blockedByMachine.end(), 0.0L);
        for (int k = 0; k < in.K; ++k) {
            int position = in.o, load = 0;
            long double previous = 0;
            for (int node : plan.vehicles[k]) {
                int loc = location(node), stage = node % 5, lot = node / 5;
                long double travel = in.tau[position][loc];
                if (position != loc) r.log.push_back({LogType::Move, k, -1, position, loc, previous});
                if (load == 0) r.emptyTravelByAgv[k] += travel;
                else r.loadedTravelByAgv[k] += travel;
                bool pickup = stage == 0 || stage == 3;
                r.log.push_back({pickup ? LogType::Pickup : LogType::Deliver, k, lot, loc, -1, times[node]});
                load += pickup ? 1 : -1;
                if (stage == 0) ++r.inputLotsByAgv[k];
                if (stage == 3) ++r.outputLotsByAgv[k];
                previous = times[node];
                position = loc;
            }
            r.idleVehicleTimeByAgv[k] = r.makespan - r.emptyTravelByAgv[k] - r.loadedTravelByAgv[k];
        }
        r.emptyTravel = std::accumulate(r.emptyTravelByAgv.begin(), r.emptyTravelByAgv.end(), 0.0L);
        r.loadedTravel = std::accumulate(r.loadedTravelByAgv.begin(), r.loadedTravelByAgv.end(), 0.0L);
        r.deliveries = in.n;
        return out;
    }
};

inline bool mutateTransport(const Instance& in, TransportPlan& plan,
                            const std::vector<long double>& times, std::mt19937_64& rng,
                            bool fixedMachines = false) {
    int op = static_cast<int>(rng() % 100);
    if (fixedMachines && op >= 77 && op < 90) return false;
    if (op < 40) {
        auto& seq = plan.vehicles[rng() % in.K];
        if (seq.size() < 2) return false;
        int a = static_cast<int>(rng() % seq.size());
        int radius = 2 * in.C + 3;
        int b = std::clamp(a + static_cast<int>(rng() % (2 * radius + 1)) - radius,
                           0, static_cast<int>(seq.size()) - 1);
        if (a == b) return false;
        if (op < 30) {
            int node = seq[a];
            seq.erase(seq.begin() + a);
            seq.insert(seq.begin() + b, node);
        } else std::swap(seq[a], seq[b]);
    } else if (op < 70) {
        int lot = static_cast<int>(rng() % in.n);
        int stage = rng() % 2 == 0 ? 0 : 3;
        int pickup = 5 * lot + stage, delivery = pickup + 1;
        for (auto& seq : plan.vehicles) {
            seq.erase(std::remove(seq.begin(), seq.end(), pickup), seq.end());
            seq.erase(std::remove(seq.begin(), seq.end(), delivery), seq.end());
        }
        auto& seq = plan.vehicles[rng() % in.K];
        long double target = times[pickup] + (static_cast<int>(rng() % 201) - 100);
        int at = 0;
        while (at < static_cast<int>(seq.size()) && times[seq[at]] < target) ++at;
        seq.insert(seq.begin() + at, pickup);
        int limit = std::min(static_cast<int>(seq.size()), at + 2 * in.C + 5);
        int end = at + 1 + static_cast<int>(rng() % (limit - at));
        seq.insert(seq.begin() + end, delivery);
    } else if (op < 83) {
        int a = static_cast<int>(rng() % in.n), b = static_cast<int>(rng() % in.n);
        if (a == b) return false;
        bool whole = op >= 77;
        if (whole) {
            int ma = -1, mb = -1;
            for (int j = 0; j < in.m; ++j) for (int lot : plan.machines[j]) {
                if (lot == a) ma = j;
                if (lot == b) mb = j;
            }
            if (!in.compatible(a, mb) || !in.compatible(b, ma)) return false;
            for (auto& seq : plan.machines) for (int& lot : seq) {
                if (lot == a) lot = b;
                else if (lot == b) lot = a;
            }
        }
        int stage = rng() % 2 == 0 ? 0 : 3;
        for (auto& seq : plan.vehicles) for (int& node : seq) {
            if (whole || node % 5 == stage || node % 5 == stage + 1) {
                if (node / 5 == a) node = 5 * b + node % 5;
                else if (node / 5 == b) node = 5 * a + node % 5;
            }
        }
    } else if (op < 90) {
        int source = static_cast<int>(rng() % in.m);
        if (plan.machines[source].empty()) return false;
        int a = static_cast<int>(rng() % plan.machines[source].size());
        int lot = plan.machines[source][a];
        int dest = rng() % 2 == 0 ? source : static_cast<int>(rng() % in.m);
        if (!in.compatible(lot, dest)) return false;
        plan.machines[source].erase(plan.machines[source].begin() + a);
        long double target = times[5 * lot + 2];
        int b = 0;
        while (b < static_cast<int>(plan.machines[dest].size()) &&
               times[5 * plan.machines[dest][b] + 2] < target) ++b;
        b = std::clamp(b + static_cast<int>(rng() % 3) - 1, 0,
                       static_cast<int>(plan.machines[dest].size()));
        plan.machines[dest].insert(plan.machines[dest].begin() + b, lot);
    } else {
        int a = static_cast<int>(rng() % in.K), b = static_cast<int>(rng() % in.K);
        if (a == b) return false;
        auto boundaries = [](const std::vector<int>& seq) {
            std::vector<int> result{0};
            int load = 0;
            for (int i = 0; i < static_cast<int>(seq.size()); ++i) {
                int stage = seq[i] % 5;
                load += stage == 0 || stage == 3 ? 1 : -1;
                if (load == 0) result.push_back(i + 1);
            }
            return result;
        };
        auto ba = boundaries(plan.vehicles[a]), bb = boundaries(plan.vehicles[b]);
        int ia = static_cast<int>(rng() % ba.size());
        long double target = ba[ia] > 0 ? times[plan.vehicles[a][ba[ia] - 1]] : 0;
        std::vector<int> near;
        for (int p : bb) {
            long double time = p > 0 ? times[plan.vehicles[b][p - 1]] : 0;
            if (std::fabs(time - target) <= 150) near.push_back(p);
        }
        if (near.empty()) return false;
        int at = ba[ia], bt = near[rng() % near.size()];
        if (op < 95) {
            std::vector<int> suffix(plan.vehicles[a].begin() + at, plan.vehicles[a].end());
            plan.vehicles[a].erase(plan.vehicles[a].begin() + at, plan.vehicles[a].end());
            plan.vehicles[a].insert(plan.vehicles[a].end(), plan.vehicles[b].begin() + bt, plan.vehicles[b].end());
            plan.vehicles[b].erase(plan.vehicles[b].begin() + bt, plan.vehicles[b].end());
            plan.vehicles[b].insert(plan.vehicles[b].end(), suffix.begin(), suffix.end());
        } else {
            if (ia + 1 >= static_cast<int>(ba.size())) return false;
            std::vector<int> block(plan.vehicles[a].begin() + at, plan.vehicles[a].begin() + ba[ia + 1]);
            plan.vehicles[a].erase(plan.vehicles[a].begin() + at, plan.vehicles[a].begin() + ba[ia + 1]);
            plan.vehicles[b].insert(plan.vehicles[b].begin() + bt, block.begin(), block.end());
        }
    }
    return true;
}

struct TransportSearchConfig {
    long double temperature = 0.5L;
    int cycle = 50000;
    std::ostream* trace = nullptr;
    bool fixedMachines = false;
    bool lns = false;
    int destroySize = 4;
    int repairWidth = 32;
    bool assignment = false;
    bool jobs = false;
    int window = 0;
    bool requestScan = false;
    bool fractional = false;
};

template <typename Cost>
inline std::vector<int> transportMatching(int n, Cost cost) {
    std::vector<long double> u(n + 1), v(n + 1), minimum(n + 1);
    std::vector<int> owner(n + 1), way(n + 1), used(n + 1);
    for (int row = 1; row <= n; ++row) {
        owner[0] = row;
        int column = 0;
        std::fill(minimum.begin(), minimum.end(), std::numeric_limits<long double>::infinity());
        std::fill(used.begin(), used.end(), 0);
        do {
            used[column] = 1;
            int current = owner[column], next = 0;
            long double delta = std::numeric_limits<long double>::infinity();
            for (int c = 1; c <= n; ++c) if (!used[c]) {
                long double value = cost(current - 1, c - 1) - u[current] - v[c];
                if (value < minimum[c]) { minimum[c] = value; way[c] = column; }
                if (minimum[c] < delta) { delta = minimum[c]; next = c; }
            }
            for (int c = 0; c <= n; ++c) {
                if (used[c]) { u[owner[c]] += delta; v[c] -= delta; }
                else minimum[c] -= delta;
            }
            column = next;
        } while (owner[column] != 0);
        do {
            int previous = way[column];
            owner[column] = owner[previous];
            column = previous;
        } while (column != 0);
    }
    std::vector<int> result(n);
    for (int c = 1; c <= n; ++c) result[c - 1] = owner[c] - 1;
    return result;
}

inline void permuteTransportLots(TransportPlan& plan, const std::vector<int>& mapping) {
    for (auto& seq : plan.machines) for (int& lot : seq) lot = mapping[lot];
    for (auto& seq : plan.vehicles) for (int& node : seq) node = 5 * mapping[node / 5] + node % 5;
}

inline void sortTransportLots(const Instance& in, TransportPlan& plan) {
    std::vector<int> mapping(in.n);
    std::iota(mapping.begin(), mapping.end(), 0);
    for (int j = 0; j < in.m; ++j) {
        auto sorted = plan.machines[j];
        std::sort(sorted.begin(), sorted.end(), [&](int a, int b) {
            long double da = in.duration(a, j), db = in.duration(b, j);
            return da != db ? da < db : a < b;
        });
        for (int p = 0; p < static_cast<int>(sorted.size()); ++p) mapping[plan.machines[j][p]] = sorted[p];
    }
    permuteTransportLots(plan, mapping);
}

inline void improveFractionalAssignment(const Instance& in, TransportPlan& plan,
                                         TransportEvaluator& evaluator, std::mt19937_64& rng,
                                         long long& evaluations, long long budget) {
    TransportPlan base = plan, best = plan;
    long double bestMean = evaluator.evaluate(base);
    if (evaluations < budget) ++evaluations;
    auto machine = evaluator.machineOf;
    int n = in.n;
    std::vector<long double> x(static_cast<std::size_t>(n) * n), processing(n), vertex(n), mixed(n);
    std::vector<std::vector<long double>> p(n, std::vector<long double>(n, 1e15L));
    for (int lot = 0; lot < n; ++lot) {
        x[static_cast<std::size_t>(lot) * n + lot] = 1;
        processing[lot] = in.duration(lot, machine[lot]);
        for (int slot = 0; slot < n; ++slot)
            if (in.compatible(lot, machine[slot])) p[lot][slot] = in.duration(lot, machine[slot]);
    }
    for (int iteration = 0; iteration < 300 && evaluations + 40 < budget; ++iteration) {
        evaluator.evaluate(base, false, &processing);
        ++evaluations;
        auto weights = evaluator.durationWeights();
        auto mapping = transportMatching(n, [&](int lot, int slot) {
            return p[lot][slot] >= 1e14L ? 1e15L : weights[slot] * p[lot][slot];
        });
        for (int slot = 0; slot < n; ++slot) vertex[slot] = p[mapping[slot]][slot];
        auto value = [&](long double step) {
            for (int slot = 0; slot < n; ++slot) mixed[slot] = processing[slot] + step * (vertex[slot] - processing[slot]);
            ++evaluations;
            return evaluator.evaluate(base, false, &mixed);
        };
        long double left = 0, right = 1;
        for (int search = 0; search < 16; ++search) {
            long double a = (2 * left + right) / 3, b = (left + 2 * right) / 3;
            if (value(a) < value(b)) right = b;
            else left = a;
        }
        long double step = (left + right) / 2;
        long double old = value(0), updated = value(step);
        if (updated >= old) step = 0;
        for (auto& entry : x) entry *= 1 - step;
        for (int slot = 0; slot < n; ++slot) {
            x[static_cast<std::size_t>(mapping[slot]) * n + slot] += step;
            processing[slot] += step * (vertex[slot] - processing[slot]);
        }
    }
    for (int sample = 0; sample < 150 && evaluations < budget; ++sample) {
        std::vector<long double> perturbation(static_cast<std::size_t>(n) * n);
        long double noise = sample == 0 ? 0 : (sample % 10) * 0.05L;
        for (auto& entry : perturbation) entry = noise * static_cast<long double>(rng() % 10001) / 10000;
        auto mapping = transportMatching(n, [&](int lot, int slot) {
            if (p[lot][slot] >= 1e14L) return 1e15L;
            return -x[static_cast<std::size_t>(lot) * n + slot] + perturbation[static_cast<std::size_t>(lot) * n + slot];
        });
        TransportPlan trial = base;
        permuteTransportLots(trial, mapping);
        long double mean = evaluator.evaluate(trial);
        ++evaluations;
        if (mean < bestMean) { bestMean = mean; best = trial; }
        std::vector<int> seen(n), subset(n);
        trial = base;
        long double current = evaluator.evaluate(trial);
        if (evaluations < budget) ++evaluations;
        for (int root = 0; root < n && evaluations < budget; ++root) {
            if (seen[root] || mapping[root] == root) continue;
            std::iota(subset.begin(), subset.end(), 0);
            for (int at = root; !seen[at]; at = mapping[at]) { seen[at] = 1; subset[at] = mapping[at]; }
            TransportPlan next = trial;
            permuteTransportLots(next, subset);
            mean = evaluator.evaluate(next);
            ++evaluations;
            if (mean < current) { current = mean; trial = std::move(next); }
        }
        if (current < bestMean) { bestMean = current; best = std::move(trial); }
    }
    plan = std::move(best);
    evaluator.evaluate(plan);
}

inline void improveTransportAssignment(const Instance& in, TransportPlan& plan,
                                        TransportEvaluator& evaluator, std::mt19937_64& rng,
                                        long long& evaluations, long long budget, int iterations = 40) {
    long double mean = evaluator.evaluate(plan);
    if (evaluations < budget) ++evaluations;
    for (int attempt = 0; attempt < iterations && evaluations < budget; ++attempt) {
        auto gradient = evaluator.durationWeights();
        std::vector<int> machineOf = evaluator.machineOf;
        std::vector<long double> rank(in.n), weight(in.n);
        for (const auto& seq : plan.machines)
            for (int p = 0; p < static_cast<int>(seq.size()); ++p) rank[seq[p]] = seq.size() - p;
        long double totalRank = std::accumulate(rank.begin(), rank.end(), 0.0L);
        long double totalGradient = std::accumulate(gradient.begin(), gradient.end(), 0.0L);
        long double alpha = (attempt % 5) / 4.0L;
        for (int slot = 0; slot < in.n; ++slot) {
            long double critical = totalGradient > 0 ? gradient[slot] * totalRank / totalGradient : rank[slot];
            long double noise = attempt < 5 ? 1 : 0.8L + (rng() % 4001) / 10000.0L;
            weight[slot] = ((1 - alpha) * rank[slot] + alpha * critical) * noise;
        }
        long double penalty = attempt % 4 == 0 ? 0 : static_cast<long double>(rng() % 10);
        auto mapping = transportMatching(in.n, [&](int lot, int slot) {
            int j = machineOf[slot];
            if (!in.compatible(lot, j)) return 1e15L;
            return weight[slot] * in.duration(lot, j) + (lot == slot ? 0 : penalty * in.duration(lot, j));
        });
        TransportPlan trial = plan;
        permuteTransportLots(trial, mapping);
        long double tested = evaluator.evaluate(trial);
        ++evaluations;
        if (tested < mean - 1e-9L) {
            plan = std::move(trial);
            mean = tested;
        } else {
            std::vector<int> seen(in.n), subset(in.n);
            for (int root = 0; root < in.n && evaluations < budget; ++root) {
                if (seen[root] || mapping[root] == root) continue;
                std::iota(subset.begin(), subset.end(), 0);
                for (int at = root; !seen[at]; at = mapping[at]) {
                    seen[at] = 1;
                    subset[at] = mapping[at];
                }
                trial = plan;
                permuteTransportLots(trial, subset);
                tested = evaluator.evaluate(trial);
                ++evaluations;
                if (tested < mean - 1e-9L) { plan = std::move(trial); mean = tested; }
            }
        }
        evaluator.evaluate(plan);
        if (evaluations < budget) ++evaluations;
    }
}

inline bool repairTransportRequest(const Instance& in, TransportPlan& plan, int request,
                                    const std::vector<long double>& reference,
                                    TransportEvaluator& evaluator, int width,
                                    long long& evaluations, long long budget) {
    if (evaluations >= budget) return false;
    long double partial = evaluator.evaluate(plan, true);
    ++evaluations;
    if (!std::isfinite(partial)) return false;
    int pickup = 5 * (request / 2) + (request % 2 == 0 ? 0 : 3);
    int delivery = pickup + 1;
    int pickLoc = evaluator.location(pickup), dropLoc = evaluator.location(delivery);
    struct Insertion { long double cost; int k; int p; int q; };
    std::vector<Insertion> candidates;
    for (int k = 0; k < in.K; ++k) {
        const auto& seq = plan.vehicles[k];
        int n = static_cast<int>(seq.size());
        std::vector<int> load(n + 1), locations(n);
        std::vector<long double> distance(n + 1);
        int previous = in.o;
        for (int i = 0; i < n; ++i) {
            locations[i] = evaluator.location(seq[i]);
            load[i + 1] = load[i] + (seq[i] % 5 == 0 || seq[i] % 5 == 3 ? 1 : -1);
            distance[i + 1] = distance[i] + in.tau[previous][locations[i]];
            previous = locations[i];
        }
        for (int p = 0; p <= n; ++p) {
            if (load[p] >= in.C) continue;
            int before = p > 0 ? locations[p - 1] : in.o;
            long double ready = p > 0 ? evaluator.times[seq[p - 1]] : 0;
            long double pickTime = ready + in.tau[before][pickLoc];
            if (request % 2 != 0) pickTime = std::max(pickTime, evaluator.times[pickup]);
            if (pickTime > reference[pickup] + 300) continue;
            for (int q = p; q <= n; ++q) {
                if (load[q] >= in.C) break;
                long double delta, dropTime;
                if (p == q) {
                    delta = in.tau[before][pickLoc] + in.tau[pickLoc][dropLoc];
                    if (q < n) delta += in.tau[dropLoc][locations[q]] - in.tau[before][locations[q]];
                    dropTime = pickTime + in.tau[pickLoc][dropLoc];
                } else {
                    delta = in.tau[before][pickLoc] + in.tau[pickLoc][locations[p]] - in.tau[before][locations[p]];
                    int beforeDrop = locations[q - 1];
                    delta += in.tau[beforeDrop][dropLoc];
                    if (q < n) delta += in.tau[dropLoc][locations[q]] - in.tau[beforeDrop][locations[q]];
                    dropTime = pickTime + in.tau[pickLoc][locations[p]]
                             + distance[q] - distance[p + 1] + in.tau[beforeDrop][dropLoc];
                }
                long double cost = delta + 0.15L * std::fabs(dropTime - reference[delivery]);
                candidates.push_back({cost, k, p, q});
            }
        }
    }
    auto less = [](const Insertion& a, const Insertion& b) {
        if (a.cost != b.cost) return a.cost < b.cost;
        if (a.k != b.k) return a.k < b.k;
        if (a.p != b.p) return a.p < b.p;
        return a.q < b.q;
    };
    int count = std::min(width, static_cast<int>(candidates.size()));
    std::partial_sort(candidates.begin(), candidates.begin() + count, candidates.end(), less);
    long double best = std::numeric_limits<long double>::infinity();
    TransportPlan bestPlan;
    for (int i = 0; i < count && evaluations < budget; ++i) {
        const auto& move = candidates[i];
        TransportPlan trial = plan;
        auto& seq = trial.vehicles[move.k];
        seq.insert(seq.begin() + move.q, delivery);
        seq.insert(seq.begin() + move.p, pickup);
        long double mean = evaluator.evaluate(trial, true);
        ++evaluations;
        if (mean < best) {
            best = mean;
            bestPlan = std::move(trial);
        }
    }
    if (!std::isfinite(best)) return false;
    plan = std::move(bestPlan);
    return true;
}

inline void shuffleTransportRequests(std::vector<int>& values, std::mt19937_64& rng) {
    for (std::size_t i = values.size(); i > 1; --i) {
        std::size_t j = static_cast<std::size_t>(rng() % i);
        std::swap(values[i - 1], values[j]);
    }
}

inline bool ruinRepairTransport(const Instance& in, TransportPlan& plan,
                                 const std::vector<long double>& reference,
                                 TransportEvaluator& evaluator, std::mt19937_64& rng,
                                 const TransportSearchConfig& config,
                                 long long& evaluations, long long budget, long double& mean) {
    int first = static_cast<int>(rng() % (2 * in.n));
    int mode = static_cast<int>(rng() % 4);
    int count = std::min(2 * in.n, 2 + static_cast<int>(rng() % std::max(1, config.destroySize - 1)));
    if (mode <= 1) count = std::min(2 * in.n, 2 * (2 + static_cast<int>(rng() % 2)));
    std::vector<int> removed{first};
    if (count > 1 && mode <= 1) removed.push_back(first ^ 1);
    while (static_cast<int>(removed.size()) < count) {
        int request = -1;
        long double distance = std::numeric_limits<long double>::infinity();
        int tries = mode == 3 ? 1 : 12;
        for (int i = 0; i < tries; ++i) {
            int trial = static_cast<int>(rng() % (2 * in.n));
            if (std::find(removed.begin(), removed.end(), trial) != removed.end()) continue;
            int stage = trial % 2 == 0 ? 0 : 3;
            int firstStage = first % 2 == 0 ? 0 : 3;
            long double value = std::fabs(reference[5 * (trial / 2) + stage] - reference[5 * (first / 2) + firstStage]);
            if (value < distance) { distance = value; request = trial; }
        }
        if (request >= 0) {
            removed.push_back(request);
            if (mode <= 1 && static_cast<int>(removed.size()) < count &&
                std::find(removed.begin(), removed.end(), request ^ 1) == removed.end()) removed.push_back(request ^ 1);
        }
    }
    for (auto& seq : plan.vehicles) {
        seq.erase(std::remove_if(seq.begin(), seq.end(), [&](int node) {
            int request = 2 * (node / 5) + (node % 5 >= 3 ? 1 : 0);
            return std::find(removed.begin(), removed.end(), request) != removed.end();
        }), seq.end());
    }
    if (!config.fixedMachines && mode == 0) {
        std::vector<int> jobs;
        for (int request : removed) if (request % 2 == 0 &&
            std::find(removed.begin(), removed.end(), request + 1) != removed.end()) jobs.push_back(request / 2);
        for (int lot : jobs) {
            int source = -1;
            for (int j = 0; j < in.m; ++j)
                if (std::find(plan.machines[j].begin(), plan.machines[j].end(), lot) != plan.machines[j].end()) source = j;
            std::vector<std::pair<long double, int>> compatible;
            for (int j = 0; j < in.m; ++j) if (in.compatible(lot, j))
                compatible.push_back({in.duration(lot, j) + 0.15L * (in.tau[in.o][in.alpha[j]] + in.tau[in.beta[j]][in.f]), j});
            std::sort(compatible.begin(), compatible.end());
            int dest = compatible[rng() % std::min<std::size_t>(3, compatible.size())].second;
            auto& from = plan.machines[source];
            from.erase(std::find(from.begin(), from.end(), lot));
            auto& to = plan.machines[dest];
            long double target = reference[5 * lot + 2] + static_cast<int>(rng() % 401) - 200;
            int at = 0;
            if (rng() % 2 == 0) {
                while (at < static_cast<int>(to.size()) && in.duration(to[at], dest) < in.duration(lot, dest)) ++at;
            } else while (at < static_cast<int>(to.size()) && reference[5 * to[at] + 2] < target) ++at;
            if (at < static_cast<int>(to.size())) {
                int neighbor = 2 * to[at];
                if (std::find(removed.begin(), removed.end(), neighbor) == removed.end()) removed.push_back(neighbor);
            }
            to.insert(to.begin() + at, lot);
        }
        for (auto& seq : plan.vehicles) seq.erase(std::remove_if(seq.begin(), seq.end(), [&](int node) {
            int request = 2 * (node / 5) + (node % 5 >= 3 ? 1 : 0);
            return std::find(removed.begin(), removed.end(), request) != removed.end();
        }), seq.end());
    }
    shuffleTransportRequests(removed, rng);
    if (rng() % 2 == 0) std::stable_sort(removed.begin(), removed.end(), [](int a, int b) { return a % 2 < b % 2; });
    for (int request : removed) {
        if (!repairTransportRequest(in, plan, request, reference, evaluator, config.repairWidth, evaluations, budget)) return false;
    }
    if (evaluations >= budget) return false;
    mean = evaluator.evaluate(plan);
    ++evaluations;
    return std::isfinite(mean);
}

inline long double scanTransportRequest(const Instance& in, TransportPlan& plan,
                                        const std::vector<long double>& reference,
                                        TransportEvaluator& evaluator, std::mt19937_64& rng,
                                        const TransportSearchConfig& config,
                                        long long& evaluations, long long budget) {
    TransportPlan original = plan;
    long double before = evaluator.evaluate(plan);
    if (evaluations < budget) ++evaluations;
    int request = static_cast<int>(rng() % (2 * in.n));
    int pickup = 5 * (request / 2) + (request % 2 == 0 ? 0 : 3);
    for (auto& seq : plan.vehicles) seq.erase(std::remove_if(seq.begin(), seq.end(),
        [&](int node) { return node == pickup || node == pickup + 1; }), seq.end());
    if (!repairTransportRequest(in, plan, request, reference, evaluator,
                                std::max(20000, config.repairWidth), evaluations, budget)) plan = original;
    long double after = evaluator.evaluate(plan);
    if (evaluations < budget) ++evaluations;
    if (after > before) { plan = std::move(original); after = evaluator.evaluate(plan); }
    return after;
}

inline long double relocateTransportJob(const Instance& in, TransportPlan& plan,
                                        const std::vector<long double>& reference,
                                        TransportEvaluator& evaluator, std::mt19937_64& rng,
                                        const TransportSearchConfig& config,
                                        long long& evaluations, long long budget) {
    long double incumbent = evaluator.evaluate(plan);
    if (evaluations < budget) ++evaluations;
    int lot = static_cast<int>(rng() % in.n);
    int source = evaluator.machineOf[lot];
    TransportPlan base = plan;
    auto& from = base.machines[source];
    from.erase(std::find(from.begin(), from.end(), lot));
    for (auto& seq : base.vehicles) seq.erase(std::remove_if(seq.begin(), seq.end(),
        [&](int node) { return node / 5 == lot; }), seq.end());
    struct Placement { long double score; int machine; int at; };
    std::vector<Placement> placements;
    for (int j = 0; j < in.m && evaluations < budget; ++j) {
        if (!in.compatible(lot, j) || (config.fixedMachines && j != source)) continue;
        for (int p = 0; p <= static_cast<int>(base.machines[j].size()) && evaluations < budget; ++p) {
            TransportPlan trial = base;
            trial.machines[j].insert(trial.machines[j].begin() + p, lot);
            long double score = evaluator.evaluate(trial, true);
            ++evaluations;
            if (std::isfinite(score)) placements.push_back({score, j, p});
        }
    }
    std::sort(placements.begin(), placements.end(), [](const Placement& a, const Placement& b) {
        if (a.score != b.score) return a.score < b.score;
        if (a.machine != b.machine) return a.machine < b.machine;
        return a.at < b.at;
    });
    TransportPlan best = plan;
    long double bestMean = incumbent;
    int count = std::min(12, static_cast<int>(placements.size()));
    for (int i = 0; i < count && evaluations < budget; ++i) {
        const auto& placement = placements[i];
        TransportPlan trial = base;
        trial.machines[placement.machine].insert(trial.machines[placement.machine].begin() + placement.at, lot);
        evaluator.evaluate(trial, true);
        ++evaluations;
        auto target = reference;
        long double start = evaluator.times[5 * lot + 2];
        target[5 * lot] = std::max(0.0L, start - in.tau[in.o][in.alpha[placement.machine]]);
        target[5 * lot + 1] = start;
        if (!repairTransportRequest(in, trial, 2 * lot, target, evaluator, config.repairWidth, evaluations, budget)) continue;
        if (evaluations >= budget) break;
        evaluator.evaluate(trial, true);
        ++evaluations;
        target[5 * lot + 3] = evaluator.times[5 * lot + 3];
        target[5 * lot + 4] = target[5 * lot + 3] + in.tau[in.beta[placement.machine]][in.f];
        if (!repairTransportRequest(in, trial, 2 * lot + 1, target, evaluator, config.repairWidth, evaluations, budget)) continue;
        if (evaluations >= budget) break;
        long double score = evaluator.evaluate(trial);
        ++evaluations;
        if (score < bestMean - 1e-9L) { bestMean = score; best = std::move(trial); }
    }
    plan = std::move(best);
    evaluator.evaluate(plan);
    return bestMean;
}

inline bool reorderTransportWindow(const Instance& in, TransportPlan& plan,
                                    const std::vector<long double>& reference,
                                    std::mt19937_64& rng, int length) {
    int k = static_cast<int>(rng() % in.K);
    auto& seq = plan.vehicles[k];
    int size = std::min(length, static_cast<int>(seq.size()));
    if (size < 2) return false;
    int first = static_cast<int>(rng() % (seq.size() - size + 1));
    std::vector<int> nodes(seq.begin() + first, seq.begin() + first + size);
    std::vector<int> index(5 * in.n, -1), previous(in.n, -1), nextTwo(in.n, -1);
    for (int i = 0; i < size; ++i) index[nodes[i]] = i;
    std::vector<int> machine(in.n);
    for (int j = 0; j < in.m; ++j) {
        const auto& jobs = plan.machines[j];
        for (int p = 0; p < static_cast<int>(jobs.size()); ++p) {
            machine[jobs[p]] = j;
            if (p > 0) previous[jobs[p]] = jobs[p - 1];
            if (p + 2 < static_cast<int>(jobs.size())) nextTwo[jobs[p]] = jobs[p + 2];
        }
    }
    auto location = [&](int node) {
        int stage = node % 5;
        return stage == 0 ? in.o : stage == 4 ? in.f : stage == 1 ? in.alpha[machine[node / 5]] : in.beta[machine[node / 5]];
    };
    int load = 0;
    for (int p = 0; p < first; ++p) load += seq[p] % 5 == 0 || seq[p] % 5 == 3 ? 1 : -1;
    int startLocation = first > 0 ? location(seq[first - 1]) : in.o;
    long double startTime = first > 0 ? reference[seq[first - 1]] : 0;
    long double slack = static_cast<long double>(rng() % 4) * in.medianTau;
    std::vector<long double> release(size), due(size, std::numeric_limits<long double>::infinity());
    std::vector<int> prerequisites(size), change(size), locations(size);
    for (int i = 0; i < size; ++i) {
        int node = nodes[i], lot = node / 5, stage = node % 5;
        locations[i] = location(node);
        change[i] = stage == 0 || stage == 3 ? 1 : -1;
        if (stage == 1 || stage == 4) {
            int paired = index[node - 1];
            if (paired >= 0) prerequisites[i] |= 1 << paired;
        }
        if (stage == 1) {
            release[i] = previous[lot] >= 0 ? reference[5 * previous[lot] + 2] + sim::RELEASE_GUARD : 0;
            due[i] = reference[5 * lot + 2] + slack;
        }
        if (stage == 3) {
            release[i] = reference[5 * lot + 2] + in.duration(lot, machine[lot]) + sim::RELEASE_GUARD;
            if (previous[lot] >= 0) {
                int before = 5 * previous[lot] + 3;
                if (index[before] >= 0) prerequisites[i] |= 1 << index[before];
                else release[i] = std::max(release[i], reference[before] + sim::RELEASE_GUARD);
            }
            if (nextTwo[lot] >= 0) due[i] = reference[5 * nextTwo[lot] + 2] + slack;
        }
    }
    int states = 1 << size;
    std::vector<int> loads(states, load);
    for (int mask = 1; mask < states; ++mask) {
        int bit = 0;
        while ((mask & (1 << bit)) == 0) ++bit;
        loads[mask] = loads[mask ^ (1 << bit)] + change[bit];
    }
    const long double invalid = std::numeric_limits<long double>::infinity();
    std::vector<long double> time(static_cast<std::size_t>(states) * size, invalid);
    std::vector<std::int8_t> parent(static_cast<std::size_t>(states) * size, -1);
    for (int i = 0; i < size; ++i) if (prerequisites[i] == 0 && loads[1 << i] >= 0 && loads[1 << i] <= in.C) {
        long double at = std::max(release[i], startTime + in.tau[startLocation][locations[i]] + sim::RELEASE_GUARD);
        if (at <= due[i] + sim::EPS) time[static_cast<std::size_t>(1 << i) * size + i] = at;
    }
    for (int mask = 1; mask < states; ++mask) {
        if (loads[mask] < 0 || loads[mask] > in.C) continue;
        for (int last = 0; last < size; ++last) {
            long double now = time[static_cast<std::size_t>(mask) * size + last];
            if (!std::isfinite(now)) continue;
            for (int next = 0; next < size; ++next) {
                if ((mask & (1 << next)) || (mask & prerequisites[next]) != prerequisites[next]) continue;
                int after = mask | (1 << next);
                if (loads[after] < 0 || loads[after] > in.C) continue;
                long double arrival = std::max(release[next], now + in.tau[locations[last]][locations[next]] + sim::RELEASE_GUARD);
                std::size_t state = static_cast<std::size_t>(after) * size + next;
                if (arrival <= due[next] + sim::EPS && arrival < time[state]) {
                    time[state] = arrival;
                    parent[state] = static_cast<std::int8_t>(last);
                }
            }
        }
    }
    int last = -1;
    long double best = invalid;
    for (int i = 0; i < size; ++i) {
        long double value = time[static_cast<std::size_t>(states - 1) * size + i];
        if (first + size < static_cast<int>(seq.size())) value += in.tau[locations[i]][location(seq[first + size])];
        if (value < best) { best = value; last = i; }
    }
    if (last < 0) return false;
    int mask = states - 1;
    std::vector<int> result(size);
    for (int p = size - 1; p >= 0; --p) {
        result[p] = nodes[last];
        int before = parent[static_cast<std::size_t>(mask) * size + last];
        mask ^= 1 << last;
        last = before;
    }
    if (result == nodes) return false;
    std::copy(result.begin(), result.end(), seq.begin() + first);
    return true;
}

inline Outcome solveTransport(const Instance& in, TransportPlan start, const Options& options,
                              const TransportSearchConfig& config = {}) {
    if (!std::isfinite(config.temperature) || config.temperature < 0 || config.cycle < 1 ||
        config.destroySize < 2 || config.repairWidth < 1 || config.window < 0 || config.window > 16)
        throw std::runtime_error("parametros de transporte incorrectos");
    TransportEvaluator evaluator(in);
    long double currentMean = evaluator.evaluate(start);
    if (!std::isfinite(currentMean)) throw std::runtime_error("plan de transporte inicial no factible");
    TransportPlan best = start, current = start;
    long double bestMean = currentMean;
    std::vector<long double> currentTimes = evaluator.times;
    std::mt19937_64 rng(options.seed);
    long long evaluations = 1;
    long long feasible = 1, accepted = 0;
    int budget = std::max(1, options.maxEvals);
    if (config.trace) *config.trace << "evals,best_mean,current_mean,feasible_proposals,accepted\n"
                                   << std::setprecision(15) << "1," << bestMean << ',' << currentMean << ",1,0\n";
    if (config.assignment && !config.fixedMachines && evaluations < budget) {
        improveTransportAssignment(in, current, evaluator, rng, evaluations, budget);
        currentMean = evaluator.evaluate(current);
        best = current;
        bestMean = currentMean;
        currentTimes = evaluator.times;
        if (config.trace) *config.trace << evaluations << ',' << bestMean << ',' << currentMean << ",1,0\n";
    }
    if (config.fractional && !config.fixedMachines && evaluations < budget) {
        improveFractionalAssignment(in, current, evaluator, rng, evaluations, budget);
        currentMean = evaluator.evaluate(current);
        best = current;
        bestMean = currentMean;
        currentTimes = evaluator.times;
        if (config.trace) *config.trace << evaluations << ',' << bestMean << ',' << currentMean << ",1,0\n";
    }
    while (evaluations < budget) {
        long long previousEvaluations = evaluations;
        TransportPlan trial = current;
        long double mean = std::numeric_limits<long double>::infinity();
        if (config.requestScan && rng() % 100 == 0) {
            mean = scanTransportRequest(in, trial, currentTimes, evaluator, rng, config, evaluations, budget);
        } else if (config.window >= 2 && rng() % 1000 == 0) {
            if (!reorderTransportWindow(in, trial, currentTimes, rng, config.window)) continue;
            mean = evaluator.evaluate(trial);
            ++evaluations;
        } else if (config.jobs && !config.fixedMachines && rng() % 20 == 0) {
            mean = relocateTransportJob(in, trial, currentTimes, evaluator, rng, config, evaluations, budget);
        } else if (config.lns && rng() % 5 == 0) {
            ruinRepairTransport(in, trial, currentTimes, evaluator, rng, config, evaluations, budget, mean);
        } else {
            if (!mutateTransport(in, trial, currentTimes, rng, config.fixedMachines)) continue;
            mean = evaluator.evaluate(trial);
            ++evaluations;
        }
        if (std::isfinite(mean)) ++feasible;
        long double cycle = static_cast<long double>(evaluations % config.cycle) / config.cycle;
        long double temperature = config.temperature * std::pow(0.02L, cycle);
        if (std::isfinite(mean) && (mean <= currentMean || (temperature > 0 &&
                std::generate_canonical<double, 53>(rng) < std::exp((currentMean - mean) / temperature)))) {
            ++accepted;
            current = std::move(trial);
            currentMean = mean;
            currentTimes = evaluator.times;
            if (mean < bestMean - 1e-9L) {
                best = current;
                bestMean = mean;
            }
        }
        if (evaluations / config.cycle > previousEvaluations / config.cycle) {
            current = best;
            currentMean = bestMean;
            if (config.assignment && !config.fixedMachines && evaluations < budget) {
                improveTransportAssignment(in, current, evaluator, rng, evaluations, budget, 10);
            }
            currentMean = evaluator.evaluate(current);
            if (currentMean < bestMean) { best = current; bestMean = currentMean; }
            currentTimes = evaluator.times;
        }
        if (config.trace && (evaluations / 10000 > previousEvaluations / 10000 || evaluations == budget))
            *config.trace << evaluations << ',' << bestMean << ',' << currentMean << ',' << feasible << ',' << accepted << '\n';
    }
    return evaluator.outcome(best, evaluations);
}

}
