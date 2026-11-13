#include "heuristic.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

using namespace io1;

struct Config {
    std::string input;
    std::string output;
    std::string txt;
    std::string debugDir;
    std::string fromTxt;
    std::string selector;
    std::string strategy = "vnd";
    std::uint64_t seed = 20261106;
    int maxEvals = 1000;
    int maxNeighbors = 40;
    int phaseGlobal = -1;
    int phaseLookahead = 0;
    int phaseOutboundOnly = 0;
    int phaseHardUnblock = 0;
    std::string policyFile;
    std::string transportTrace;
    long double transportTemperature = 0.5L;
    int transportCycle = 50000;
    bool transportFixedMachines = false;
    bool transportLns = false;
    int transportDestroy = 4;
    int transportRepair = 32;
    std::string transportStart = "phase";
    long double transportStartWeight = 0;
    bool transportAssignment = false;
    bool transportJobs = false;
    int transportRebuild = 0;
    bool transportSpt = false;
    int transportWindow = 0;
    int transportProfile = 0;
    bool transportRequestScan = false;
    bool transportFractional = false;
    int transportSeedBias = 99;
    int transportSeedBatch = 0;
    std::array<double, 9> dispatchWeights{1, 0, 0, 0, 0, 0, 0, 0, 0};
    bool selfTest = false;
    bool listStrategies = false;
    bool dispatchAblation = false;
    bool help = false;
};

std::string trim(const std::string& text) {
    std::size_t begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return "";
    std::size_t end = text.find_last_not_of(" \t\r\n");
    return text.substr(begin, end - begin + 1);
}

bool fileExists(const std::string& path) {
    std::ifstream file(path);
    return file.good();
}

std::string stem(const std::string& path) {
    std::size_t slash = path.find_last_of("/\\");
    std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    std::size_t dot = name.find_last_of('.');
    return dot == std::string::npos ? name : name.substr(0, dot);
}

void loadSelector(const std::string& path, Config& config) {
    std::ifstream file(path);
    if (!file) return;
    std::string line;
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        std::size_t equal = line.find('=');
        if (equal == std::string::npos) continue;
        std::string key = trim(line.substr(0, equal));
        std::string value = trim(line.substr(equal + 1));
        if (key == "strategy") config.strategy = value;
        else if (key == "seed") config.seed = std::stoull(value);
        else if (key == "max_evals") config.maxEvals = std::stoi(value);
        else if (key == "max_neighbors") config.maxNeighbors = std::stoi(value);
        else if (key == "phase_global") {
            if (value == "auto") config.phaseGlobal = -1;
            else config.phaseGlobal = std::stoi(value) == 0 ? 0 : 1;
        }
        else if (key == "phase_lookahead") {
            config.phaseLookahead = std::stoi(value) == 0 ? 0 : 1;
        }
        else if (key == "phase_outbound_only") {
            config.phaseOutboundOnly = std::stoi(value) == 0 ? 0 : 1;
        }
        else if (key == "phase_hard_unblock") {
            config.phaseHardUnblock = std::stoi(value) == 0 ? 0 : 1;
        }
    }
}

