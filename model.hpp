#pragma once
// CSV configuration, design combinations, distributions and common random numbers.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace house {
using Vec6 = std::array<double, 6>;
constexpr int K = 6;
constexpr double PI = 3.14159265358979323846;

inline std::vector<std::string> split(const std::string& s, char separator) {
    std::vector<std::string> out; std::string part;
    for (char c : s) { if (c == separator) { out.push_back(part); part.clear(); } else part += c; }
    out.push_back(part); return out;
}
inline std::vector<std::string> parseCsvLine(const std::string& s) {
    std::vector<std::string> out; std::string cell; bool quoted = false;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '"') {
            if (quoted && i + 1 < s.size() && s[i + 1] == '"') { cell += '"'; ++i; }
            else quoted = !quoted;
        } else if (c == ',' && !quoted) { out.push_back(cell); cell.clear(); }
        else if (c != '\r') cell += c;
    }
    if (quoted) throw std::runtime_error("Unterminated CSV quote");
    out.push_back(cell); return out;
}
using CsvRow = std::unordered_map<std::string, std::string>;
inline std::vector<CsvRow> readCsv(const std::string& path) {
    std::ifstream f(path); if (!f) throw std::runtime_error("Cannot open CSV: " + path);
    std::string line; if (!std::getline(f, line)) throw std::runtime_error("Empty CSV: " + path);
    const auto headers = parseCsvLine(line); std::vector<CsvRow> out;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        const auto values = parseCsvLine(line);
        if (values.size() != headers.size()) throw std::runtime_error("Malformed CSV row: " + line);
        CsvRow row;
        for (size_t i = 0; i < headers.size(); ++i) row.emplace(headers[i], values[i]);
        out.push_back(std::move(row));
    }
    return out;
}
inline double number(const CsvRow& row, const std::string& key, double fallback = 0) {
    auto it = row.find(key);
    return (it == row.end() || it->second.empty()) ? fallback : std::stod(it->second);
}
inline std::string field(const CsvRow& row, const std::string& key) {
    auto it = row.find(key); return it == row.end() ? "" : it->second;
}
inline std::unordered_map<std::string, double> keyValues(const std::string& value) {
    std::unordered_map<std::string, double> out;
    for (const auto& item : split(value, '|')) {
        auto pos = item.find('=');
        if (pos != std::string::npos) {
            const std::string val = item.substr(pos + 1);
            try { out[item.substr(0, pos)] = std::stod(val); } catch (const std::exception&) { }
        }
    }
    return out;
}
inline double get(const std::unordered_map<std::string, double>& data, const std::string& key) {
    const auto it = data.find(key);
    if (it == data.end()) throw std::runtime_error("Missing numeric parameter: " + key);
    return it->second;
}

// Acklam's inverse standard-normal CDF: stable without third-party libraries.
inline double normalQuantile(double p) {
    p = std::clamp(p, 1e-14, 1.0 - 1e-14);
    constexpr double a[] = {-3.969683028665376e+01, 2.209460984245205e+02,
       -2.759285104469687e+02, 1.383577518672690e+02, -3.066479806614716e+01, 2.506628277459239};
    constexpr double b[] = {-5.447609879822406e+01, 1.615858368580409e+02,
       -1.556989798598866e+02, 6.680131188771972e+01, -1.328068155288572e+01};
    constexpr double c[] = {-7.784894002430293e-03, -3.223964580411365e-01,
       -2.400758277161838, -2.549732539343734, 4.374664141464968, 2.938163982698783};
    constexpr double d[] = {7.784695709041462e-03, 3.224671290700398e-01,
       2.445134137142996, 3.754408661907416};
    if (p < 0.02425) {
        const double q = std::sqrt(-2.0 * std::log(p));
        return (((((c[0]*q+c[1])*q+c[2])*q+c[3])*q+c[4])*q+c[5]) /
               ((((d[0]*q+d[1])*q+d[2])*q+d[3])*q+1);
    }
    if (p > 1 - 0.02425) return -normalQuantile(1-p);
    const double q = p - 0.5, r = q*q;
    return (((((a[0]*r+a[1])*r+a[2])*r+a[3])*r+a[4])*r+a[5])*q /
           (((((b[0]*r+b[1])*r+b[2])*r+b[3])*r+b[4])*r+1);
}
inline double normalCdf(double z) { return 0.5 * std::erfc(-z / std::sqrt(2.0)); }

