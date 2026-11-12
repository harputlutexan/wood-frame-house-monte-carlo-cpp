// C++17 Wood-frame house: Steps 1-15, cost-minimizing nonanticipative recourse.
#include "optimization.hpp"
#include <iostream>
#include <sstream>

using namespace house;

static void outputMetrics(std::ofstream& f, const char* stage,
                          const std::vector<Metrics>& metrics) {
    for (const auto& m:metrics)
        f << stage << ',' << m.designIndex << ',' << m.feasible << ',' << m.energyMargin << ','
          << m.mean << ',' << m.p90 << ',' << m.budgetOverrun << ',' << m.scheduleOverrun << ','
          << m.meanDuration << ',' << m.meanActions << '\n';
}
static std::string designLabel(const Design& d) {
    std::ostringstream s;
    for (int i=0;i<5;++i) { if (i) s << " / "; s << d.label[i]; }
    return s.str();
}
int main(int argc, char** argv) {
    try {
        if (argc!=3 && argc!=4) {
            std::cerr << "Usage: " << argv[0] << " input.csv settings.csv [results.csv]\n";
            return 2;
        }
        const Config c=loadConfig(argv[1],argv[2]);
        const std::string output=argc==4?argv[3]:"results.csv";
        std::cout << std::fixed << std::setprecision(2);
        std::cout << "=== Wood-Frame House Monte Carlo Optimization (C++17) ===\n";
        std::cout << "Designs: " << c.designs.size() << ", scenarios/design: " << c.iterations << '\n';

        // Steps 1-11: all designs under the baseline distributions.
        const auto baseline=evaluateDesigns(c,c.samples);
        const Metrics& nominal=selectDesign(baseline);
        const Design& nominalDesign=c.designs[nominal.designIndex-1];
        const auto baselineCount=std::count_if(baseline.begin(),baseline.end(),
                                             [](const Metrics& m){return m.feasible;});
        std::cout << "Baseline feasible designs: " << baselineCount << "/72\n"
                  << "Nominal: #" << nominal.designIndex << " | " << designLabel(nominalDesign)
                  << " | mean $" << nominal.mean << " | P90 $" << nominal.p90 << '\n';

        // Step 12: rank 2850/3000 by max(cost/budget, duration/target), NOT original J.
        const Solution nominalPaths=PolicySolver(c,nominalDesign,c.samples,0).solve();
        const int critical=criticalScenario(c,nominalPaths,0);
        std::cout << "Critical scenario (95th-percentile rank): #" << critical+1
                  << " | cost $" << ownerCost(c,nominalPaths.traces[critical].state)
                  << " | duration " << nominalPaths.traces[critical].state.elapsed << " d\n";

        // Step 13: single-scenario leave-one-at-a-time median relief, and global Spearman.
        const Sensitivity sens=sensitivity(c,nominalDesign,nominalPaths,critical);
        std::cout << "Variable                          Local relief     Spearman    Candidate\n";
        for (int j=0;j<K;++j) {
            const bool isCandidate=std::find(sens.eligible.begin(),sens.eligible.end(),j)!=sens.eligible.end();
            std::cout << std::left << std::setw(34) << c.variableNames[j] << std::right
                      << std::setw(10) << std::setprecision(5) << sens.localRelief[j]
                      << std::setw(13) << sens.correlation[j] << "       " << (isCandidate?"yes":"no") << '\n';
        }
        std::cout << std::setprecision(2);

        // Step 14: evaluate each eligible distribution improvement with full policy reoptimization.
        const MitigationChoice selected=chooseMitigation(c,nominalDesign,nominal.mean,sens);
        for (int j=0;j<K;++j) if (selected.evaluated[j])
            std::cout << "Mitigation candidate: " << c.variableNames[j]
                      << " | net savings $" << selected.evaluatedSavings[j] << '\n';
        const std::string mitigationName=selected.variable<0?"NONE":c.variableNames[selected.variable];
        const double fee=selected.variable<0?0:c.mitigationFee[selected.variable];
        std::cout << "Mitigation: " << mitigationName << " | fee $" << fee
                  << " | nominal-design net savings $" << selected.netSavings << '\n';

        // Step 15: only rerun when a distribution actually changes.
        // With NONE, reusing baseline metrics is mathematically identical and avoids
        // another 72 x 3,000 phase-by-phase optimization evaluations.
        std::vector<Metrics> revisedMetrics;
        const std::vector<Metrics>* post=&baseline;
        if (selected.variable>=0) {
            const auto changed=c.remap(selected.variable);
            revisedMetrics=evaluateDesigns(c,changed,fee);
            post=&revisedMetrics;
        } else {
            std::cout << "No mitigation selected: reusing baseline results (no redundant rerun).\n";
        }
        const Metrics& finalDesign=selectDesign(*post);
        const auto finalCount=std::count_if(post->begin(),post->end(),
                                           [](const Metrics& m){return m.feasible;});
        std::cout << "Post-mitigation feasible designs: " << finalCount << "/72\n"
                  << "Final selected: #" << finalDesign.designIndex << " | "
                  << designLabel(c.designs[finalDesign.designIndex-1]) << '\n'
                  << "  Expected owner cost: $" << finalDesign.mean << '\n'
                  << "  P90 owner cost:      $" << finalDesign.p90 << '\n'
                  << "  Budget overrun:      " << 100*finalDesign.budgetOverrun << "%\n"
                  << "  Schedule overrun:    " << 100*finalDesign.scheduleOverrun << "%\n"
                  << "  Energy margin:      " << finalDesign.energyMargin << '\n'
                  << "  Feasible:           " << (finalDesign.feasible?"YES":"NO (fallback selection)") << '\n';

        std::ofstream out(output);
        if (!out) throw std::runtime_error("Cannot write results: "+output);
        out << std::setprecision(12);
        out << "stage,combination,feasible,energy_margin,expected_cost,p90_cost,budget_overrun,"
               "schedule_overrun,expected_duration,expected_action_count\n";
        outputMetrics(out,"baseline",baseline);
        outputMetrics(out,"post_mitigation",*post);
        std::cout << "Saved 144 design-stage results to " << output << '\n';
        std::cout << "Note: Original J score, NumPy PCG64 and post-Step-15 stress tests are intentionally"
                     " not part of this revised model.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << '\n';
        return 1;
    }
}
