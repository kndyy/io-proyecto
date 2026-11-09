#pragma once

#include <algorithm>
#include <fstream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace io1 {

struct Lot {
    int id = 0;
    char type = '?';
    long double quantity = 0;
};

struct Instance {
    int n = 0;
    int m = 0;
    int K = 0;
    int C = 1;
    std::vector<Lot> lots;
    std::vector<std::vector<long double>> speed;
    std::vector<std::vector<long double>> tau;
    std::vector<int> alpha;
    std::vector<int> beta;
    std::vector<std::string> locationNames;
    int o = -1;
    int f = -1;
    int locations = 0;
    long double medianTau = 1;

    bool compatible(int lot, int machine) const {
        return speed[lot][machine] > 0;
    }

    long double duration(int lot, int machine) const {
        return lots[lot].quantity / speed[lot][machine];
    }
};

inline int locationIndex(const std::string& name, int m) {
    if (name == "o") return -2;
    if (name == "f") return -3;
    if (name.rfind("alpha_", 0) == 0) return std::stoi(name.substr(6)) - 1;
    if (name.rfind("beta_", 0) == 0) return m + std::stoi(name.substr(5)) - 1;
    throw std::runtime_error("ubicacion desconocida: " + name);
}

inline Instance loadInstance(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("no se pudo abrir " + path);

    Instance data;
    std::string token;
    in >> token;
    if (token != "IO1_V1") throw std::runtime_error("cabecera IO1_V1 requerida");
    in >> data.n >> data.m >> data.K >> data.C;

    in >> token;
    if (token != "LOTS") throw std::runtime_error("seccion LOTS requerida");
    data.lots.resize(data.n);
    for (int i = 0; i < data.n; ++i) {
        int id;
        char type;
        long double quantity;
        in >> id >> type >> quantity;
        data.lots[id - 1] = {id, type, quantity};
    }

    in >> token;
    if (token != "RATES") throw std::runtime_error("seccion RATES requerida");
    int rates;
    in >> rates;
    std::vector<std::map<char, long double>> machineRates(data.m);
    for (int i = 0; i < rates; ++i) {
        int machine;
        char type;
        long double value;
        in >> machine >> type >> value;
        machineRates[machine - 1][type] = value;
    }
    data.speed.assign(data.n, std::vector<long double>(data.m, 0));
    for (int i = 0; i < data.n; ++i) {
        for (int j = 0; j < data.m; ++j) {
            auto it = machineRates[j].find(data.lots[i].type);
            if (it != machineRates[j].end()) data.speed[i][j] = it->second;
        }
    }

    in >> token;
    if (token != "LOCATIONS") throw std::runtime_error("seccion LOCATIONS requerida");
    in >> data.locations;
    data.alpha.assign(data.m, -1);
    data.beta.assign(data.m, -1);
    data.locationNames.resize(data.locations);
    for (int i = 0; i < data.locations; ++i) {
        std::string name;
        in >> name;
        data.locationNames[i] = name;
        int index = locationIndex(name, data.m);
        if (index == -2) data.o = i;
        else if (index == -3) data.f = i;
        else if (index < data.m) data.alpha[index] = i;
        else data.beta[index - data.m] = i;
    }

    in >> token;
    if (token != "TIMES") throw std::runtime_error("seccion TIMES requerida");
    data.tau.assign(data.locations, std::vector<long double>(data.locations, 0));
    for (int i = 0; i < data.locations; ++i) {
        for (int j = 0; j < data.locations; ++j) {
            in >> data.tau[i][j];
        }
    }
    std::vector<long double> positive;
    for (const auto& row : data.tau) {
        for (long double value : row) {
            if (value > 0) positive.push_back(value);
        }
    }
    if (!positive.empty()) {
        std::sort(positive.begin(), positive.end());
        std::size_t middle = positive.size() / 2;
        data.medianTau = positive.size() % 2 == 0
            ? (positive[middle - 1] + positive[middle]) / 2
            : positive[middle];
    }
    return data;
}

inline long double lotHeight(const Instance& in, int lot) {
    long double best = std::numeric_limits<long double>::infinity();
    for (int j = 0; j < in.m; ++j) {
        if (!in.compatible(lot, j)) continue;
        long double value = in.duration(lot, j) + in.tau[in.o][in.alpha[j]] + in.tau[in.beta[j]][in.f];
        if (value < best) best = value;
    }
    return best;
}

inline long double lotFastest(const Instance& in, int lot) {
    long double best = std::numeric_limits<long double>::infinity();
    for (int j = 0; j < in.m; ++j) {
        if (in.compatible(lot, j)) best = std::min(best, in.duration(lot, j));
    }
    return best;
}

}
