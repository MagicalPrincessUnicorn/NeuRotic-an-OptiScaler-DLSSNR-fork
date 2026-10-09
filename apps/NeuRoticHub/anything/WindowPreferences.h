#include <menu/Localization.h>
#pragma once
#include <cstdint>
#include <cmath>
#include <string>
#include <vector>
#include <json.hpp>
namespace nh {
struct NrPreferences {
 uint64_t revision=0;
 float transferStrength=1,colourStrength=1,comparison=0;
 uint32_t nrScalePercent=100;
 bool overlay=true;
 // Unsupported controls are reported, never approximated through other fields.
 bool requestsDepth=false,requestsMotion=false,requestsCamera=false,requestsSr=false,requestsFg=false;
};
struct WorkerCapabilities { bool nrResolutionScale=false; };
struct WindowOptionResult { bool accepted=false;uint64_t revision=0;nlohmann::json options;std::vector<std::string> applied,unsupported;std::string reason; };
inline WindowOptionResult MapNrPreferences(const NrPreferences& p,const WorkerCapabilities& c) {
 WindowOptionResult r;r.revision=p.revision;
 if(!std::isfinite(p.transferStrength)||!std::isfinite(p.colourStrength)||!std::isfinite(p.comparison)||
    p.transferStrength<0||p.transferStrength>4||p.colourStrength<0||p.colourStrength>4||p.comparison<0||p.comparison>1||
    p.nrScalePercent<25||p.nrScalePercent>100){r.reason=Neurotic::UiLiteral("desktop.windowpreferences.window_preferences_are_outside_supported_ranges_42c33311", "Window preferences are outside supported ranges");return r;}
 if(p.nrScalePercent!=100&&!c.nrResolutionScale){r.reason=Neurotic::UiLiteral("desktop.windowpreferences.worker_does_not_support_nr_resolution_scale_89dedc87", "Worker does not support NR resolution scale");return r;}
 for(const auto& item:{std::pair{p.requestsDepth,"depth"},std::pair{p.requestsMotion,"motion"},std::pair{p.requestsCamera,"camera"},std::pair{p.requestsSr,"SR"},std::pair{p.requestsFg,"FG"}})if(item.first)r.unsupported.emplace_back(item.second);
 if(!r.unsupported.empty()){r.reason=Neurotic::UiLiteral("desktop.windowpreferences.window_mode_supports_image_only_nr_requested_gui_8199450f", "Window mode supports image-only NR; requested guides/SR/FG remain unsupported");return r;}
 r.options={{"guides","off"},{"transferStrength",p.transferStrength},{"colourStrength",p.colourStrength},{"split",p.comparison},{"outputMode",p.overlay?"overlay":"preview"}};
 if(c.nrResolutionScale)r.options["nrScalePercent"]=p.nrScalePercent;
 r.applied={"transferStrength","colourStrength","comparison","outputMode"};if(c.nrResolutionScale)r.applied.emplace_back("nrScalePercent");
 r.accepted=true;return r;
}
inline nlohmann::json ProjectWorkerStatus(const nlohmann::json& snapshot) {
 // Preserve worker source incarnation/session and dimensions as provided. This
 // serializer never upgrades Present or capture success to display observation.
 nlohmann::json result={{"source","Window"},{"guideMode","image-only"},{"displayObserved",nullptr}};
 for(const char* field:{"phase","session","window","target","frame","captureWidth","captureHeight",Neurotic::UiLiteral("desktop.anythingview.workwidth_d258bdc6", "workWidth"),Neurotic::UiLiteral("desktop.anythingview.workheight_6220a8cc", "workHeight"),"outputMode","requestedRevision","appliedRevision","restartRequired",Neurotic::UiLiteral("desktop.anythingview.reason_adbde5fa", "reason")})
  if(snapshot.contains(field))result[field]=snapshot[field];
 return result;
}
}
