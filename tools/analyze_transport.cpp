#include "transport.hpp"

#include <iomanip>
#include <iostream>

using namespace io1;

int main(int argc, char** argv) {
    if (argc != 3) return 1;
    Instance in = loadInstance(argv[1]);
    TransportPlan plan = loadTransportPlan(in, argv[2]);
    TransportEvaluator evaluator(in);
    long double mean = evaluator.evaluate(plan);
    long double process = 0, ideal = 0, spt = 0, busy = 0;
    for (int lot = 0; lot < in.n; ++lot) {
        process += evaluator.times[5 * lot + 2] + in.duration(lot, evaluator.machineOf[lot]);
        busy += in.duration(lot, evaluator.machineOf[lot]);
    }
    for (int j = 0; j < in.m; ++j) {
        long double time = 0;
        std::vector<long double> durations;
        for (int lot : plan.machines[j]) {
            long double duration = in.duration(lot, j);
            time += duration;
            ideal += time;
            durations.push_back(duration);
        }
        std::sort(durations.begin(), durations.end());
        time = 0;
        for (long double duration : durations) { time += duration; spt += time; }
    }
    Candidate bound = buildSlotAssignment(in, 0, 0);
    long double lower = 0;
    for (int j = 0; j < in.m; ++j) {
        long double time = 0;
        for (int lot : bound.sequence[j]) { time += in.duration(lot, j); lower += time; }
    }
    std::cout << std::fixed << std::setprecision(6)
              << "mean=" << mean << " process_mean=" << process / in.n
              << " ordered_ideal=" << ideal / in.n << " fixed_assignment_spt=" << spt / in.n
              << " unrelated_machine_bound=" << lower / in.n << " processing_busy=" << busy << '\n';
    for (int j = 0; j < in.m; ++j) {
        long double duration = 0;
        for (int lot : plan.machines[j]) duration += in.duration(lot, j);
        std::cout << "machine=" << j + 1 << " jobs=" << plan.machines[j].size() << " processing=" << duration << '\n';
    }
}