Config parseArguments(int argc, char** argv) {
    Config config;
    std::string strategy;
    bool hasStrategy = false;
    bool hasSeed = false;
    bool hasMaxEvals = false;
    bool hasMaxNeighbors = false;
    bool hasPhaseGlobal = false;
    bool hasPhaseLookahead = false;
    bool hasPhaseOutboundOnly = false;
    bool hasPhaseHardUnblock = false;
    std::uint64_t seed = 0;
    int maxEvals = 0;
    int maxNeighbors = 0;
    int phaseGlobal = -1;
    int phaseLookahead = 0;
    int phaseOutboundOnly = 0;
    int phaseHardUnblock = 0;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto value = [&]() {
            if (i + 1 >= argc) throw std::runtime_error("falta valor para " + arg);
            return std::string(argv[++i]);
        };
        if (arg == "--input") config.input = value();
        else if (arg == "--out") config.output = value();
        else if (arg == "--txt") config.txt = value();
        else if (arg == "--debug-dir") config.debugDir = value();
        else if (arg == "--from-txt") config.fromTxt = value();
        else if (arg == "--transport-trace") config.transportTrace = value();
        else if (arg == "--transport-temperature") config.transportTemperature = std::stold(value());
        else if (arg == "--transport-cycle") config.transportCycle = std::stoi(value());
        else if (arg == "--transport-fixed-machines") config.transportFixedMachines = true;
        else if (arg == "--transport-lns") config.transportLns = true;
        else if (arg == "--transport-destroy") config.transportDestroy = std::stoi(value());
        else if (arg == "--transport-repair") config.transportRepair = std::stoi(value());
        else if (arg == "--transport-start") config.transportStart = value();
        else if (arg == "--transport-start-weight") config.transportStartWeight = std::stold(value());
        else if (arg == "--transport-assignment") config.transportAssignment = true;
        else if (arg == "--transport-jobs") config.transportJobs = true;
        else if (arg == "--transport-rebuild") config.transportRebuild = std::stoi(value());
        else if (arg == "--transport-spt") config.transportSpt = true;
        else if (arg == "--transport-window") config.transportWindow = std::stoi(value());
        else if (arg == "--transport-profile") config.transportProfile = std::stoi(value());
        else if (arg == "--transport-request-scan") config.transportRequestScan = true;
        else if (arg == "--transport-fractional") config.transportFractional = true;
        else if (arg == "--transport-seed-bias") config.transportSeedBias = std::stoi(value());
        else if (arg == "--transport-seed-batch") config.transportSeedBatch = std::stoi(value());
        else if (arg == "--selector") config.selector = value();
        else if (arg == "--strategy") { strategy = value(); hasStrategy = true; }
        else if (arg == "--seed") { seed = std::stoull(value()); hasSeed = true; }
        else if (arg == "--max-evals") { maxEvals = std::stoi(value()); hasMaxEvals = true; }
        else if (arg == "--max-neighbors") { maxNeighbors = std::stoi(value()); hasMaxNeighbors = true; }
        else if (arg == "--phase-global") {
            std::string v = value();
            if (v == "auto") phaseGlobal = -1;
            else phaseGlobal = std::stoi(v) == 0 ? 0 : 1;
            hasPhaseGlobal = true;
        }
        else if (arg == "--phase-lookahead") { phaseLookahead = std::stoi(value()) == 0 ? 0 : 1; hasPhaseLookahead = true; }
        else if (arg == "--phase-outbound-only") { phaseOutboundOnly = std::stoi(value()) == 0 ? 0 : 1; hasPhaseOutboundOnly = true; }
        else if (arg == "--phase-hard-unblock") { phaseHardUnblock = std::stoi(value()) == 0 ? 0 : 1; hasPhaseHardUnblock = true; }
        else if (arg == "--policy-file") config.policyFile = value();
        else if (arg == "--self-test") config.selfTest = true;
        else if (arg == "--list-strategies") config.listStrategies = true;
        else if (arg == "--dispatch-ablation") config.dispatchAblation = true;
        else if (arg == "--help" || arg == "-h") config.help = true;
        else throw std::runtime_error("argumento desconocido: " + arg);
    }

    std::string selector = config.selector;
    if (selector.empty() && fileExists("selector.conf")) selector = "selector.conf";
    if (selector.empty() && fileExists("tools/selector.conf")) selector = "tools/selector.conf";
    if (!selector.empty()) loadSelector(selector, config);

    if (hasStrategy) config.strategy = strategy;
    if (hasSeed) config.seed = seed;
    if (hasMaxEvals) config.maxEvals = maxEvals;
    if (hasMaxNeighbors) config.maxNeighbors = maxNeighbors;
    if (hasPhaseGlobal) config.phaseGlobal = phaseGlobal;
    if (hasPhaseLookahead) config.phaseLookahead = phaseLookahead;
    if (hasPhaseOutboundOnly) config.phaseOutboundOnly = phaseOutboundOnly;
    if (hasPhaseHardUnblock) config.phaseHardUnblock = phaseHardUnblock;
    if (!config.policyFile.empty()) {
        std::ifstream file(config.policyFile);
        std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        for (char& c : text) if (c == ',') c = ' ';
        std::stringstream stream(text);
        for (int i = 0; i < 9; ++i) {
            double value = 0;
            if (!(stream >> value)) break;
            config.dispatchWeights[i] = value;
        }
    }
    return config;
}

Instance singleInstance(long double quantity, long double speed) {
    Instance in;
    in.n = 1;
    in.m = 1;
    in.K = 1;
    in.C = 1;
    in.locations = 4;
    in.lots = {{1, 'A', quantity}};
    in.speed = {{speed}};
    in.alpha = {1};
    in.beta = {2};
    in.o = 0;
    in.f = 3;
    in.tau = {
        {0, 5, 5, 10},
        {5, 0, 2, 10},
        {5, 2, 0, 7},
        {10, 10, 7, 0}
    };
    return in;
}

bool testSingle() {
    Instance in = singleInstance(10, 1);
    Candidate candidate;
    candidate.sequence = {{0}};
    Result result = simulate(in, candidate, 1);
    if (!result.feasible) return false;
    if (std::fabs(result.mean - 24) > 1e-4L) return false;
    if (std::fabs(result.completion[0] - 24) > 1e-4L) return false;
    if (std::fabs(result.processStart[0] - 5) > 1e-4L) return false;
    if (std::fabs(result.processEnd[0] - 15) > 1e-4L) return false;

    Instance fractional = singleInstance(5, 2);
    Result other = simulate(fractional, candidate, 1);
    return other.feasible && std::fabs(other.mean - 16.5L) < 1e-4L;
}

