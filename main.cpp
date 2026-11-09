#include "heuristic.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace io1;

struct Config {
    std::string input;
    std::string output;
    std::string txt;
    std::string fromTxt;
    std::string selector;
    std::string strategy = "vnd";
    std::uint64_t seed = 20261106;
    int maxEvals = 1000;
    int maxNeighbors = 40;
    bool selfTest = false;
    bool listStrategies = false;
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
    }
}

Config parseArguments(int argc, char** argv) {
    Config config;
    std::string strategy;
    bool hasStrategy = false;
    bool hasSeed = false;
    bool hasMaxEvals = false;
    bool hasMaxNeighbors = false;
    std::uint64_t seed = 0;
    int maxEvals = 0;
    int maxNeighbors = 0;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto value = [&]() {
            if (i + 1 >= argc) throw std::runtime_error("falta valor para " + arg);
            return std::string(argv[++i]);
        };
        if (arg == "--input") config.input = value();
        else if (arg == "--out") config.output = value();
        else if (arg == "--txt") config.txt = value();
        else if (arg == "--from-txt") config.fromTxt = value();
        else if (arg == "--selector") config.selector = value();
        else if (arg == "--strategy") { strategy = value(); hasStrategy = true; }
        else if (arg == "--seed") { seed = std::stoull(value()); hasSeed = true; }
        else if (arg == "--max-evals") { maxEvals = std::stoi(value()); hasMaxEvals = true; }
        else if (arg == "--max-neighbors") { maxNeighbors = std::stoi(value()); hasMaxNeighbors = true; }
        else if (arg == "--self-test") config.selfTest = true;
        else if (arg == "--list-strategies") config.listStrategies = true;
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

void printUsage() {
    std::cout << "uso: io1 --input <archivo.io1> [--out <resultado.csv>] [--txt <envio.txt>]\n"
              << "       [--from-txt <envio_previo.txt>] [--selector <archivo>]\n"
              << "       [--strategy <nombre>] [--seed <entero>] [--max-evals <entero>]\n"
              << "       [--max-neighbors <entero>] [--self-test] [--list-strategies]\n";
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
                                     "vnd", "vnd-s0", "vnd-s1", "vnd-s2", "integrated", "alns-joint"}) {
                std::cout << name << '\n';
            }
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

        Strategy strategy;
        if (!strategyFromName(config.strategy, strategy)) {
            std::cerr << "estrategia desconocida: " << config.strategy << '\n';
            return 1;
        }

        Instance in = loadInstance(config.input);
        Options options{config.seed, config.maxEvals, config.maxNeighbors};
        auto begin = std::chrono::steady_clock::now();
        Outcome outcome;
        if (!config.fromTxt.empty()) {
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

        std::cout << "RESULT instance=" << stem(config.input)
                  << " strategy=" << outcome.strategy
                  << " batch_limit=" << outcome.batchLimit
                  << " feasible=" << (outcome.result.feasible ? 1 : 0)
                  << " mean=" << std::fixed << std::setprecision(6)
                  << (outcome.result.feasible ? outcome.result.mean : -1)
                  << " evals=" << outcome.evals
                  << " cpu_ms=" << milliseconds << '\n';
        return outcome.result.feasible ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
