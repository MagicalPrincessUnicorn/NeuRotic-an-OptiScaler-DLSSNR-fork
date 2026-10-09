#pragma once

namespace DlssNr::Bridge
{
// The DX11 bridge owns only the primary void* slot it wrote. Typed or alias
// slots on the game's original NGX block may contain unrelated resources.
template<class Resource,class Parameters,class Result>
Resource* ReadResource(Parameters* parameters,const char* primary,const char* alias,
                       Result success,bool bridge)
{
    if (!parameters || !primary) return nullptr;
    if (bridge)
    {
        void* value=nullptr;
        return parameters->Get(primary,&value)==success ? static_cast<Resource*>(value) : nullptr;
    }
    Resource* value=nullptr;
    if (parameters->Get(primary,&value)==success && value) return value;
    value=nullptr;
    if (alias && parameters->Get(alias,&value)==success && value) return value;
    void* pointer=nullptr;
    if (parameters->Get(primary,&pointer)==success && pointer) return static_cast<Resource*>(pointer);
    pointer=nullptr;
    if (alias && parameters->Get(alias,&pointer)==success && pointer) return static_cast<Resource*>(pointer);
    return nullptr;
}
}