bool testBlocking() {
    Instance in;
    in.n = 3;
    in.m = 1;
    in.K = 1;
    in.C = 1;
    in.locations = 4;
    in.lots = {{1, 'A', 10}, {2, 'A', 10}, {3, 'A', 10}};
    in.speed = {{1}, {1}, {1}};
    in.alpha = {1};
    in.beta = {2};
    in.o = 0;
    in.f = 3;
    in.tau = {
        {0, 5, 5, 10},
        {5, 0, 100, 100},
        {5, 100, 0, 100},
        {10, 100, 100, 0}
    };
    Candidate candidate;
    candidate.sequence = {{0, 1, 2}};
    Result result = simulate(in, candidate, 1);
    return result.feasible && result.deliveries == 3 &&
           result.blockedCount > 0 && result.blockedTime > 0;
}

bool testSmoke() {
    std::string directory;
    if (fileExists("instances/instancia_01.io1")) directory = "instances/";
    else if (fileExists("../instances/instancia_01.io1")) directory = "../instances/";
    else return false;

    for (int i = 1; i <= 10; ++i) {
        std::string name = "instancia_";
        if (i < 10) name += "0";
        name += std::to_string(i) + ".io1";
        Instance in = loadInstance(directory + name);
        Candidate candidate = buildOrdered(in, orderByHeight(in), Assign::Score);
        std::vector<int> limits{1};
        if (in.C > 1) limits.push_back(in.C);
        for (int limit : limits) {
            Result result = simulate(in, candidate, limit);
            if (!result.feasible || result.deliveries != in.n ||
                !std::isfinite(result.mean)) {
                return false;
            }
        }
    }
    return true;
}

bool selfTest() {
    return testSingle() && testBlocking() && testSmoke();
}

void writeCsv(const Instance& in, const Outcome& outcome, const std::string& path) {
    std::vector<int> machineOf(in.n, -1);
    for (int j = 0; j < in.m; ++j) {
        for (int lot : outcome.candidate.sequence[j]) machineOf[lot] = j;
    }
    std::ofstream file(path);
    file << "lot_id,machine_id,process_start,process_end,delivered_at\n";
    file << std::fixed << std::setprecision(6);
    for (int i = 0; i < in.n; ++i) {
        file << in.lots[i].id << ',' << machineOf[i] + 1 << ','
             << outcome.result.processStart[i] << ','
             << outcome.result.processEnd[i] << ','
             << outcome.result.completion[i] << '\n';
    }
}

Candidate loadTxtCandidate(const Instance& in, const std::string& path) {
    struct Process {
        long double start;
        int order;
        int lot;
    };
    std::vector<std::vector<Process>> processes(in.m);
    std::ifstream file(path);
    std::string line;
    std::getline(file, line);
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::stringstream stream(line);
        std::vector<std::string> fields;
        std::string field;
        while (std::getline(stream, field, ';')) fields.push_back(field);
        if (fields.size() != 7 || fields[2] != "PROCESS") continue;
        int machine = std::stoi(fields[0].substr(1)) - 1;
        int order = std::stoi(fields[1]);
        int lot = std::stoi(fields[3]) - 1;
        long double start = std::stold(fields[6]);
        processes[machine].push_back({start, order, lot});
    }
    Candidate candidate;
    candidate.sequence.resize(in.m);
    for (int j = 0; j < in.m; ++j) {
        std::sort(processes[j].begin(), processes[j].end(),
                  [](const Process& a, const Process& b) {
                      if (a.start != b.start) return a.start < b.start;
                      return a.order < b.order;
                  });
        for (const Process& process : processes[j]) {
            candidate.sequence[j].push_back(process.lot);
        }
    }
    return candidate;
}

std::string txtLocation(const Instance& in, int index) {
    if (index == in.o) return "o";
    if (index == in.f) return "f";
    for (int j = 0; j < in.m; ++j) {
        if (index == in.alpha[j]) return "a" + std::to_string(j + 1);
    }
    for (int j = 0; j < in.m; ++j) {
        if (index == in.beta[j]) return "b" + std::to_string(j + 1);
    }
    return in.locationNames[index];
}

void writeTxt(const Instance& in, const Outcome& outcome, const std::string& path) {
    const auto& log = outcome.result.log;
    std::vector<int> orders(log.size(), 0);
    std::vector<int> machineOrders(in.m, 0);
    std::vector<int> agvOrders(in.K, 0);
    for (std::size_t i = 0; i < log.size(); ++i) {
        if (log[i].type == LogType::Process) {
            orders[i] = ++machineOrders[log[i].resource];
        } else {
            orders[i] = ++agvOrders[log[i].resource];
        }
    }

    std::ofstream file(path);
    file << "recurso;orden;accion;lote;ubicacion;destino;inicio\n";
    file << std::fixed << std::setprecision(12);
    for (int j = 0; j < in.m; ++j) {
        for (std::size_t i = 0; i < log.size(); ++i) {
            const LogEvent& event = log[i];
            if (event.type != LogType::Process || event.resource != j) continue;
            file << "M" << j + 1 << ';' << orders[i] << ";PROCESS;"
                 << in.lots[event.lot].id << ";;;" << event.start << '\n';
        }
    }
    for (int k = 0; k < in.K; ++k) {
        for (std::size_t i = 0; i < log.size(); ++i) {
            const LogEvent& event = log[i];
            if (event.type == LogType::Process || event.resource != k) continue;
            file << "AGV" << k + 1 << ';' << orders[i] << ';';
            if (event.type == LogType::Move) {
                file << "MOVE;;" << txtLocation(in, event.from) << ';'
                     << txtLocation(in, event.to) << ';' << event.start << '\n';
            } else if (event.type == LogType::Pickup) {
                file << "PICKUP;" << in.lots[event.lot].id << ';'
                     << txtLocation(in, event.from) << ";;" << event.start << '\n';
            } else {
                file << "DELIVER;" << in.lots[event.lot].id << ';'
                     << txtLocation(in, event.from) << ";;" << event.start << '\n';
            }
        }
    }
}

