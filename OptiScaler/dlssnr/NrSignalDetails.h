#pragma once
#include "NrDiagnosticSignals.h"
#include <vector>
#include <cmath>
#include <cstdio>

namespace DlssNr::SignalDetails {
struct Row { const char* label; std::string value; };
struct Panel { std::vector<Row> values, technical; };
inline const char* Description(int signal) {
    static constexpr const char* descriptions[] = {
        "The graphics input reported by the game.",
        "The source image available to the selected rendering method.",
        "The dimensions of the image supplied to Neural Rendering.",
        "The game's presentation buffer.",
        "The color format reported for NR output.",
        "Previous frames help keep rendering stable over time.",
        "Depth helps separate surfaces between frames.",
        "Motion tracks movement between frames.",
        "Small sampling offsets used by the first NR pass.",
        "A texture that describes the game's scene exposure.",
        "The exposure multiplier supplied with the scene.",
        "The image supplied to Neural Rendering.",
        "Activity from the selected rendering method.",
        "Model processing completed by the GPU.",
        "The handoff of NR output to the game.",
        "Ray Reconstruction reported by the game.",
        "Generated-frame activity reported by the selected provider.",
        "Whether the model receives an image without the game UI.",
        "Whether the game UI is protected from model changes.",
        "Whether the game UI is protected in generated frames.",
        "The upscaler and its input and output dimensions."
    };
    return signal>=0&&signal<21?descriptions[signal]:"No details available.";
}
inline const char* Status(SignalReceipts::State state) {
    using S=SignalReceipts::State;
    switch(state){
    case S::Consumed:return "GPU work completed";
    case S::Usable:return "Available";
    case S::Detected:return "Detected";
    case S::Pending:return "Waiting for completion";
    case S::Off:return "Off";
    case S::NotApplicable:return "Not used by this method";
    case S::Stale:return "No recent observation";
    case S::Blocked:return "Refused";
    default:return "Unavailable";
    }
}
inline std::string Extent(uint32_t width,uint32_t height) {
    return width&&height?std::to_string(width)+" x "+std::to_string(height):"Not reported";
}
inline Panel Receipt(SignalReceipts::Signal signal,const SignalReceipts::Reading& r,bool applicable=true) {
    using S=SignalReceipts::Signal;using Stage=SignalReceipts::Stage;using Domain=SignalReceipts::Domain;
    Panel panel;
    // Facts remain diagnostic only; the caller owns status and route applicability.
    // Never present stale/off/other-route values as current, or invent a 0 x 0 image.
    const bool current=applicable&&DiagnosticSignals::Current(r);
    const auto& f=r.fact;
    const bool image=signal==S::Color||signal==S::Output||signal==S::Depth||signal==S::Motion||signal==S::Exposure;
    if(image){
        panel.values.push_back({"Image size",current?Extent(f.width,f.height):"Not reported"});
        if(signal==S::Color||signal==S::Output){
            panel.values.push_back({"Processing stage",!current?"Not reported":f.stage==Stage::PreSr?"Before upscaling":f.stage==Stage::PostSr?"After upscaling":f.stage==Stage::PostRr?"After Ray Reconstruction":f.stage==Stage::PostToneMap?"After tone mapping":"Not reported"});
            panel.values.push_back({"Color encoding",!current?"Not reported":f.domain==Domain::LinearHdr?"Linear HDR":f.domain==Domain::EncodedDisplay?"Display encoded":"Not reported"});
        }
    }
    if(signal==S::PreExposure){
        char value[48]{};
        if(current&&f.valuesKnown&&std::isfinite(f.x))std::snprintf(value,sizeof(value),"%.4g",f.x);
        panel.values.push_back({"Multiplier",value[0]?value:"Not reported"});
    }
    if(signal==S::History)panel.values.push_back({"History reuse",current&&r.state==SignalReceipts::State::Consumed?"Confirmed":"Not confirmed"});
    if(signal==S::Evaluation||signal==S::Delivery){
        panel.values.push_back({"GPU completion",current&&r.state==SignalReceipts::State::Consumed?"Confirmed":"Not confirmed"});
        if(signal==S::Evaluation&&current&&f.width&&f.height)panel.values.push_back({"Work size",Extent(f.width,f.height)});
        if(signal==S::Delivery)panel.values.push_back({"Display", "Not independently verified"});
    }
    panel.values.push_back({"Input validation",current?(f.admitted?"Accepted":"Not confirmed"):"Not reported"});
    // IDs describe the last diagnostic observation, not current-frame authority.
    // Keep rapidly changing counters out of the normal reading path.
    if(applicable&&r.sequence){
        auto add=[&](const char* label,uint64_t value){panel.technical.push_back({label,std::to_string(value)});};
        add("Observation ID",r.sequence);add("Route ID",r.epoch.route);
        add("Lifecycle ID",r.epoch.lifecycle);add("Revision ID",r.epoch.revision);add("Resource revision",r.epoch.resources);
        if(image){
            panel.technical.push_back({"Format ID",f.format?std::to_string(f.format):"Not reported"});
            if(f.width&&f.height){add("Origin X",f.originX);add("Origin Y",f.originY);}
        }
    }
    return panel;
}
}
