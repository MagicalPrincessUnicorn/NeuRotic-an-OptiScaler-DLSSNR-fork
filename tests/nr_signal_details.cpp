#include "dlssnr/NrSignalDetails.h"
#include <cassert>
#include <limits>
#include <cstdio>
using namespace DlssNr;
static std::string Value(const SignalDetails::Panel& panel,const char* label){
    for(const auto& row:panel.values)if(std::string(row.label)==label)return row.value;
    return {};
}
int main(){
    using S=SignalReceipts::Signal;using R=SignalReceipts::State;
    SignalReceipts::Reading r;r.state=R::Consumed;r.sequence=6821;r.age=16;
    r.fact.present=r.fact.admitted=true;r.epoch.route=2;r.epoch.revision=339;
    // Josh's screenshot: completed evaluation without texture metadata.
    auto panel=SignalDetails::Receipt(S::Evaluation,r);
    assert(Value(panel,"GPU completion")=="Confirmed");
    assert(Value(panel,"Image size").empty()&&Value(panel,"Work size").empty());
    assert(Value(panel,"Color encoding").empty()&&Value(panel,"Processing stage").empty());
    assert(panel.technical.size()==5);
    auto color=SignalDetails::Receipt(S::Color,r);
    assert(Value(color,"Image size")=="Not reported");
    r.fact.width=1920;r.fact.height=1080;r.fact.stage=SignalReceipts::Stage::PostSr;
    r.fact.domain=SignalReceipts::Domain::LinearHdr;
    color=SignalDetails::Receipt(S::Color,r);
    assert(Value(color,"Image size")=="1920 x 1080");
    assert(Value(color,"Processing stage")=="After upscaling");
    for(auto state:{R::Stale,R::Off,R::NotApplicable,R::Blocked,R::Unavailable}){
        r.state=state;
        assert(Value(SignalDetails::Receipt(S::Color,r),"Image size")=="Not reported");
        assert(Value(SignalDetails::Receipt(S::Evaluation,r),"GPU completion")=="Not confirmed");
    }
    r.state=R::Consumed;r.age=2001;
    assert(Value(SignalDetails::Receipt(S::History,r),"History reuse")=="Not confirmed");
    r.age=16;panel=SignalDetails::Receipt(S::Evaluation,r,false);
    assert(Value(panel,"GPU completion")=="Not confirmed"&&panel.technical.empty());
    r.fact.valuesKnown=true;r.fact.x=std::numeric_limits<float>::infinity();
    assert(Value(SignalDetails::Receipt(S::PreExposure,r),"Multiplier")=="Not reported");
    for(int i=0;i<21;++i)assert(std::string(SignalDetails::Description(i))!="No details available.");
    assert(Value(SignalDetails::Receipt(S::Delivery,r),"Display")=="Not independently verified");
    puts("PASS signal detail projection: completed evaluation, applicable fields, missing metadata, stale/off/refused/other-route values, all 21 descriptions");
}
