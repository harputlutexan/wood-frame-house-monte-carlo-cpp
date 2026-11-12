#pragma once
// Quantity takeoff, construction costs, contractor normalization, phase-by-phase cash flow.
#include "model.hpp"

namespace house {
struct DesignData {
    double wallFactor, wallAdder, wallEnergy, wallDuration, wallLabor;
    double roofAreaFactor, roofCostFactor, roofPenalty, roofDuration;
    double claddingUnit, claddingDuration;
    double packageCost, windowFactor, mepFactor, packageEnergy, packageDuration;
    double bidFactor, general, feeRate, excluded, allowance, allowanceFactor, outsideFactor;
    double contractorDuration, changeFactor, claddingFactor, roofFactor, advancedFactor;
    double controlFactor, controlFee;
    double netWall, roofArea, energyMargin;
    bool advanced;
};
inline DesignData prepare(const Config& cfg, const Design& design) {
    const auto& wall=cfg.lookups.at("wall."+design.label[0]);
    const auto& roof=cfg.lookups.at("roof."+design.label[1]);
    const auto& clad=cfg.lookups.at("cladding."+design.label[2]);
    const auto& pack=cfg.lookups.at("energy."+design.label[3]);
    const auto& cont=cfg.lookups.at("contractor."+design.label[4]);
    DesignData d{};
    d.wallFactor=get(wall,"framing_factor"); d.wallAdder=get(wall,"wall_adder");
    d.wallEnergy=get(wall,"wall_energy"); d.wallDuration=get(wall,"wall_duration");
    d.wallLabor=get(wall,"wall_labor_factor");
    d.roofAreaFactor=get(roof,"roof_area_factor"); d.roofCostFactor=get(roof,"roof_cost_factor");
    d.roofPenalty=get(roof,"roof_energy_penalty"); d.roofDuration=get(roof,"roof_duration");
    d.claddingUnit=get(clad,design.label[0]); d.claddingDuration=get(clad,"cladding_duration");
    d.packageCost=get(pack,"package_cost"); d.windowFactor=get(pack,"window_factor");
    d.mepFactor=get(pack,"mechanical_electrical_plumbing_factor");
    d.packageEnergy=get(pack,"package_energy"); d.packageDuration=get(pack,"package_duration");
    d.bidFactor=get(cont,"bid_factor"); d.general=get(cont,"general_conditions");
    d.feeRate=get(cont,"fee_rate"); d.excluded=get(cont,"excluded_scope");
    d.allowance=get(cont,"allowance"); d.allowanceFactor=get(cont,"allowance_factor");
    d.outsideFactor=get(cont,"outside_scope_factor");
    d.contractorDuration=get(cont,"duration_factor"); d.changeFactor=get(cont,"change_order_factor");
    d.claddingFactor=get(cont,design.label[2]=="stucco" ? "stucco_factor" : "fiber_cement_factor");
    d.roofFactor=get(cont,design.label[1]=="gable" ? "gable_factor" : "hip_factor");
    d.advancedFactor=get(cont,"advanced_wall_factor");
    d.controlFactor=get(cont,"change_control_factor"); d.controlFee=get(cont,"change_control_fee");
    d.advanced=(design.label[0]=="2x6_advanced_framing");
    // CSV geometry: house + garage roof plan, specified opening reduction / allowance.
    const auto dims=split(cfg.fixed.at("floor_plate_dimensions"),'x');
    const auto garage=split(cfg.fixed.at("attached_garage_dimensions"),'x');
    const double length=std::stod(dims.at(0)), width=std::stod(dims.at(1));
    const double gl=std::stod(garage.at(0)), gw=std::stod(garage.at(1));
    const double story=cfg.params.storyHeight, overhang=cfg.params.roofOverhang;
    const auto pitch=split(cfg.fixed.at("roof_pitch"),':');
    const double slope=std::stod(pitch.at(0))/std::stod(pitch.at(1));
    const double gross=2*(length+width)*story*2;
    d.netWall=gross*(1-0.18)+620;
    d.roofArea=((length+2*overhang)*(width+2*overhang)+
               (gl+2*overhang)*(gw+2*overhang))*std::sqrt(1+slope*slope)*d.roofAreaFactor*1.08;
    d.energyMargin=d.wallEnergy+d.packageEnergy-d.roofPenalty-0.05;
    return d;
}
struct Trade {
    std::array<double,5> base{}, duration{}, exposure{};
    double subtotal=0, normalized=0;
};
inline Trade tradeCosts(const Config& cfg, const DesignData& d, const Vec6& u) {
    // Lumber index = material price ratio, productivity affects labor inversely.
    const double framing=0.62*u[0]+0.38/(u[1]*d.wallLabor);
    const double wall=d.netWall*26*d.wallFactor*framing*(d.advanced?d.advancedFactor:1.0);
    const double second=21600*framing;
    const double roof=d.roofArea*32*d.roofCostFactor*framing*d.roofFactor;
    const double clad=d.netWall*d.claddingUnit*d.claddingFactor;
    const double windows=52000*d.windowFactor;
    const double mep=165000*d.mepFactor*u[2];
    Trade t;
    t.base={cfg.params.sitework+cfg.params.foundation,
            wall+second+roof+cfg.params.garage+cfg.params.wui,
            clad+windows+d.netWall*d.wallAdder+cfg.params.doors,
            mep+d.packageCost,
            cfg.params.interior+cfg.params.closeout};
    t.subtotal=std::accumulate(t.base.begin(),t.base.end(),0.0);
    const double covered=std::max(t.subtotal-d.excluded,0.0);
    t.normalized=(d.bidFactor*covered+d.general+d.allowance*d.allowanceFactor)*(1+d.feeRate)
                 +d.excluded*d.outsideFactor;
    for (double& b : t.base) b*=t.normalized/t.subtotal; // allocate full normalized bid by trade.
    t.duration={55+u[3],
                d.contractorDuration*(65*d.wallDuration+45*d.roofDuration)/u[1],
                d.contractorDuration*40*d.claddingDuration+0.65*u[4],
                55+d.packageDuration+0.15*u[4],
                80+0.20*u[4]};
    t.exposure={0.35,0.90,1.00,1.35,0.85};
    if (d.advanced) { t.exposure[1]*=1.35; t.exposure[2]*=1.35; }
    return t;
}
struct State {
    double elapsed=0, financed=0, finance=0, delay=0, remainingAllowance=0;
    double rawChange=0, spend=0, carryD=1, carryX=1;
    int intensive=0;
    bool control=false;
};
inline State initialState(const DesignData& d) {
    State s; s.remainingAllowance=d.allowance*d.allowanceFactor; return s;
}
inline double effectiveActionFee(const Config& cfg, int phase, uint8_t priorMask) {
    double fee=cfg.actions[phase].fee;
    if (phase==1 && (priorMask & (1u<<0))) fee*=1.20;
    if (phase==2 && (priorMask & (1u<<1))) fee*=0.70;
    if (phase==3 && (priorMask & (1u<<2))) fee*=0.75;
    return fee;
}
inline bool actionAvailable(const Config& cfg, const Design& d, int phase,
                            const Vec6& u, const State& s, uint8_t priorMask) {
    bool trigger=false;
    switch (phase) {
        case 0: trigger=cfg.bin(3,u[3])==2; break;
        case 1: trigger=d.option[0]!=0 && (cfg.bin(0,u[0])==2 || cfg.bin(1,u[1])==0); break;
        case 2: trigger=cfg.bin(4,u[4])==2; break;
        case 3: trigger=cfg.bin(2,u[2])==2; break;
        case 4: trigger=(cfg.bin(5,u[5])==2 || cfg.bin(4,u[4])==2) && !(priorMask&(1u<<3)); break;
    }
    return trigger && s.intensive < cfg.maxActions &&
           s.spend+effectiveActionFee(cfg,phase,priorMask) <= cfg.recourseBudget+1e-9;
}
inline State applyPhase(const Config& cfg, const DesignData& d, const Vec6& u,
                        const Trade& t, int phase, State s, bool intervene, uint8_t priorMask) {
    const Action a=intervene?cfg.actions[phase]:Action{};
    const double fee=intervene?effectiveActionFee(cfg,phase,priorMask):0.0;
    const double duration=t.duration[phase]*s.carryD*a.d;
    const double start=s.elapsed;
    const double base=t.base[phase]*a.c;
    const double escalation=std::pow(1+cfg.params.annualEscalation,(start+0.5*duration)/365);
    const double escalated=base*escalation;
    const double raw=escalated*u[5]*d.changeFactor*(s.control?d.controlFactor:1.0)*
                     t.exposure[phase]*s.carryX*a.x;
    const double used=std::min(s.remainingAllowance,raw);
    const double cash=escalated+raw-used+fee;
    s.finance+=cfg.params.annualFinancing*(s.financed+0.5*cash)*duration/365;
    const double target=cfg.params.scheduleTarget;
    const double late=std::max(start+duration-target,0.0)-std::max(start-target,0.0);
    s.delay+=late*cfg.params.delayCostPerDay;
    s.financed+=cash; s.elapsed+=duration; s.rawChange+=raw;
    s.remainingAllowance-=used;
    s.spend+=fee; s.intensive+=intervene?1:0;
    s.carryD=a.nextD; s.carryX=a.nextX; // next-phase carry; no action restores factor 1.
    if (phase==2 && d.allowance*d.allowanceFactor>0 &&
        1-s.remainingAllowance/(d.allowance*d.allowanceFactor)>0.70) {
        s.control=true;
        s.financed+=d.controlFee; // after phase-3 financing, one-time change control.
    }
    return s;
}
inline double ownerCost(const Config& c, const State& s, double mitigationFee=0) {
    return s.financed+s.finance+s.delay+c.params.ownerFixed+mitigationFee;
}
inline double costScheduleRisk(const Config& c, const State& s, double mitigationFee=0) {
    return std::max(ownerCost(c,s,mitigationFee)/c.params.budgetLimit,
                    s.elapsed/c.params.scheduleTarget);
}
} // namespace house