struct Distribution {
    std::string kind;
    double mean = 0, sd = 0, cv = 0, lo = 0, hi = 0;
    double inverse(double u) const {
        u = std::clamp(u, 1e-14, 1.0 - 1e-14);
        if (kind == "triangular") { // CSV 'mean' is actually the mode.
            const double cut = (mean - lo) / (hi - lo);
            return u < cut ? lo + std::sqrt(u*(hi-lo)*(mean-lo))
                           : hi - std::sqrt((1-u)*(hi-lo)*(hi-mean));
        }
        double mu = mean, sigma = sd;
        if (kind == "truncated_lognormal") {
            sigma = std::sqrt(std::log1p(cv*cv));
            mu = std::log(mean) - sigma*sigma/2;
        } else if (kind != "truncated_normal") throw std::runtime_error("Unknown distribution: " + kind);
        const double lowZ = (kind == "truncated_normal") ? (lo-mu)/sigma : (std::log(lo)-mu)/sigma;
        const double highZ = (kind == "truncated_normal") ? (hi-mu)/sigma : (std::log(hi)-mu)/sigma;
        const double p = normalCdf(lowZ) + u*(normalCdf(highZ)-normalCdf(lowZ));
        const double z = mu + sigma*normalQuantile(p);
        return std::clamp(kind == "truncated_normal" ? z : std::exp(z), lo, hi);
    }
};
struct Design {
    int index = 0;
    std::array<int,5> option{};
    std::array<std::string,5> label{};
};
struct Action {
    double d=1, c=1, x=1, fee=0, nextD=1, nextX=1;
};
// Parsed once at startup. Frequently used constants should never call stod in a hot loop.
struct NumericParams {
    double storyHeight=0, roofOverhang=0;
    double sitework=0, foundation=0, garage=0, wui=0, doors=0, interior=0, closeout=0;
    double annualEscalation=0, annualFinancing=0, scheduleTarget=0, delayCostPerDay=0;
    double ownerFixed=0, budgetLimit=0, p90Limit=0;
};

struct Config {
    NumericParams params;
    std::unordered_map<std::string, std::string> fixed;
    std::unordered_map<std::string, std::unordered_map<std::string,double>> lookups;
    std::array<std::vector<std::string>,5> choices;
    std::array<std::string,6> variableNames{};
    std::array<Distribution,6> baseline{}, mitigation{};
    std::array<bool,6> mitigable{};
    std::array<double,6> mitigationFee{};
    std::array<std::array<double,2>,6> bins{};
    std::array<Action,5> actions{};
    std::vector<Design> designs;
    std::vector<Vec6> uniforms, samples;
    double recourseBudget=22000, maxActions=2;
    int iterations=3000;
    double fixedDouble(const std::string& key) const { return std::stod(fixed.at(key)); }
    double lookup(const std::string& name, const std::string& key) const {
        return get(lookups.at(name),key);
    }
    int bin(int variable, double value) const {
        return value < bins[variable][0] ? 0 : (value <= bins[variable][1] ? 1 : 2);
    }
    std::vector<Vec6> remap(int variable=-1) const {
        if (variable >= 0) {
            // CRN: copy original scenarios, recompute only the improved variable.
            // No new uniforms and no unnecessary inverse-CDF calls for the other five.
            std::vector<Vec6> out = samples;
            for (size_t s=0; s<out.size(); ++s)
                out[s][variable] = mitigation[variable].inverse(uniforms[s][variable]);
            return out;
        }
        // First generation of all six physical variables from stored uniforms.
        std::vector<Vec6> out(uniforms.size());
        for (size_t s=0; s<out.size(); ++s)
            for (int v=0; v<K; ++v)
                out[s][v] = baseline[v].inverse(uniforms[s][v]);
        return out;
    }
};

