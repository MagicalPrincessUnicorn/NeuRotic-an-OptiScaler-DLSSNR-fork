#pragma once
#include <cstdint>
namespace DlssNr::RenderingOutput {
enum class Producer : uint32_t { None, Native, Present, BuiltIn, CapturedImage };
struct Fact {uint64_t generation=0,completed=0;bool ready=false;};
struct Input {
    uint64_t now=0,context=0,sourceFrames=0;
    unsigned route=2;
    bool requested=false,effect=true,live=false,hardFailure=false,unsafe=false,pending=false;
    Fact native,present,builtIn;
};
struct Decision {Producer producer=Producer::None;bool active=false,fallbackEligible=false;uint64_t completed=0;};
class Health {
    struct Progress {
        uint64_t generation=0,count=0,last=0;bool seen=false;
        bool Sample(Fact fact,uint64_t now){
            if(!seen||generation!=fact.generation||fact.completed<count){generation=fact.generation;count=fact.completed;last=0;seen=true;}
            else if(fact.completed>count){count=fact.completed;last=now;}
            return fact.ready&&last&&now>=last&&now-last<=2000;
        }
    } native_,present_,builtIn_;
    uint64_t context_=0;bool initialized_=false;
public:
    static constexpr uint64_t GraceMs=15000,MinimumFrames=30;
    Decision Sample(const Input& in){
        if(!initialized_||context_!=in.context){*this=Health{};initialized_=true;context_=in.context;}
        Decision out;
        const bool native=native_.Sample(in.native,in.now),present=present_.Sample(in.present,in.now),builtIn=builtIn_.Sample(in.builtIn,in.now);
        if(in.requested&&in.effect){
            if(in.route==0&&native){out.producer=Producer::Native;out.completed=in.native.completed;}
            else if((in.route==1||in.route==2)&&builtIn){out.producer=Producer::BuiltIn;out.completed=in.builtIn.completed;}
            else if((in.route==1||in.route==2)&&present){out.producer=Producer::Present;out.completed=in.present.completed;}
        }
        out.active=out.producer!=Producer::None;
        // Captured-image rendering is an explicit third mode. Present failure
        // can never authorize switching to a different producer by itself.
        out.fallbackEligible=in.route==3&&in.requested&&in.effect&&in.live&&!in.hardFailure&&!in.unsafe;
        return out;
    }
};
}
