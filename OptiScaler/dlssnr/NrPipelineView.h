#pragma once
#include <array>
#include <cstdint>

namespace DlssNr::PipelineView {
// Fixed presentation of existing owners, never an execution graph or admission policy.
enum class Stage { Render,Inputs,Nr,Sr,Present,Fg,Delivery };
enum class Placement { Unknown,BeforeSr,AfterSr,AfterPresent,CapturedImage };
enum class FgOrder { Unknown,BeforePresent,AfterPresent };
enum class State { Unknown,Off,Requested,Detected,Used,Fallback,Blocked };
struct Facts {
    Placement requested=Placement::Unknown,effective=Placement::Unknown;
    FgOrder fgOrder=FgOrder::Unknown;
    bool enabled=false,active=false,blocked=false,degraded=false,srAvailable=false,srUsed=false,rrIntegrated=false;
    bool renderActive=false,presentSeen=false,fgRequested=false,fgUsed=false,inputsSelected=false,inputsUsed=false,deliveryObserved=false;
};
struct Node {Stage stage;State state;bool ordered=true;};
struct Model {
    std::array<Node,7> nodes{};unsigned count=0;
    Placement placement=Placement::Unknown,requested=Placement::Unknown;
    State nrState=State::Unknown;bool rrIntegrated=false,observedPlacement=false;
};
inline Model Build(const Facts& f) {
    Model m;m.requested=f.requested;m.observedPlacement=f.active&&f.effective!=Placement::Unknown;
    m.placement=m.observedPlacement?f.effective:f.requested;m.rrIntegrated=f.rrIntegrated&&f.srAvailable;
    m.nrState=!f.enabled?State::Off:f.blocked?State::Blocked:f.active?(f.degraded?State::Fallback:State::Used):State::Requested;
    const auto add=[&](Stage stage,State state,bool ordered=true){m.nodes[m.count++]={stage,state,ordered};};
    const auto nr=[&]{const bool ordered=m.placement!=Placement::Unknown;
        add(Stage::Inputs,!f.enabled?State::Off:f.inputsUsed&&f.active?State::Used:f.inputsSelected?State::Detected:State::Unknown,ordered);
        add(Stage::Nr,m.nrState,ordered);};
    const auto fg=[&]{add(Stage::Fg,!f.fgRequested?State::Off:f.fgUsed&&f.fgOrder!=FgOrder::Unknown?State::Used:
        f.fgUsed?State::Detected:State::Requested,f.fgOrder!=FgOrder::Unknown||!f.fgRequested);};
    add(Stage::Render,f.renderActive?State::Used:State::Unknown);
    if(m.placement==Placement::BeforeSr)nr();
    add(Stage::Sr,f.srUsed?State::Used:f.srAvailable?State::Detected:State::Unknown);
    if(m.placement==Placement::AfterSr)nr();
    if(f.fgOrder==FgOrder::BeforePresent)fg();
    add(Stage::Present,f.presentSeen?State::Used:State::Unknown);
    if(m.placement!=Placement::BeforeSr&&m.placement!=Placement::AfterSr)nr();
    if(f.fgOrder!=FgOrder::BeforePresent)fg();
    add(Stage::Delivery,f.enabled&&f.active&&!f.blocked?State::Used:!f.blocked&&f.deliveryObserved?State::Detected:State::Unknown);
    return m;
}
struct SignalEvidence {
    int preferred=0;uint32_t selected=0,used=0;
    bool selectedFresh=false,usedFresh=false,enabled=false,outputActive=false,blocked=false,rejected=false;
};
struct SignalView {int preferred=0;uint32_t selected=0,used=0;State state=State::Unknown;};
inline SignalView Project(const SignalEvidence& e) {
    SignalView v;v.preferred=e.preferred;v.selected=e.selectedFresh?e.selected:0;
    v.used=e.enabled&&e.outputActive&&e.usedFresh?e.used:0;
    v.state=!e.enabled?State::Off:e.blocked?State::Blocked:v.used?
        e.preferred!=0&&e.preferred!=int(v.used)?State::Fallback:State::Used:
        e.rejected?State::Fallback:v.selected?State::Detected:State::Unknown;
    return v;
}
}