void runDispatchAblation(const Instance& in, const Config& config) {
    Options freezeOptions{config.seed, 5000, config.maxNeighbors, 0, 0};
    Outcome frozen = solvePhaseBucketMode(in, freezeOptions);
    std::vector<std::pair<std::string, Candidate>> modes;
    Candidate base = frozen.candidate;
    base.phaseAware = true;
    base.mixedRoutes = false;
    base.phaseLookahead = false;
    base.phaseBias = frozen.candidate.phaseBias;
    base.phaseGlobal = false;
    base.phaseEnumerated = false;
    base.phaseJoint = false;
    Candidate g0 = base;
    modes.push_back({"G0", g0});
    Candidate g1 = base;
    g1.phaseGlobal = true;
    modes.push_back({"G1", g1});
    Candidate r = base;
    r.phaseEnumerated = true;
    modes.push_back({"R", r});
    Candidate j = r;
    j.phaseJoint = true;
    modes.push_back({"J", j});
    for (const auto& entry : modes) {
        auto begin = std::chrono::steady_clock::now();
        Result result = simulate(in, entry.second, frozen.batchLimit, false);
        auto end = std::chrono::steady_clock::now();
        double cpu = std::chrono::duration<double, std::milli>(end - begin).count();
        long double starved = std::accumulate(result.machineStarvedByMachine.begin(),
                                              result.machineStarvedByMachine.end(), 0.0L);
        std::cout << "ABLATION instance=" << stem(config.input)
                  << " seed=" << config.seed
                  << " mode=" << entry.first
                  << " mean=" << std::fixed << std::setprecision(6) << result.mean
                  << " feasible=" << (result.feasible ? 1 : 0)
                  << " batch_limit=" << frozen.batchLimit
                  << " blocked=" << result.blockedTime
                  << " empty=" << result.emptyTravel
                  << " loaded=" << result.loadedTravel
                  << " in_trips=" << result.phaseInputTrips
                  << " out_trips=" << result.phaseOutputTrips
                  << " in_lots=" << result.phaseInputLots
                  << " out_lots=" << result.phaseOutputLots
                  << " output_fill=" << (result.phaseOutputTrips > 0
                        ? static_cast<long double>(result.phaseOutputLots) /
                          (result.phaseOutputTrips * in.C) : 0)
                  << " machine_starved=" << starved
                  << " multi_free_pct=" << (result.dispatchEpochCount > 0
                        ? 100.0L * result.multiFreeEpochCount / result.dispatchEpochCount : 0)
                  << " candidate_routes=" << result.candidateRoutes
                  << " dfs_nodes=" << result.dfsNodes
                  << " joint_choices=" << result.jointChoicesUsed
                  << " cpu_ms=" << cpu
                  << '\n';
    }
}

std::string debugLocation(const Instance& in, int index) {
    if (index == in.o) return "o";
    if (index == in.f) return "f";
    for (int j = 0; j < in.m; ++j) {
        if (index == in.alpha[j]) return "a" + std::to_string(j + 1);
    }
    for (int j = 0; j < in.m; ++j) {
        if (index == in.beta[j]) return "b" + std::to_string(j + 1);
    }
    return in.locationNames[index];
}

std::string debugNumber(long double value) {
    if (!std::isfinite(value)) return "NA";
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(6) << value;
    return stream.str();
}

long double debugPercentile(std::vector<long double> values, long double q) {
    if (values.empty()) return 0;
    std::sort(values.begin(), values.end());
    long double position = q * (values.size() - 1);
    std::size_t low = static_cast<std::size_t>(std::floor(position));
    std::size_t high = static_cast<std::size_t>(std::ceil(position));
    if (low == high) return values[low];
    long double weight = position - low;
    return values[low] * (1 - weight) + values[high] * weight;
}

