#include "heuristic.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace io1;

struct Row {
    int band = -1;
    DispatchSnapshot snapshot;
    int alternativeRank = -1;
    DispatchAction alternative;
    Result result;
    bool overrideSeen = false;
    bool overrideApplied = false;
    bool overrideRejected = false;
    std::string rejection;
    long double baselineMean = 0;
    long double cpuMs = 0;
    long double baselineCpuMs = 0;
};

std::string readSha() {
    std::ifstream head(".git/HEAD");
    std::string line;
    if (!std::getline(head, line)) return "unknown";
    if (line.rfind("ref: ", 0) == 0) {
        std::ifstream ref(".git/" + line.substr(5));
        std::string sha;
        if (std::getline(ref, sha)) return sha;
        return "unknown";
    }
    return line;
}

int parseNumber(const std::string& text) {
    return std::stoi(text);
}

std::string machinesText(const std::vector<int>& machines) {
    std::ostringstream stream;
    for (std::size_t i = 0; i < machines.size(); ++i) {
        if (i) stream << '|';
        stream << machines[i] + 1;
    }
    return stream.str();
}

std::vector<DispatchSnapshot> selectCritical(const DispatchAudit& audit, int maxEvents) {
    std::vector<DispatchSnapshot> eligible;
    long long total = audit.totalActions;
    long long start = total < 25 ? 1 : 5;
    long long end = total < 25 ? total : static_cast<long long>(std::floor(total * 0.9L));
    for (const auto& snapshot : audit.trace) {
        if (snapshot.chosen.index < start || snapshot.chosen.index >= end) continue;
        if (snapshot.alternatives.empty()) continue;
        eligible.push_back(snapshot);
    }
    if (eligible.empty()) return {};
    std::vector<DispatchSnapshot> selected;
    int perBand = std::max(1, maxEvents / 5);
    std::size_t begin = 0;
    for (int band = 0; band < 5 && begin < eligible.size(); ++band) {
        std::size_t finish = std::min(eligible.size(), begin + (eligible.size() - begin) / (5 - band));
        std::vector<DispatchSnapshot> group(eligible.begin() + static_cast<std::ptrdiff_t>(begin),
                                           eligible.begin() + static_cast<std::ptrdiff_t>(finish));
        std::sort(group.begin(), group.end(), [](const DispatchSnapshot& a, const DispatchSnapshot& b) {
            auto key = [](const DispatchSnapshot& s) {
                return std::make_tuple(s.blockedMachines == 0,
                                       !(s.readyOutputs >= 2 && s.waitingInputs >= 1),
                                       s.freeAgvs < 2,
                                       -s.blockedMachines,
                                       -s.readyOutputs,
                                       s.chosen.index);
            };
            return key(a) < key(b);
        });
        for (int i = 0; i < perBand && i < static_cast<int>(group.size()); ++i)
            selected.push_back(group[i]);
        begin = finish;
    }
    return selected;
}

