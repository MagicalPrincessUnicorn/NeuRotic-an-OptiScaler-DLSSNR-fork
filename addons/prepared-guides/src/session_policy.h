#pragma once
#include <cstdint>
namespace nrpg
{
inline bool SdrAllowed(unsigned colorSpace) { return colorSpace==1; }
inline bool MayReuse(bool privateComplete,bool sourceReturned,bool healthy)
{return privateComplete && sourceReturned && healthy;}
struct Episode
{
    std::uint32_t generation=1,capture=0,previous=0,frame=0,previousFrame=0;
    bool begun=false,provider=false,history=false,quarantined=false;
    bool Begin(std::uint32_t next)
    {
        if(quarantined || next>16777214u || capture>=16777214u || generation>16777214u ||
            (begun && next==frame))return false;
        if(begun && next!=frame+1)Drop();
        history=previous>0 && previousFrame+1==next;
        frame=next;++capture;begun=true;provider=false;return true;
    }
    void Provider(){if(begun && !quarantined)provider=true;}
    bool Armed()const{return begun && provider && !quarantined;}
    bool History()const{return Armed() && history;}
    void Delivered(){if(Armed()){previous=capture;previousFrame=frame;}}
    void Drop(){history=false;previous=0;previousFrame=0;++generation;}
    void Quarantine(){quarantined=true;provider=false;Drop();}
};
}
