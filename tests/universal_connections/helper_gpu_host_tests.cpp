#include "../../addons/prepared-guides/helper/PreparedGpuProtocol.h"
#include <cassert>
#include <iostream>
using namespace nrpg::gpu;
int main(){
    Hello h;h.nonce={1,2};h.adapterLuid=9;h.processId=17;h.processBits=32;Hello decoded;
    auto b=Encode(h);assert(b.size()==80&&Decode(b,decoded)&&decoded.nonce==h.nonce);
    for(std::size_t i=0;i<b.size();++i)assert(!Decode(std::span<const std::uint8_t>(b).first(i),decoded));
    auto invalid=b;invalid.push_back(0);assert(!Decode(invalid,decoded));
    for(auto offset:{0,4,6,8,12,60}){invalid=b;invalid[offset]^=0xff;assert(!Decode(invalid,decoded));}
    GpuRegistration reg;reg.nonce=h.nonce;reg.identity={1,2,3,4,5,0};reg.id=3;reg.kind=ObjectKind::Texture;reg.handle=0xfedcba9876543210ULL;reg.width=256;reg.height=64;reg.format=28;reg.mips=reg.samples=reg.arraySize=1;reg.access=0x10000000;
    GpuRegistration rd;auto rb=Encode(reg);assert(rb.size()==144&&Get(std::span<const std::uint8_t>(rb).subspan(16),72)==reg.handle&&Decode(rb,rd)&&rd.handle==reg.handle);
    invalid=rb;invalid[143]=1;assert(!Decode(invalid,rd));
    GpuFrameRequest f;f.ticket={{1,2,3,4,5,6},0,3,7,8,9,{1,2}};auto&d=f.description;d.producer=1;d.session=2;d.stream=3;d.device=4;d.generation=5;d.capture=d.maskCapture=6;d.flags=P::Reset|P::HasDistrust|P::ZeroJitterPolicy;d.color=d.depth=d.motion=d.distrust={256,64,0,0,256,64};f.registrations={3,4,5,6,7};
    auto fb=Encode(f);GpuFrameRequest fd;assert(fb.size()==416&&Decode(fb,fd)&&fd.ticket==f.ticket&&fd.description.maskCapture==6);
    invalid=fb;invalid[16+120+48]=99;assert(!Decode(invalid,fd));invalid=fb;invalid.back()=1;assert(!Decode(invalid,fd));
    invalid=fb;Put(invalid,16+120+232,1,4);assert(!Decode(invalid,fd));
    for(auto offset:{72U,184U,192U,196U,200U,204U,208U,224U,228U}){invalid=fb;Put(invalid,16+120+offset,UINT32_MAX,4);assert(!Decode(invalid,fd));}
    invalid=fb;Put(invalid,16+120+216,0x7fc00000,4);assert(!Decode(invalid,fd));
    invalid=fb;Put(invalid,16+120+56,7);assert(!Decode(invalid,fd));
    invalid=fb;Put(invalid,16+120+64,5);assert(!Decode(invalid,fd));
    GpuFrameResult result;result.ticket=f.ticket;result.outcome=P::Ipc::Outcome::CompletedSuccess;result.facts=15;result.outputCapture=6;result.completedValue=8;GpuFrameResult out;auto encoded=Encode(result);assert(encoded.size()==176&&Decode(encoded,out));result.completedValue=9;assert(!Decode(Encode(result),out));result.completedValue=8;result.facts=8;assert(!Decode(Encode(result),out));
    Command command{f.ticket,3},commandOut;auto release=Encode(command,Kind::Release);assert(release.size()==144&&Decode(release,commandOut,Kind::Release));release.back()=1;assert(!Decode(release,commandOut,Kind::Release));
    T::TicketSequence sequence(f.ticket.identity,f.ticket.nonce);assert(sequence.Accept(f.ticket)&&!sequence.Accept(f.ticket));
    auto next=f.ticket;next.identity.capture++;next.sequence++;next.producerValue++;next.outputValue++;next.nonce[0]++;assert(!sequence.Accept(next));next.nonce=f.ticket.nonce;next.identity.generation++;assert(!sequence.Accept(next));next.identity.generation=f.ticket.identity.generation;assert(sequence.Accept(next));
    auto request=reg;request.direction=Direction::HostToProducer;request.handle=0;assert(Decode(Encode(request),rd));request.direction=Direction::ProducerToHost;assert(!Decode(Encode(request),rd));
    std::cout<<"PASS NRH2 envelope/descriptor/registration/ticket/receipt malformed and golden-size tests "<<sizeof(void*)*8<<" bit\n";
}
