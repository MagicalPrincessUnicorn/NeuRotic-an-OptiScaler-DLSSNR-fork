#include <dlssnr/NrSignalReceipts.h>
#include <cassert>
#include <cstdio>
using namespace DlssNr::SignalReceipts;
int main() {
    Tracker<unsigned> owner;
    Epoch epoch{1,2,3,4};
    assert(owner.Read(Signal::Color,epoch,100,Mode::Active).state==State::Pending);
    auto frame=owner.Begin(epoch,100);
    Fact color; color.present=true;color.width=256;color.height=64;color.format=28;
    color.stage=Stage::PostSr;color.domain=Domain::EncodedDisplay;
    owner.Observe(frame,Signal::Color,color);
    assert(owner.Read(Signal::Color,epoch,101,Mode::Active).state==State::Detected);
    owner.Reject(frame,Signal::Color,"incompatible device");
    assert(owner.Read(Signal::Color,epoch,101,Mode::Active).state==State::Blocked);
    frame=owner.Begin(epoch,102);owner.Observe(frame,Signal::Color,color);
    owner.Admit(frame,Signal::Color);
    assert(owner.Read(Signal::Color,epoch,103,Mode::Active).state==State::Usable);
    assert(owner.Read(Signal::Exposure,epoch,103,Mode::Active).state==State::Unavailable);
    owner.Record(frame,17,Bit(Signal::Color));
    owner.Poll([](unsigned){return Proof{true,true,false};});
    assert(owner.Read(Signal::Color,epoch,104,Mode::Active).state==State::Pending);
    owner.Poll([](unsigned){return Proof{true,true,true};});
    auto consumed=owner.Read(Signal::Color,epoch,105,Mode::Active);
    assert(consumed.state==State::Consumed&&consumed.fact.width==256);
    assert(owner.Read(Signal::Color,epoch,3000,Mode::Active).state==State::Stale);
    frame=owner.Begin(epoch,3001);owner.Observe(frame,Signal::Color,color);owner.Admit(frame,Signal::Color);
    owner.Record(frame,18,Bit(Signal::Color));
    ++epoch.revision;
    assert(owner.Read(Signal::Color,epoch,3002,Mode::Active).state==State::Pending);
    owner.Poll([](unsigned){return Proof{true,true,true};});
    assert(owner.Read(Signal::Color,epoch,3003,Mode::Active).state==State::Pending);
    frame=owner.Begin(epoch,3004);owner.Observe(frame,Signal::Color,color);owner.Admit(frame,Signal::Color);
    owner.Record(frame,19,Bit(Signal::Color));
    assert(owner.Read(Signal::Color,epoch,3005,Mode::Off).state==State::Off);
    owner.Poll([](unsigned){return Proof{true,true,true};});
    assert(owner.Read(Signal::Color,epoch,3006,Mode::Active).state==State::Pending);
    assert(owner.Read(Signal::Color,epoch,3006,Mode::NotApplicable).state==State::NotApplicable);
    assert(owner.Read(Signal::Color,epoch,3006,Mode::Failed).state==State::Blocked);
    frame=owner.Begin(epoch,3010);owner.Observe(frame,Signal::Color,color);owner.Admit(frame,Signal::Color);
    owner.Record(frame,20,Bit(Signal::Color));
    owner.Poll([](unsigned){return Proof{false,false,false};});
    assert(owner.Read(Signal::Color,epoch,3011,Mode::Active).state==State::Blocked);
    frame=owner.Begin(epoch,3020);owner.Observe(frame,Signal::Color,color);owner.Admit(frame,Signal::Color);
    owner.Record(frame,21,Bit(Signal::Color));
    frame=owner.Begin(epoch,3021);color.width=128;owner.Observe(frame,Signal::Color,color);owner.Admit(frame,Signal::Color);
    owner.Poll([](unsigned){return Proof{true,true,true};});
    assert(owner.Read(Signal::Color,epoch,3022,Mode::Active).state==State::Usable); // old raster completion cannot revive
    // Route identity and exact first-pass argument provenance survive completion.
    epoch.route=2;frame=owner.Begin(epoch,4000);owner.Observe(frame,Signal::Motion,color);
    owner.Values(frame,Signal::Motion,2256.f,1264.f);owner.Admit(frame,Signal::Motion);
    owner.Record(frame,22,Bit(Signal::Motion));owner.Poll([](unsigned){return Proof{true,true,true};});
    auto actual=owner.Read(Signal::Motion,epoch,4001,Mode::Active);
    assert(actual.state==State::Consumed&&actual.fact.valuesKnown&&actual.fact.x==2256.f&&actual.fact.y==1264.f);
    epoch.route=0;assert(owner.Read(Signal::Motion,epoch,4002,Mode::Active).state==State::Pending);
    owner.Poll([](unsigned){return Proof{true,true,true};});
    assert(owner.Read(Signal::Motion,epoch,4003,Mode::Active).state==State::Pending);
    std::puts("PASS receipt lifecycle: observation, admission, completion, optional absence, stale, epoch/off revocation, invalid recording");
}
