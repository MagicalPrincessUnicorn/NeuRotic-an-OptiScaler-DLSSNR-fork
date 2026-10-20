#pragma once
#include "PreparedGuideStatusV2.h"
#include "connections/ConnectionPolicy.h"
#include "NrPreflightState.h"
namespace DlssNr::OwnerReporting {
struct PreparedView {
 using State=NrPreflightState::State;
 State color=State::NotReported,depth=State::NotReported,motion=State::NotReported;
 State evaluation=State::NotReported,delivery=State::NotReported;
 bool sourceChoices=false;
};
inline PreparedView Prepared(const PreparedGuides::SnapshotV2& input,
 const Connections::SignalStatusSnapshot& signals,bool selected) noexcept {
 PreparedView out;using S=NrPreflightState::State;
 if(!selected||!input.available||!input.fresh)return out;
 const auto& p=input.status;
 out.sourceChoices=p.sourceApi!=0x20000;
 if(p.restartRequired){out.evaluation=out.delivery=S::Fault;return out;}
 if(p.outputValid&&p.session&&p.capture)out.evaluation=out.delivery=S::Consumed;
 else if(p.modelPreparing)out.evaluation=out.delivery=S::Pending;
 const bool used=p.outputValid&&signals.usedFresh&&signals.used.consumed&&
  signals.used.capture==p.capture&&signals.used.generation==p.generation;
 const bool offered=signals.fresh&&signals.status.capture==p.capture&&signals.status.generation==p.generation;
 const auto state=[&](uint32_t consumed,uint32_t available){
  return used&&consumed?S::Consumed:offered&&available?S::Usable:S::NotReported;
 };
 out.color=state(signals.used.color,signals.status.color);
 out.depth=state(signals.used.depth,signals.status.depth);
 out.motion=state(signals.used.motion,signals.status.motion);
 return out;
}
}