void writeDebugReport(const Instance& in, const Outcome& outcome, const std::string& directory) {
    std::filesystem::create_directories(directory);
    const Result& result = outcome.result;
    const bool dispatchMetrics = outcome.strategy != "transport-search";
    std::vector<int> machineOf(in.n, -1);
    std::vector<int> positionOf(in.n, -1);
    for (int j = 0; j < in.m; ++j) {
        for (std::size_t p = 0; p < outcome.candidate.sequence[j].size(); ++p) {
            int lot = outcome.candidate.sequence[j][p];
            machineOf[lot] = j;
            positionOf[lot] = static_cast<int>(p);
        }
    }
    std::vector<long double> completions;
    for (long double value : result.completion)
        if (std::isfinite(value)) completions.push_back(value);
    long double sum = 0;
    for (long double value : completions) sum += value;
    long double machineBusy = 0;
    std::vector<std::vector<long double>> starts(in.m), ends(in.m);
    for (int lot = 0; lot < in.n; ++lot) {
        int j = machineOf[lot];
        if (j < 0) continue;
        starts[j].push_back(result.processStart[lot]);
        ends[j].push_back(result.processEnd[lot]);
        machineBusy += result.processEnd[lot] - result.processStart[lot];
    }
    long double starved = 0;
    for (long double value : result.machineStarvedByMachine) starved += value;
    long double loaded = 0, empty = 0;
    for (long double value : result.loadedTravelByAgv) loaded += value;
    for (long double value : result.emptyTravelByAgv) empty += value;
    long double makespan = result.makespan;
    {
        std::ofstream file(std::filesystem::path(directory) / "summary.csv");
        file << "key,value\n"
             << "strategy," << outcome.strategy << '\n'
             << "dispatch_metrics_available," << (dispatchMetrics ? 1 : 0) << '\n'
             << "feasible," << (result.feasible ? 1 : 0) << '\n'
             << "n," << in.n << "\nm," << in.m << "\nK," << in.K << "\nC," << in.C << '\n'
             << "evals," << outcome.evals << '\n'
             << "batch_limit," << outcome.batchLimit << '\n'
             << "mean," << debugNumber(result.mean) << '\n'
             << "sum_completion," << debugNumber(sum) << '\n'
             << "makespan," << debugNumber(makespan) << '\n'
             << "min_completion," << debugNumber(completions.empty() ? 0 : *std::min_element(completions.begin(), completions.end())) << '\n'
             << "p50_completion," << debugNumber(debugPercentile(completions, 0.50L)) << '\n'
             << "p90_completion," << debugNumber(debugPercentile(completions, 0.90L)) << '\n'
             << "p95_completion," << debugNumber(debugPercentile(completions, 0.95L)) << '\n'
             << "p99_completion," << debugNumber(debugPercentile(completions, 0.99L)) << '\n'
             << "max_completion," << debugNumber(completions.empty() ? 0 : *std::max_element(completions.begin(), completions.end())) << '\n'
             << "machine_busy," << debugNumber(machineBusy) << '\n'
             << "machine_idle," << debugNumber(makespan * in.m - machineBusy) << '\n'
             << "machine_util_pct," << debugNumber(makespan > 0 ? 100 * machineBusy / (makespan * in.m) : 0) << '\n'
             << "machine_starved," << debugNumber(starved) << '\n'
             << "blocked_time," << debugNumber(result.blockedTime) << '\n'
             << "blocked_count," << result.blockedCount << '\n'
             << "loaded_travel," << debugNumber(loaded) << '\n'
             << "empty_travel," << debugNumber(empty) << '\n'
             << "empty_share_pct," << debugNumber(loaded + empty > 0 ? 100 * empty / (loaded + empty) : 0) << '\n'
             << "agv_util_pct," << debugNumber(makespan > 0 ? 100 * (loaded + empty) / (makespan * in.K) : 0) << '\n'
             << "input_trips," << (dispatchMetrics ? std::to_string(result.phaseInputTrips) : "") << '\n'
             << "output_trips," << (dispatchMetrics ? std::to_string(result.phaseOutputTrips) : "") << '\n'
             << "input_lots," << (dispatchMetrics ? std::to_string(result.phaseInputLots) : std::to_string(in.n)) << '\n'
             << "output_lots," << (dispatchMetrics ? std::to_string(result.phaseOutputLots) : std::to_string(in.n)) << '\n'
             << "input_fill," << (dispatchMetrics ? debugNumber(result.phaseInputTrips > 0 ? static_cast<long double>(result.phaseInputLots) / (result.phaseInputTrips * in.C) : 0) : "") << '\n'
             << "output_fill," << (dispatchMetrics ? debugNumber(result.phaseOutputTrips > 0 ? static_cast<long double>(result.phaseOutputLots) / (result.phaseOutputTrips * in.C) : 0) : "") << '\n'
             << "dispatch_epochs," << result.dispatchEpochCount << '\n'
             << "multi_free_epochs," << result.multiFreeEpochCount << '\n'
             << "max_free," << result.maxSimultaneouslyFree << '\n'
             << "candidate_routes," << result.candidateRoutes << '\n'
             << "dfs_nodes," << result.dfsNodes << '\n'
             << "joint_choices," << result.jointChoicesUsed << '\n'
             << "learned_choices," << result.learnedChoiceCount << '\n'
             << "learned_changes," << result.learnedChoiceChanges << '\n';
    }
    {
        std::ofstream file(std::filesystem::path(directory) / "lots.csv");
        file << "lot_id,type,quantity,machine_id,sequence_position,inbound_pickup,alpha_delivery,"
                "process_start,process_end,beta_pickup,delivery,alpha_transport,alpha_wait,"
                "process_duration,beta_wait,outbound_time,total_flow\n";
        for (int lot = 0; lot < in.n; ++lot) {
            long double pStart = result.processStart[lot];
            long double pEnd = result.processEnd[lot];
            long double delivery = result.completion[lot];
            long double alpha = result.alphaDelivery[lot];
            long double beta = result.betaPickup[lot];
            long double inbound = result.inboundPickup[lot];
            file << in.lots[lot].id << ',' << in.lots[lot].type << ',' << debugNumber(in.lots[lot].quantity)
                 << ',' << machineOf[lot] + 1 << ',' << positionOf[lot] << ','
                 << debugNumber(inbound) << ',' << debugNumber(alpha) << ','
                 << debugNumber(pStart) << ',' << debugNumber(pEnd) << ','
                 << debugNumber(beta) << ',' << debugNumber(delivery) << ','
                 << debugNumber(alpha - inbound) << ',' << debugNumber(pStart - alpha) << ','
                 << debugNumber(pEnd - pStart) << ',' << debugNumber(beta - pEnd) << ','
                 << debugNumber(delivery - beta) << ',' << debugNumber(delivery) << '\n';
        }
    }
    {
        std::ofstream file(std::filesystem::path(directory) / "machines.csv");
        file << "machine_id,lots,busy,idle,util_pct,initial_idle,gaps,blocked,starved,tail,"
                "process_count,first_start,last_end\n";
        for (int j = 0; j < in.m; ++j) {
            std::vector<std::pair<long double, long double>> intervals;
            for (std::size_t p = 0; p < starts[j].size(); ++p) intervals.push_back({starts[j][p], ends[j][p]});
            std::sort(intervals.begin(), intervals.end());
            long double busy = 0, gaps = 0;
            for (std::size_t p = 0; p < intervals.size(); ++p) {
                busy += intervals[p].second - intervals[p].first;
                if (p > 0) gaps += std::max(0.0L, intervals[p].first - intervals[p - 1].second);
            }
            long double first = intervals.empty() ? 0 : intervals.front().first;
            long double last = intervals.empty() ? 0 : intervals.back().second;
            long double tail = makespan - last;
            file << j + 1 << ',' << intervals.size() << ',' << debugNumber(busy) << ','
                 << debugNumber(makespan - busy) << ','
                 << debugNumber(makespan > 0 ? 100 * busy / makespan : 0) << ','
                 << debugNumber(first) << ',' << debugNumber(gaps) << ','
                 << debugNumber(result.blockedByMachine[j]) << ','
                 << debugNumber(result.machineStarvedByMachine[j]) << ','
                 << debugNumber(tail) << ',' << intervals.size() << ','
                 << debugNumber(first) << ',' << debugNumber(last) << '\n';
        }
    }
    {
        std::ofstream file(std::filesystem::path(directory) / "agvs.csv");
        file << "agv_id,loaded_travel,empty_travel,travel,idle_time,travel_util_pct,"
                "empty_share_pct,input_trips,output_trips,input_lots,output_lots,"
                "avg_input_batch,avg_output_batch\n";
        for (int k = 0; k < in.K; ++k) {
            long double l = result.loadedTravelByAgv[k];
            long double e = result.emptyTravelByAgv[k];
            int inTrips = result.inputTripsByAgv[k];
            int outTrips = result.outputTripsByAgv[k];
            file << k + 1 << ',' << debugNumber(l) << ',' << debugNumber(e) << ','
                 << debugNumber(l + e) << ',' << debugNumber(result.idleVehicleTimeByAgv[k]) << ','
                 << debugNumber(makespan > 0 ? 100 * (l + e) / makespan : 0) << ','
                 << debugNumber(l + e > 0 ? 100 * e / (l + e) : 0) << ','
                 << (inTrips >= 0 ? std::to_string(inTrips) : "") << ',' << (outTrips >= 0 ? std::to_string(outTrips) : "") << ','
                 << result.inputLotsByAgv[k] << ',' << result.outputLotsByAgv[k] << ','
                 << (inTrips >= 0 ? debugNumber(inTrips > 0 ? static_cast<long double>(result.inputLotsByAgv[k]) / inTrips : 0) : "") << ','
                 << (outTrips >= 0 ? debugNumber(outTrips > 0 ? static_cast<long double>(result.outputLotsByAgv[k]) / outTrips : 0) : "") << '\n';
        }
    }
    {
        std::ofstream file(std::filesystem::path(directory) / "events.csv");
        file << "type,resource,resource_index,lot,from_location,to_location,start\n";
        for (const LogEvent& event : result.log) {
            std::string type;
            std::string resource;
            int resourceIndex = 0;
            if (event.type == LogType::Process) { type = "PROCESS"; resource = "M"; resourceIndex = event.resource + 1; }
            else { type = event.type == LogType::Pickup ? "PICKUP" : event.type == LogType::Deliver ? "DELIVER" : "MOVE"; resource = "AGV"; resourceIndex = event.resource + 1; }
            file << type << ',' << resource << ',' << resourceIndex << ','
                 << (event.lot >= 0 ? std::to_string(in.lots[event.lot].id) : "") << ','
                 << (event.from >= 0 ? debugLocation(in, event.from) : "") << ','
                 << (event.to >= 0 ? debugLocation(in, event.to) : "") << ','
                 << debugNumber(event.start) << '\n';
        }
    }
}

