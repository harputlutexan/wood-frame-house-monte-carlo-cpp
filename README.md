# Wood-Frame House Monte Carlo Optimization (C++17)

An end-to-end, reproducible **Monte Carlo engineering simulation and cost optimization** case study for a two-story California wood-frame house. The model evaluates **72 design-contractor combinations** under **3,000 common scenarios**, simulates five construction phases, optimizes adaptive construction interventions and selects the lowest-expected-cost feasible design.

> **Scope:** All data and engineering/cost coefficients are synthetic benchmark assumptions. This computational demonstration is **not** a validated construction estimate, code-compliant structural design, or substitute for professional engineering review.

## Model and methods

| Item | Configuration |
| --- | --- |
| Language | C++17, standard library only |
| Inputs | Two CSV files |
| Fixed parameters | 22 |
| Design categories | Wall (3), roof (2), cladding (2), energy package (2), contractor (3) |
| Design-contractor combinations | **72** |
| Uncertain variables | **6** |
| Distributions | Truncated normal, truncated lognormal, triangular |
| Monte Carlo | **3,000** common scenarios per design, **216,000** initial design-scenario evaluations |
| Random engine | `std::mt19937_64`, seed `74291` |
| Interventions | Phase-specific, nonanticipative, $22,000 spend cap and max two intensive actions |

Six uncertainties represent lumber pricing, framing labor productivity, MEP pricing, permit delay, weather delay and change-order percentage. Common random numbers allow fair comparisons across designs and mitigation options.

## Example results

| Metric | Result |
| --- | ---: |
| Baseline-feasible combinations | **53 / 72** |
| Nominal design | **#33** |
| Critical scenario at 95th-percentile rank | **#2224** |
| Mitigation selected | **NONE**; tested options were not cost-effective |
| Final design | **#33** |
| Expected owner cost | **$1,209,984.33** |
| P90 owner cost | **$1,254,602.99** |
| Budget overrun probability | **0.03%** |
| Schedule overrun probability | **1.80%** |
| Energy margin | **0.03** |

Final configuration: 2x6 cavity-insulated wall, gable roof, fiber-cement cladding, code-minimum energy package, contractor C. These are results of the **synthetic model**, not observed project costs. A full baseline and post-mitigation result table is provided in `sample_results/results.csv`.

## Build and run

Run these commands from the repository root, where both input CSV files are located.

### Linux / macOS

```bash
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic main.cpp -o wood_house
./wood_house california_wood_house_inputs_v4.csv california_wood_house_settings_v4.csv results.csv
python3 scripts/verify_outputs.py results.csv
```

Apple Clang (`clang++`) is also supported.

### Windows (Visual Studio Developer Command Prompt)

```bat
cl /std:c++17 /O2 /EHsc /W4 main.cpp /Fe:wood_house.exe
wood_house.exe california_wood_house_inputs_v4.csv california_wood_house_settings_v4.csv results.csv
python scripts/verify_outputs.py results.csv
```

Alternatively run `build_windows.bat` to build and execute the model.

### CMake

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

With a Visual Studio generator, the executable typically lives in `build/Release/`.

## Algorithm (15 steps)

1. Load and validate two CSV configuration files.
2. Enumerate 72 combinations of construction and contractor choices.
3. Set the six bounded uncertainty distributions.
4. Generate 3,000 common Monte Carlo scenarios and reuse them across all designs.
5. Calculate house, garage, wall and roof quantities.
6. Estimate scenario-dependent trade and construction costs.
7. Normalize contractors' pricing, fees and scope exclusions.
8. Simulate five phases with costs, time, financing, escalation and delay penalties.
9. Select affordable, eligible adaptive interventions without using future information.
10. Filter designs using energy margin, budget and schedule exceedance probabilities, and P90 cost.
11. Select the lowest-expected-cost feasible nominal design.
12. Select the 95th-percentile-rank critical scenario using normalized cost and schedule risk.
13. Combine local median-replacement sensitivity with Spearman rank correlations to shortlist uncertainties.
14. Test mitigation options using original random samples and subtract mitigation fees.
15. Re-evaluate all designs if mitigation is selected; otherwise reuse baseline metrics. Choose final minimum-cost feasible design.

## Implementation and efficiency

- `model.hpp`: parsing, design enumeration, bounded distributions and Monte Carlo draws.
- `engineering.hpp`: building quantities, construction costs, contractor bid normalization and phase simulation.
- `optimization.hpp`: nonanticipative intervention optimization, feasibility, scenario ranking and mitigation selection.
- `main.cpp`: runs the pipeline and writes the result CSV.
- Designs are evaluated sequentially; temporary trade-cost data are cached only for the current design.
- Larger data structures use references and move semantics to avoid avoidable copies.
- Common uniform draws are retained; mitigation remaps only the affected uncertainty.
- Frequently used numeric CSV parameters are parsed once.
- When mitigation is `NONE`, Step 15 reuses existing baseline results, avoiding another 216,000 evaluations.

Output `results.csv` contains **144 rows** (72 baseline and 72 post-mitigation summaries). When mitigation is `NONE`, the second group intentionally duplicates baseline metrics.

## Differences from the source benchmark

This revised 15-step model minimizes **expected owner cost** in adaptive intervention optimization instead of using a custom `J` score, and selects the critical scenario using normalized cost/schedule ratios. It uses `std::mt19937_64`, not NumPy PCG64. It does **not** claim an exact reproduction of the original reference scenario sequence. Tail-risk CVaR95, additional sequential stress tests and minimax regret are outside the scope of this revision.

The settings CSV is validated and read, but descriptive formula entries are implemented explicitly in C++ rather than dynamically interpreted. Changes to descriptive settings alone do not rewrite the compiled algorithms.

## Automated verification

GitHub Actions compiles and runs the project on Linux and Windows, then checks generated results against the included reference output. Cross-compiler numeric comparisons allow small floating-point tolerances.
