// Inert fixture only. Observe the real widgets; no replacement edit behavior.
#include "dlssnr/DlssNrFeature_Dx12.h"
#include "dlssnr/DlssNr_Present.h"
#include "dlssnr/VulkanPresentStatus.h"
#include "dlssnr/VulkanNrPassPlan.h"
namespace MultipassFixture {
struct Item { ImGuiID id{}; ImRect rect{}; ImGuiWindow* window{}; };
static std::unordered_map<std::string,Item> items;
static DlssNr::TelemetrySnapshot native;
static DlssNr::PresentTelemetrySnapshot present;
static DlssNr::VkPresentStatusSnapshot vk;
static DlssNr::VkNrChainResult chain;
static std::string text;
static std::unordered_map<std::string,std::string> smallCaptions;
static std::vector<std::string> presetCaptions;
struct Heading { std::string full, shown; ImRect rect; float right; bool clipped; };
static std::vector<Heading> headings;
static std::string headingTooltip;
static void Remember(const char* label) {
    items[label]={GImGui->LastItemData.ID,GImGui->LastItemData.Rect,ImGui::GetCurrentWindow()};
}
}
namespace ImGui {
static bool MultipassFixtureSmallButton(const char* label) {
    MultipassFixture::smallCaptions[label]=Neurotic::Translate(label);
    const bool changed=SmallButton(label);MultipassFixture::Remember(label);return changed;
}
static bool MultipassFixtureCombo(const char* label,int* value,const char* const* options,int count,int height=-1) {
    if(count==4&&std::string_view(options[1])=="Preset 1"){
        MultipassFixture::presetCaptions.clear();for(int i=1;i<4;++i)MultipassFixture::presetCaptions.push_back(Neurotic::Translate(options[i]));
    }
    return Combo(label,value,options,count,height);
}
static bool MultipassFixtureCombo(const char* label,int* value,const char* options,int height=-1) {return Combo(label,value,options,height);}
static void MultipassFixtureTextWrapped(const char* format,...) {
    va_list args;va_start(args,format);char buffer[2048];va_list copy;va_copy(copy,args);
    vsnprintf(buffer,sizeof(buffer),format,copy);va_end(copy);MultipassFixture::text=buffer;
    TextWrappedV(format,args);va_end(args);
}
static bool MultipassFixtureSliderInt(const char* label,int* value,int lo,int hi,const char* format="%d",ImGuiSliderFlags flags=0) {
    const bool changed=SliderInt(label,value,lo,hi,format,flags);MultipassFixture::Remember(label);return changed;
}
static bool MultipassFixtureButton(const char* label,const ImVec2& size=ImVec2(0,0)) {
    const bool changed=Button(label,size);MultipassFixture::Remember(label);return changed;
}
}