int main(int argc, char** argv) {
    std::string instanceNumber = "09";
    int seed = 20261109;
    int freezeEvals = 5000;
    int mode = 0;
    int lookahead = 0;
    int maxEvents = 20;
    int alts = 3;
    std::string out = "experiments/local/oracle.csv";
    for (int i = 1; i + 1 < argc; i += 2) {
        std::string key = argv[i];
        std::string value = argv[i + 1];
        if (key == "--instance") instanceNumber = value;
        else if (key == "--seed") seed = parseNumber(value);
        else if (key == "--freeze-evals") freezeEvals = parseNumber(value);
        else if (key == "--mode") mode = parseNumber(value);
        else if (key == "--lookahead") lookahead = parseNumber(value);
        else if (key == "--max-events") maxEvents = parseNumber(value);
        else if (key == "--alts") alts = parseNumber(value);
        else if (key == "--out") out = value;
    }
    std::filesystem::create_directories(std::filesystem::path(out).parent_path());
    std::string digits = instanceNumber;
    if (digits.size() == 1) digits = "0" + digits;
    Instance instance = loadInstance("instances/instancia_" + digits + ".io1");
    Options options;
    options.seed = seed;
    options.maxEvals = freezeEvals;
    options.maxNeighbors = 120;
    options.phaseGlobal = mode;
    options.phaseLookahead = lookahead;
    Outcome frozen = solvePhaseBucketMode(instance, options);
    Candidate candidate = frozen.candidate;
    DispatchAudit audit;
    Result baseline = simulate(instance, candidate, frozen.batchLimit, false, &audit);
    Result plain = simulate(instance, candidate, frozen.batchLimit, false);
    bool equal = baseline.feasible == plain.feasible &&
        std::fabs(baseline.mean - plain.mean) <= 1e-9L;
    for (int i = 0; equal && i < instance.n; ++i)
        if (std::fabs(baseline.completion[i] - plain.completion[i]) > 1e-9L) equal = false;
    if (!equal) {
        std::cerr << "trace-only result differs from plain simulation\n";
        return 2;
    }
    std::vector<DispatchSnapshot> selected = selectCritical(audit, maxEvents);
    std::vector<Row> rows;
    std::string sha = readSha();
    for (const auto& snapshot : selected) {
        DispatchAction baselineAction = snapshot.chosen;
        DispatchOverride control;
        control.enabled = true;
        control.targetIndex = baselineAction.index;
        control.agv = baselineAction.agv;
        control.output = baselineAction.output;
        control.machines = baselineAction.machines;
        control.expectedPrefixFingerprint = snapshot.prefixFingerprint;
        DispatchAudit controlAudit;
        auto start = std::chrono::steady_clock::now();
        Result controlResult = simulate(instance, candidate, frozen.batchLimit, false,
                                        &controlAudit, &control);
        auto end = std::chrono::steady_clock::now();
        Row controlRow;
        controlRow.snapshot = snapshot;
        controlRow.alternativeRank = -1;
        controlRow.alternative = baselineAction;
        controlRow.result = controlResult;
        controlRow.overrideSeen = controlAudit.overrideTargetSeen;
        controlRow.overrideApplied = controlAudit.overrideApplied;
        controlRow.overrideRejected = controlAudit.overrideRejected;
        controlRow.rejection = controlAudit.rejectionReason;
        controlRow.baselineMean = baseline.mean;
        controlRow.cpuMs = std::chrono::duration<double, std::milli>(end - start).count();
        controlRow.baselineCpuMs = 0;
        if (!controlRow.overrideApplied || !controlResult.feasible ||
            std::fabs(controlResult.mean - baseline.mean) > 1e-9L) {
            std::cerr << "control override did not reproduce baseline\n";
            return 3;
        }
        rows.push_back(controlRow);
        int limit = std::min<int>(alts, static_cast<int>(snapshot.alternatives.size()));
        for (int rank = 0; rank < limit; ++rank) {
            const DispatchAction& alternative = snapshot.alternatives[rank];
            DispatchOverride override;
            override.enabled = true;
            override.targetIndex = baselineAction.index;
            override.agv = baselineAction.agv;
            override.output = alternative.output;
            override.machines = alternative.machines;
            override.expectedPrefixFingerprint = snapshot.prefixFingerprint;
            DispatchAudit forceAudit;
            auto altStart = std::chrono::steady_clock::now();
            Result result = simulate(instance, candidate, frozen.batchLimit, false,
                                     &forceAudit, &override);
            auto altEnd = std::chrono::steady_clock::now();
            Row row;
            row.snapshot = snapshot;
            row.alternativeRank = rank;
            row.alternative = alternative;
            row.result = result;
            row.overrideSeen = forceAudit.overrideTargetSeen;
            row.overrideApplied = forceAudit.overrideApplied;
            row.overrideRejected = forceAudit.overrideRejected;
            row.rejection = forceAudit.rejectionReason;
            row.baselineMean = baseline.mean;
            row.cpuMs = std::chrono::duration<double, std::milli>(altEnd - altStart).count();
            row.baselineCpuMs = controlRow.cpuMs;
            rows.push_back(row);
        }
    }
    std::ofstream file(out);
    file << "source_sha,instance,seed,freeze_evals,mode,lookahead,batch_limit,phase_bias,"
            "action_index,event_time,agv,blocked_machines,ready_outputs,waiting_inputs,free_agvs,"
            "baseline_output,baseline_machines,alternative_rank,alternative_output,alternative_machines,"
            "baseline_mean,alternative_mean,delta_pct,baseline_blocked,alternative_blocked,"
            "baseline_empty,alternative_empty,baseline_starved,alternative_starved,"
            "baseline_cpu_ms,alternative_cpu_ms,override_seen,override_applied,override_rejected,"
            "feasible,rejection_reason,prefix_fingerprint,band\n";
    file << std::setprecision(12);
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const Row& row = rows[i];
        const auto& s = row.snapshot;
        long double altMean = row.result.feasible ? row.result.mean : -1;
        long double delta = row.result.feasible
            ? 100.0L * (row.baselineMean - altMean) / row.baselineMean : 0;
        long double altStarved = 0;
        for (long double value : row.result.machineStarvedByMachine) altStarved += value;
        file << sha << ',' << digits << ',' << seed << ',' << freezeEvals << ',' << mode << ','
             << lookahead << ',' << frozen.batchLimit << ',' << candidate.phaseBias << ','
             << s.chosen.index << ',' << s.chosen.time << ',' << s.chosen.agv + 1 << ','
             << s.blockedMachines << ',' << s.readyOutputs << ',' << s.waitingInputs << ','
             << s.freeAgvs << ',' << (s.chosen.output ? 1 : 0) << ','
             << '"' << machinesText(s.chosen.machines) << '"' << ','
             << row.alternativeRank << ',' << (row.alternative.output ? 1 : 0) << ','
             << '"' << machinesText(row.alternative.machines) << '"' << ','
             << row.baselineMean << ',' << altMean << ',' << delta << ','
             << baseline.blockedTime << ',' << row.result.blockedTime << ','
             << baseline.emptyTravel << ',' << row.result.emptyTravel << ','
             << (std::accumulate(baseline.machineStarvedByMachine.begin(),
                                 baseline.machineStarvedByMachine.end(), 0.0L)) << ','
             << altStarved << ',' << row.baselineCpuMs << ',' << row.cpuMs << ','
             << (row.overrideSeen ? 1 : 0) << ',' << (row.overrideApplied ? 1 : 0) << ','
             << (row.overrideRejected ? 1 : 0) << ',' << (row.result.feasible ? 1 : 0) << ','
             << row.rejection << ',' << s.prefixFingerprint << ',' << i / (alts + 1) << '\n';
    }
    std::ofstream summary(out + ".summary.csv");
    summary << "instance,seed,freeze_evals,mode,lookahead,frozen_mean,action_count,"
               "eligible_action_count,selected_action_count,attempted_alternatives,"
               "valid_alternatives,improving_alternatives,best_single_mean,best_single_gain_pct,"
               "median_valid_delta_pct,median_cpu_ratio,coverage_status,source_sha\n";
    int selectedCount = static_cast<int>(selected.size());
    int attempted = 0, valid = 0, improving = 0;
    long double bestMean = baseline.mean;
    long double bestGain = 0;
    std::vector<long double> deltas;
    std::vector<long double> ratios;
    for (const auto& row : rows) {
        if (row.alternativeRank < 0) continue;
        ++attempted;
        if (row.overrideApplied && row.result.feasible) {
            ++valid;
            long double delta = 100.0L * (row.baselineMean - row.result.mean) / row.baselineMean;
            deltas.push_back(delta);
            if (row.cpuMs > 0 && row.baselineCpuMs > 0) ratios.push_back(row.cpuMs / row.baselineCpuMs);
            if (row.result.mean < bestMean) { bestMean = row.result.mean; bestGain = delta; }
            if (delta >= 0.5L) ++improving;
        }
    }
    long double medianDelta = 0;
    if (!deltas.empty()) {
        std::sort(deltas.begin(), deltas.end());
        medianDelta = deltas[deltas.size() / 2];
    }
    long double medianRatio = 0;
    if (!ratios.empty()) {
        std::sort(ratios.begin(), ratios.end());
        medianRatio = ratios[ratios.size() / 2];
    }
    int eligibleCount = 0;
    for (const auto& snapshot : audit.trace)
        if (!snapshot.alternatives.empty()) ++eligibleCount;
    summary << digits << ',' << seed << ',' << freezeEvals << ',' << mode << ',' << lookahead << ','
            << baseline.mean << ',' << audit.totalActions << ',' << eligibleCount << ','
            << selectedCount << ',' << attempted << ',' << valid << ',' << improving << ','
            << bestMean << ',' << bestGain << ',' << medianDelta << ',' << medianRatio << ','
            << (selectedCount >= 15 ? "ok" : "coverage-poor") << ',' << sha << '\n';
    return 0;
}