void printUsage() {
    std::cout << "uso: io1 --input <archivo.io1> [--out <resultado.csv>] [--txt <envio.txt>]\n"
              << "       [--from-txt <envio_previo.txt>] [--selector <archivo>]\n"
              << "       [--strategy <nombre>] [--seed <entero>] [--max-evals <entero>]\n"
              << "       [--max-neighbors <entero>] [--phase-global <auto|0|1>] [--phase-lookahead <0|1>]\n"
              << "       [--phase-outbound-only <0|1>] [--phase-hard-unblock <0|1>] [--policy-file <archivo>]\n"
              << "       [--debug-dir <directorio>]\n"
              << "       [--transport-trace <csv>] [--transport-temperature <real>] [--transport-cycle <entero>]\n"
              << "       [--transport-fixed-machines]\n"
              << "       [--transport-lns] [--transport-destroy <entero>] [--transport-repair <entero>]\n"
              << "       [--transport-start <phase|slots>] [--transport-start-weight <real>]\n"
              << "       [--transport-assignment]\n"
              << "       [--transport-jobs]\n"
              << "       [--transport-rebuild <0..6>]\n"
              << "       [--transport-spt]\n"
              << "       [--transport-window <0..16>]\n"
              << "       [--transport-profile <0|1|2>]\n"
              << "       [--transport-request-scan]\n"
              << "       [--transport-fractional]\n"
              << "       [--transport-seed-bias <-5|-2|0|2|5>] [--transport-seed-batch <entero>]\n"
              << "       [--self-test] [--list-strategies] [--dispatch-ablation]\n";
}