inline Config loadConfig(const std::string& inputPath, const std::string& settingsPath) {
    Config c;
    const auto settings = readCsv(settingsPath);
    if (settings.size() != 27) throw std::runtime_error("Expected 27 settings records");
    const std::array<std::string,5> designNames = {"wall_system","roof_form","exterior_cladding","energy_package","contractor"};
    const std::array<std::string,5> actionNames = {"phase1.permit_expedite","phase2.panelized_framing",
        "phase3.weather_protection","phase4.early_procurement","phase5.closeout_overtime"};
    int variables = 0;
    for (const auto& row : readCsv(inputPath)) {
        const std::string group=field(row,"input_group"), key=field(row,"parameter"), val=field(row,"fixed_value");
        if (group=="fixed") c.fixed[key]=val;
        else if (group=="lookup") c.lookups[key]=keyValues(val);
        else if (group=="design") {
            for (int j=0;j<5;++j) if (key==designNames[j]) c.choices[j]=split(field(row,"allowed_values"),'|');
        } else if (group=="uncertain") {
            if (variables>=K) throw std::runtime_error("Too many uncertain inputs");
            c.variableNames[variables]=key;
            c.baseline[variables]={field(row,"distribution"),number(row,"mean"),number(row,"standard_deviation"),
                                   number(row,"coefficient_of_variation"),number(row,"minimum"),number(row,"maximum")};
            ++variables;
        } else if (group=="information_bin") {
            std::string name=key.substr(4);
            for (int j=0;j<K;++j) if (name==c.variableNames[j]) {
                const auto limits=split(val,'|');
                c.bins[j]={std::stod(limits.at(0)),std::stod(limits.at(1))};
            }
        } else if (group=="recourse_action") {
            for (int j=0;j<5;++j) if (key==actionNames[j]) {
                auto v=keyValues(val);
                c.actions[j]={get(v,"d"),get(v,"c"),get(v,"x"),get(v,"fee"),get(v,"next_d"),get(v,"next_x")};
            }
        } else if (group=="policy") {
            if (key=="recourse_budget") c.recourseBudget=std::stod(val);
            if (key=="maximum_intensive_recourse_count") c.maxActions=std::stod(val);
        } else if (group=="mitigation") {
            for (int j=0;j<K;++j) if (key==c.variableNames[j]) {
                c.mitigable[j]=true;
                c.mitigationFee[j]=get(keyValues(val),"fee");
                c.mitigation[j]={field(row,"distribution"),number(row,"mean"),number(row,"standard_deviation"),
                                 number(row,"coefficient_of_variation"),number(row,"minimum"),number(row,"maximum")};
            }
        }
    }
    if (variables!=K) throw std::runtime_error("Expected six uncertain variables");
    for (const auto& v : c.choices) if (v.empty()) throw std::runtime_error("Missing design choices");
    // Cartesian order: leftmost design variable varies slowest, contractor fastest.
    for (int a=0;a<(int)c.choices[0].size();++a)
    for (int b=0;b<(int)c.choices[1].size();++b)
    for (int d=0;d<(int)c.choices[2].size();++d)
    for (int e=0;e<(int)c.choices[3].size();++e)
    for (int f=0;f<(int)c.choices[4].size();++f) {
        Design design; design.index=(int)c.designs.size()+1;
        design.option={a,b,d,e,f};
        for (int i=0;i<5;++i) design.label[i]=c.choices[i][design.option[i]];
        c.designs.push_back(design);
    }
    if (c.designs.size()!=72) throw std::runtime_error("Expected 72 design/contractor combinations");
    // Parse numeric CSV constants once. The simulation then reads doubles directly.
    auto& p=c.params;
    p.storyHeight=c.fixedDouble("story_height");
    p.roofOverhang=c.fixedDouble("roof_overhang");
    p.sitework=c.fixedDouble("sitework_cost");
    p.foundation=c.fixedDouble("foundation_cost");
    p.garage=c.fixedDouble("garage_cost");
    p.wui=c.fixedDouble("wildland_urban_interface_cost");
    p.doors=c.fixedDouble("doors_cost");
    p.interior=c.fixedDouble("interior_cost");
    p.closeout=c.fixedDouble("closeout_cost");
    p.annualEscalation=c.fixedDouble("annual_escalation_rate");
    p.annualFinancing=c.fixedDouble("annual_financing_rate");
    p.scheduleTarget=c.fixedDouble("schedule_target");
    p.delayCostPerDay=c.fixedDouble("delay_cost_per_day");
    p.ownerFixed=c.fixedDouble("owner_fixed_costs");
    p.budgetLimit=c.fixedDouble("budget_limit");
    p.p90Limit=c.fixedDouble("percentile_90_cost_limit");
    c.iterations=(int)c.fixedDouble("simulation_iterations");
    c.uniforms.resize(c.iterations);
    std::mt19937_64 rng((uint64_t)c.fixedDouble("random_seed"));
    // Variable-major generation, 3,000 stored draws per variable. C++ mt19937_64,
    // not NumPy PCG64: benchmark scenario indices are not expected to match.
    for (int v=0;v<K;++v)
        for (int s=0;s<c.iterations;++s)
            c.uniforms[s][v]=std::generate_canonical<double,53>(rng);
    c.samples=c.remap();
    return c;
}
} // namespace house
