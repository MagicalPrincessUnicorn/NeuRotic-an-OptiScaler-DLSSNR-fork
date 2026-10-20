#include <dlssnr/PreparedContentProbe.h>
#include <cassert>
#include <iostream>
#include <vector>
using namespace DlssNr::PreparedContent;
namespace P=Neurotic::Feed::Prepared;
int main(){
    P::Descriptor d{};d.producer=1;d.session=2;d.stream=3;d.device=4;d.generation=5;d.capture=6;
    Bindings bindings{};for(auto& r:bindings)r={4,5,100};
    std::vector<std::byte> pixels(16*8*4),model(pixels.size());
    View view{pixels,64,16,8,4,1,true};
    Probe probe;assert(probe.Begin(d,bindings,Mode::TransportOnlyPattern).accepted);
    assert(FillPattern(IdentityOf(d),pixels,64,16,8,4));Observation o{IdentityOf(d),Stage::Capture,bindings[0],0,view,Evidence::CompletedReadback};
    auto captured=probe.Observe(o);assert(captured.accepted&&captured.sampleBytes<=MaxSampleBytes);
    o.stage=Stage::Processed;assert(probe.Observe(o).code==Code::StageOrder);
    o.stage=Stage::Prepared;assert(probe.Observe(o).accepted);
    o.stage=Stage::Processed;assert(probe.Observe(o).accepted);
    o.stage=Stage::CopyBack;o.resource.device=9;assert(probe.Observe(o).code==Code::BindingMismatch);
    o.resource=bindings[3];pixels[0]^=std::byte{1};assert(!probe.Observe(o).accepted);pixels[0]^=std::byte{1};
    assert(probe.Observe(o).accepted);o.stage=Stage::PresentAttempt;
    auto attempt=probe.Observe(o);assert(attempt.accepted&&!attempt.displayObserved&&attempt.code==Code::PresentAttemptOnly);
    o.stage=Stage::ObservedDisplay;assert(probe.Observe(o).code==Code::InvalidEvidence);
    o.evidence=Evidence::DisplayObservation;assert(probe.Observe(o).displayObserved);
    assert(probe.Begin(d,bindings,Mode::Processed).code==Code::StaleCapture);
    d.capture++;d.flags|=P::HasDistrust;d.maskCapture=d.capture-1;
    assert(probe.Begin(d,bindings,Mode::Processed).code==Code::StaleMask);
    d.flags&=~P::HasDistrust;d.maskCapture=0;
    assert(probe.Begin(d,bindings,Mode::Processed).accepted);
    o={IdentityOf(d),Stage::Capture,bindings[0],0,view,Evidence::CompletedReadback};o.view.completed=false;
    assert(probe.Observe(o).code==Code::InvalidSample);o.view.completed=true;o.view.rowPitch=1;
    assert(probe.Observe(o).code==Code::InvalidSample);o.view=view;o.identity.capture--;
    assert(probe.Observe(o).code==Code::StaleCapture);o.identity=IdentityOf(d);
    assert(probe.Observe(o).accepted);o.stage=Stage::Prepared;assert(probe.Observe(o).accepted);
    model=pixels;for(auto& b:model)b^=std::byte{55};o.stage=Stage::Processed;o.view.bytes=model;
    assert(probe.Observe(o).accepted); // model output need not equal input
    o.stage=Stage::CopyBack;assert(probe.Observe(o).accepted);
    d.capture++;assert(probe.Begin(d,bindings,Mode::Processed).accepted);
    pixels[0]^=std::byte{2};o={IdentityOf(d),Stage::Capture,bindings[0],0,view,Evidence::CompletedReadback};
    assert(probe.Observe(o).accepted);o.stage=Stage::Prepared;assert(probe.Observe(o).accepted);
    o.stage=Stage::Processed;o.view.bytes=model;auto stale=probe.Observe(o);
    assert(stale.accepted&&stale.code==Code::SuspectedStaleOutput&&stale.inputChanged&&stale.outputRepeated);
    o.stage=Stage::CopyBack;o.view.bytes=pixels;assert(probe.Observe(o).code==Code::ContentMismatch);
    o.view.bytes=model;o.view.representation=2;assert(probe.Observe(o).code==Code::RepresentationMismatch);
    d.capture++;d.flags|=P::HasDistrust;d.maskCapture=d.capture;
    auto noDisplay=bindings;noDisplay[5]={};assert(probe.Begin(d,noDisplay,Mode::Processed).accepted);
    o={IdentityOf(d),Stage::Capture,bindings[0],d.capture-1,view,Evidence::CompletedReadback};
    assert(probe.Observe(o).code==Code::StaleMask);o.maskCapture=d.capture;
    o.identity.device++;assert(probe.Observe(o).code==Code::IdentityMismatch);o.identity.device--;
    o.view.rowPitch=UINT64_MAX;assert(probe.Observe(o).code==Code::InvalidSample);o.view=view;
    assert(probe.Observe(o).accepted);
    std::cout<<"prepared bounded content probe checks passed\n";
}
