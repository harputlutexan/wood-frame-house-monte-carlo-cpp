#pragma once
// Exact backward induction for expected cost, conditional on observed information bins.
// Memory: only current scenario paths and per-scenario results, never all 216,000 phase histories.
#include "engineering.hpp"

namespace house {
struct Trace {
    int index=0; State state{}; uint8_t actions=0;
};
struct Solution {
    std::vector<Trace> traces;
    double sumCost=0, sumDuration=0, sumRaw=0, sumSpend=0;
    void append(Solution&& other) {
        sumCost+=other.sumCost; sumDuration+=other.sumDuration;
        sumRaw+=other.sumRaw; sumSpend+=other.sumSpend;
        traces.insert(traces.end(),std::make_move_iterator(other.traces.begin()),
                      std::make_move_iterator(other.traces.end()));
    }
};
class PolicySolver {
    const Config& cfg;
    const Design& design;
    const DesignData data;
    const std::vector<Vec6>& scenarios;
    const double mitigationFee;
    std::vector<Trade> trades; // reused for every decision branch of this design.

    int newInformationBin(int phase, const Vec6& u) const {
        switch(phase) {
            case 0: return cfg.bin(3,u[3]); // permit
            case 1: return 3*cfg.bin(0,u[0])+cfg.bin(1,u[1]); // lumber, productivity
            case 2: return cfg.bin(4,u[4]); // weather
            case 3: return cfg.bin(2,u[2]); // MEP
            default:return cfg.bin(5,u[5]); // change orders
        }
    }
    static bool lessObjective(const Solution& a, const Solution& b) {
        // Minimize expected total cost; duration/changes/spend are deterministic tie breakers.
        constexpr double tol=1e-5;
        if (std::abs(a.sumCost-b.sumCost)>tol) return a.sumCost<b.sumCost;
        if (std::abs(a.sumDuration-b.sumDuration)>tol) return a.sumDuration<b.sumDuration;
        if (std::abs(a.sumRaw-b.sumRaw)>tol) return a.sumRaw<b.sumRaw;
        if (std::abs(a.sumSpend-b.sumSpend)>tol) return a.sumSpend<b.sumSpend;
        return false; // keep standard action before nonstandard action.
    }
    Solution solveNode(int phase, const std::vector<Trace>& incoming) const {
        if (incoming.empty()) return {};
        Solution best; bool hasBest=false;
        const auto& first=incoming.front();
        const bool eligible=actionAvailable(cfg,design,phase,scenarios[first.index],first.state,first.actions);
        for (int choice=0; choice<=(eligible?1:0); ++choice) {
            Solution candidate; candidate.traces.reserve(incoming.size());
            std::array<std::vector<Trace>,9> groups;
            for (const Trace& path : incoming) {
                Trace next=path;
                next.state=applyPhase(cfg,data,scenarios[path.index],trades[path.index],phase,
                                      path.state,choice==1,path.actions);
                if (choice) next.actions|=(uint8_t)(1u<<phase);
                if (phase==4) {
                    candidate.sumCost+=ownerCost(cfg,next.state,mitigationFee);
                    candidate.sumDuration+=next.state.elapsed;
                    candidate.sumRaw+=next.state.rawChange;
                    candidate.sumSpend+=next.state.spend;
                    candidate.traces.push_back(next);
                } else {
                    groups[newInformationBin(phase+1,scenarios[path.index])].push_back(next);
                }
            }
            if (phase!=4) for (const auto& group:groups)
                if (!group.empty()) candidate.append(solveNode(phase+1,group));
            if (!hasBest || lessObjective(candidate,best)) {
                best=std::move(candidate); hasBest=true;
            }
        }
        return best;
    }
public:
    PolicySolver(const Config& c, const Design& d, const std::vector<Vec6>& x, double fee)
      : cfg(c), design(d), data(prepare(c,d)), scenarios(x), mitigationFee(fee) {
        trades.reserve(x.size());
        for (const auto& s:x) trades.push_back(tradeCosts(cfg,data,s));
    }
    Solution solve() const {
        std::array<std::vector<Trace>,3> groups;
        for (size_t i=0;i<scenarios.size();++i) {
            Trace t; t.index=(int)i; t.state=initialState(data);
            groups[cfg.bin(3,scenarios[i][3])].push_back(t);
        }
        Solution out; out.traces.reserve(scenarios.size());
        for (const auto& group:groups) if (!group.empty()) out.append(solveNode(0,group));
        std::sort(out.traces.begin(),out.traces.end(),[](const auto& a,const auto& b){return a.index<b.index;});
        return out;
    }
};
struct Metrics {
    int designIndex=0;
    double mean=0, p90=0, budgetOverrun=0, scheduleOverrun=0, energyMargin=0;
    double meanDuration=0, meanActions=0;
    bool feasible=false;
};
inline Metrics summarize(const Config& cfg, const Design& d, const Solution& result, double fee) {
    Metrics m; m.designIndex=d.index;
    m.energyMargin=prepare(cfg,d).energyMargin;
    std::vector<double> costs; costs.reserve(result.traces.size());
    double totalDuration=0, actions=0;
    for (const auto& t:result.traces) {
        const double c=ownerCost(cfg,t.state,fee);
        costs.push_back(c);
        m.budgetOverrun+=(c>cfg.params.budgetLimit)?1:0;
        m.scheduleOverrun+=(t.state.elapsed>cfg.params.scheduleTarget)?1:0;
        totalDuration+=t.state.elapsed;
        actions+=t.state.intensive;
    }
    const double n=(double)costs.size();
    m.mean=result.sumCost/n; m.budgetOverrun/=n; m.scheduleOverrun/=n;
    m.meanDuration=totalDuration/n; m.meanActions=actions/n;
    std::sort(costs.begin(),costs.end());
    const double k=0.9*(n-1); const size_t left=(size_t)std::floor(k);
    m.p90=costs[left]+(k-left)*(costs[left+1]-costs[left]);
    m.feasible=(m.energyMargin>=0 && m.budgetOverrun<=0.35 && m.scheduleOverrun<=0.40 &&
                m.p90<=cfg.params.p90Limit);
    return m;
}
inline bool betterDesign(const Metrics& x, const Metrics& y) {
    if (x.feasible!=y.feasible) return x.feasible;
    auto less=[](double a,double b){return a<b-1e-9;};
    if (!x.feasible) {
        if (std::abs(x.energyMargin-y.energyMargin)>1e-9) return x.energyMargin>y.energyMargin;
        if (less(x.budgetOverrun,y.budgetOverrun)) return true;
        if (less(y.budgetOverrun,x.budgetOverrun)) return false;
        if (less(x.scheduleOverrun,y.scheduleOverrun)) return true;
        if (less(y.scheduleOverrun,x.scheduleOverrun)) return false;
        if (less(x.p90,y.p90)) return true;
        if (less(y.p90,x.p90)) return false;
        return x.designIndex<y.designIndex;
    }
    if (less(x.mean,y.mean)) return true;
    if (less(y.mean,x.mean)) return false;
    if (less(x.p90,y.p90)) return true;
    if (less(y.p90,x.p90)) return false;
    if (less(x.scheduleOverrun,y.scheduleOverrun)) return true;
    if (less(y.scheduleOverrun,x.scheduleOverrun)) return false;
    return x.designIndex<y.designIndex;
}
inline const Metrics& selectDesign(const std::vector<Metrics>& list) {
    return *std::min_element(list.begin(),list.end(),betterDesign);
}
inline std::vector<Metrics> evaluateDesigns(const Config& cfg, const std::vector<Vec6>& samples,
                                            double fee=0) {
    std::vector<Metrics> out; out.reserve(cfg.designs.size());
    for (const auto& d:cfg.designs) {
        Solution s=PolicySolver(cfg,d,samples,fee).solve();
        out.push_back(summarize(cfg,d,s,fee));
    }
    return out;
}
inline std::vector<double> ranks(const std::vector<double>& values) {
    std::vector<int> order(values.size()); std::iota(order.begin(),order.end(),0);
    std::sort(order.begin(),order.end(),[&](int a,int b){
        return values[a]==values[b]?a<b:values[a]<values[b];
    });
    std::vector<double> result(values.size());
    for (size_t i=0;i<order.size();) {
        size_t j=i+1; while (j<order.size() && values[order[j]]==values[order[i]]) ++j;
        const double avg=(i+1+j)*0.5; // average of 1-based ranks i+1 through j.
        for (size_t k=i;k<j;++k) result[order[k]]=avg;
        i=j;
    }
    return result;
}
inline double spearman(const std::vector<double>& x, const std::vector<double>& y) {
    const auto a=ranks(x),b=ranks(y); const double n=(double)x.size();
    const double center=(n+1)/2;
    double xy=0,xx=0,yy=0;
    for (size_t i=0;i<x.size();++i) {
        const double u=a[i]-center,v=b[i]-center;
        xy+=u*v; xx+=u*u; yy+=v*v;
    }
    return xx*yy>0?xy/std::sqrt(xx*yy):0;
}
inline int criticalScenario(const Config& cfg, const Solution& s, double fee) {
    std::vector<std::pair<double,int>> scored;
    for (const auto& t:s.traces)
        scored.push_back({costScheduleRisk(cfg,t.state,fee),t.index});
    std::sort(scored.begin(),scored.end());
    return scored.at(2849).second; // exact 1-based rank 2850, returns 0-based index.
}
inline State replayActions(const Config& cfg, const Design& d, const Vec6& u, uint8_t mask) {
    const DesignData data=prepare(cfg,d);
    const Trade trade=tradeCosts(cfg,data,u);
    State st=initialState(data); uint8_t previous=0;
    for (int phase=0;phase<5;++phase) {
        const bool active=(mask & (1u<<phase));
        st=applyPhase(cfg,data,u,trade,phase,st,active,previous);
        if (active) previous|=(uint8_t)(1u<<phase);
    }
    return st;
}
struct Sensitivity {
    std::array<double,6> localRelief{}, correlation{};
    std::vector<int> eligible;
};
inline Sensitivity sensitivity(const Config& cfg, const Design& design,
                                const Solution& baseline, int critical, double fee=0) {
    Sensitivity o;
    const auto& ref=baseline.traces.at(critical); // trace vector sorted by scenario ID.
    const double risk=costScheduleRisk(cfg,ref.state,fee);
    std::vector<double> costs(baseline.traces.size());
    for (const auto& path:baseline.traces)
        costs[path.index]=ownerCost(cfg,path.state,fee);
    for (int j=0;j<6;++j) {
        Vec6 adjusted=cfg.samples[critical];
        adjusted[j]=cfg.baseline[j].inverse(0.5);
        const State after=replayActions(cfg,design,adjusted,ref.actions);
        o.localRelief[j]=risk-costScheduleRisk(cfg,after,fee);
        std::vector<double> input; input.reserve(cfg.samples.size());
        for (const auto& v:cfg.samples) input.push_back(v[j]);
        o.correlation[j]=spearman(input,costs);
    }
    std::array<int,6> local{0,1,2,3,4,5}, global=local;
    std::sort(local.begin(),local.end(),[&](int a,int b){
        return o.localRelief[a]!=o.localRelief[b]?o.localRelief[a]>o.localRelief[b]:a<b;
    });
    std::sort(global.begin(),global.end(),[&](int a,int b){
        return std::abs(o.correlation[a])!=std::abs(o.correlation[b])?
               std::abs(o.correlation[a])>std::abs(o.correlation[b]):a<b;
    });
    for (int j=0;j<6;++j) if (cfg.mitigable[j] &&
        std::find(local.begin(),local.begin()+3,j)!=local.begin()+3 &&
        std::find(global.begin(),global.begin()+4,j)!=global.begin()+4)
        o.eligible.push_back(j);
    return o;
}
struct MitigationChoice {
    int variable=-1; double netSavings=0;
    std::array<double,6> evaluatedSavings{};
    std::array<bool,6> evaluated{};
};
inline MitigationChoice chooseMitigation(const Config& cfg, const Design& nominal,
                                         double baselineMean, const Sensitivity& sens) {
    MitigationChoice best;
    for (int variable:sens.eligible) {
        const auto changed=cfg.remap(variable); // SAME stored uniforms, changed inverse CDF.
        const Solution result=PolicySolver(cfg,nominal,changed,0).solve();
        const double candidateMean=result.sumCost/(double)changed.size();
        const double benefit=baselineMean-candidateMean-cfg.mitigationFee[variable];
        best.evaluated[variable]=true; best.evaluatedSavings[variable]=benefit;
        if (benefit>best.netSavings+1e-7 ||
            (benefit>0 && std::abs(benefit-best.netSavings)<1e-7 && best.variable>=0 &&
             (sens.localRelief[variable]>sens.localRelief[best.variable] ||
             (sens.localRelief[variable]==sens.localRelief[best.variable] &&
              std::abs(sens.correlation[variable])>std::abs(sens.correlation[best.variable]))))) {
            best.variable=variable; best.netSavings=benefit;
        }
    }
    return best;
}
} // namespace house