int main(int argc, char** argv) {
    try {
        Config config = parseArguments(argc, argv);
        if (config.help) {
            printUsage();
            return 0;
        }
        if (config.listStrategies) {
            for (const char* name : {"b1", "b2", "b3", "s0", "s1", "s2",
                                     "vnd", "vnd-s0", "vnd-s1", "vnd-s2", "integrated", "alns-joint", "phase-bucket", "phase-joint", "phase-learned", "transport-search"}) {
                std::cout << name << '\n';
            }
            return 0;
        }
        if (config.dispatchAblation) {
            if (config.input.empty()) {
                printUsage();
                return 1;
            }
            Instance in = loadInstance(config.input);
            runDispatchAblation(in, config);
            return 0;
        }
        if (config.selfTest) {
            if (!selfTest()) {
                std::cout << "SELF_TEST_FAIL\n";
                return 1;
            }
            std::cout << "SELF_TEST_OK\n";
            return 0;
        }
        if (config.input.empty()) {
            printUsage();
            return 1;
        }

        Strategy strategy = Strategy::PhaseBucket;
        if (!strategyFromName(config.strategy, strategy)) {
            std::cerr << "estrategia desconocida: " << config.strategy << '\n';
            return 1;
        }

        Instance in = loadInstance(config.input);
        Options options{config.seed, config.maxEvals, config.maxNeighbors, config.phaseGlobal, config.phaseLookahead, config.phaseOutboundOnly, config.phaseHardUnblock};
        options.learnedPolicy = config.strategy == "phase-learned";
        options.dispatchWeights = config.dispatchWeights;
        auto begin = std::chrono::steady_clock::now();
        Outcome outcome;
        if (config.strategy == "transport-search") {
            TransportPlan start;
            long long seedEvals = 0;
            if (!config.fromTxt.empty()) start = loadTransportPlan(in, config.fromTxt);
            else {
                Options initial = options;
                initial.maxEvals = std::max(1, options.maxEvals / (options.phaseGlobal < 0 ? 10 : 5));
                Outcome seed;
                if (config.transportStart == "phase") seed = solvePhaseBucket(in, initial);
                else if (config.transportStart == "slots")
                    seed = slotTransportSeed(in, options, config.transportStartWeight,
                                             config.transportSeedBias, config.transportSeedBatch);
                else throw std::runtime_error("inicio de transporte desconocido");
                start = transportPlan(in, seed);
                seedEvals = seed.evals;
            }
            Options remaining = options;
            if (config.transportSpt) sortTransportLots(in, start);
            if (config.transportRebuild != 0) {
                if (config.transportRebuild < 1 || config.transportRebuild > 6) throw std::runtime_error("reconstruccion desconocida");
                int lag = config.transportRebuild >= 5 ? 0 : (config.transportRebuild - 1) % 2 + 1;
                bool global = config.transportRebuild == 3 || config.transportRebuild == 4 || config.transportRebuild == 6;
                if (config.transportProfile < 0 || config.transportProfile > 2) throw std::runtime_error("perfil de transporte desconocido");
                start = buildMilkPlan(in, start.machines, lag, config.seed, global, config.transportProfile);
            }
            remaining.maxEvals = std::max(1, options.maxEvals - static_cast<int>(seedEvals));
            std::ofstream trace;
            if (!config.transportTrace.empty()) {
                trace.open(config.transportTrace);
                if (!trace) throw std::runtime_error("no se pudo abrir " + config.transportTrace);
            }
            TransportSearchConfig transport;
            transport.temperature = config.transportTemperature;
            transport.cycle = config.transportCycle;
            transport.trace = trace.is_open() ? &trace : nullptr;
            transport.fixedMachines = config.transportFixedMachines;
            transport.lns = config.transportLns;
            transport.destroySize = config.transportDestroy;
            transport.repairWidth = config.transportRepair;
            transport.assignment = config.transportAssignment;
            transport.jobs = config.transportJobs;
            transport.window = config.transportWindow;
            transport.requestScan = config.transportRequestScan;
            transport.fractional = config.transportFractional;
            outcome = solveTransport(in, std::move(start), remaining, transport);
            outcome.evals += seedEvals;
        } else if (!config.fromTxt.empty()) {
            Candidate start = loadTxtCandidate(in, config.fromTxt);
            std::mt19937_64 rng(config.seed);
            SearchResult search = searchFromStart(in, start, options, rng);
            outcome.candidate = search.candidate;
            outcome.result = search.result;
            outcome.batchLimit = search.batchLimit;
            outcome.evals = search.evals;
            outcome.strategy = "warm";
        } else {
            outcome = solve(in, strategy, options);
        }
        auto end = std::chrono::steady_clock::now();
        double milliseconds = std::chrono::duration<double, std::milli>(end - begin).count();

        if (outcome.result.feasible && !config.output.empty()) {
            writeCsv(in, outcome, config.output);
        }
        if (outcome.result.feasible && !config.txt.empty()) {
            writeTxt(in, outcome, config.txt);
        }
        if (outcome.result.feasible && !config.debugDir.empty()) {
            writeDebugReport(in, outcome, config.debugDir);
        }

        std::cout << "RESULT instance=" << stem(config.input)
                  << " strategy=" << outcome.strategy
                  << " batch_limit=" << outcome.batchLimit
                  << " feasible=" << (outcome.result.feasible ? 1 : 0)
                  << " mean=" << std::fixed << std::setprecision(6)
                  << (outcome.result.feasible ? outcome.result.mean : -1)
                  << " evals=" << outcome.evals
                  << " cpu_ms=" << milliseconds
                  << " blocked_time=" << outcome.result.blockedTime
                  << " idle_time=" << outcome.result.machineIdleTime
                  << " empty_travel=" << outcome.result.emptyTravel
                  << " loaded_travel=" << outcome.result.loadedTravel;
        if (outcome.strategy != "transport-search") {
            std::cout << " phase_input_trips=" << outcome.result.phaseInputTrips
                  << " phase_output_trips=" << outcome.result.phaseOutputTrips
                  << " phase_input_lots=" << outcome.result.phaseInputLots
                  << " phase_output_lots=" << outcome.result.phaseOutputLots
                  << " output_fill=" << (outcome.result.phaseOutputTrips > 0
                        ? static_cast<long double>(outcome.result.phaseOutputLots) /
                          (outcome.result.phaseOutputTrips * in.C) : 0)
                  << " machine_starved=" << std::accumulate(
                        outcome.result.machineStarvedByMachine.begin(),
                        outcome.result.machineStarvedByMachine.end(), 0.0L)
                  << " dispatch_epochs=" << outcome.result.dispatchEpochCount
                  << " multi_free_epochs=" << outcome.result.multiFreeEpochCount
                  << " multi_free_pct=" << (outcome.result.dispatchEpochCount > 0
                        ? 100.0L * outcome.result.multiFreeEpochCount /
                          outcome.result.dispatchEpochCount : 0)
                  << " max_free=" << outcome.result.maxSimultaneouslyFree
                  << " candidate_routes=" << outcome.result.candidateRoutes
                  << " dfs_nodes=" << outcome.result.dfsNodes
                  << " joint_choices=" << outcome.result.jointChoicesUsed;
        }
        std::cout << " makespan=" << outcome.result.makespan
                  << " learned_choices=" << outcome.result.learnedChoiceCount
                  << " learned_changes=" << outcome.result.learnedChoiceChanges
                  << '\n';
        return outcome.result.feasible ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
