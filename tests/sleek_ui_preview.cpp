// Offline component preview: production theme, navigation, stage adapters and native widgets.
// No game hooks or provider are loaded; example configuration is local to this executable.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui/imgui_impl_dx11.h>
#include "menu/SleekTheme.h"
#include "menu/SleekWater.h"
#include "menu/SleekSections.h"
#include "menu/SleekContentCard.h"
#include "menu/MenuHeightControl.h"
#include "dlssnr/DlssNr_StageControls.h"
#include "nr/semantic/character/CharacterInspectorMenu.h"
#include <filesystem>
#include <fstream>
#include <vector>
#include <stdexcept>
#include <cstdio>
#include <chrono>
using Microsoft::WRL::ComPtr;

template<class T> struct Value
{
    T v; bool explicitValue=false;
    T value_or_default() const { return v; }
    bool has_value() const { return explicitValue; }
};
struct Theme
{
    Value<bool> LightTheme{false};
    Value<float> MenuAccentColorR{.54f},MenuAccentColorG{.54f},MenuAccentColorB{.54f};
    Value<float> MenuBGColorR{0},MenuBGColorG{0},MenuBGColorB{0},MenuBGColorA{.95f};
};
struct StageFixture
{
    NrOptional<uint32_t> DlssNrRoute{2},DlssNrUiAfterMethod{0},DlssNrPresentInputPolicy{0};
    NrOptional<uint32_t> DlssNrUiResolutionPreset{0},DlssNrUiPresentResolutionPreset{0},DlssNrUiEnhancedResolutionPreset{0};
    NrOptional<float> DlssNrUiPresentManualScale{.25f},DlssNrUiEnhancedManualScale{.25f};
    NrOptional<int32_t> DlssNrRenderingMode{1};
    NrOptional<bool> DlssNrRunBeforeSr{false},DlssNrUiManualResolution{false};
    NrOptional<float> DlssNrWorkingScale{1},DlssNrUiManualScale{1};
    NrOptional<uint32_t> DlssNrPresentResolution{1},DlssNrPresentCustomScale{0},DlssNrEnhancedResolution{0},DlssNrEnhancedCustomScale{0};
    StageFixture GetDlssNrConfigSnapshot() const { return *this; }
};
namespace Neurotic::Semantic::Character {
void StopCharacterWorker() noexcept {}
void StartCharacterWorkerAtDefaultLocation() {}
RuntimeView TryCharacterRuntimeView(){return {};}
bool CharacterWorkerRequested() noexcept{return false;}
}
struct InspectorFixture {
    CustomOptional<bool> CharacterInspectorEnabled { false };
    CustomOptional<std::string> CharacterInspectorProvider { "auto" };
    CustomOptional<bool> CharacterInspectorBodyBoxes { true };
    CustomOptional<bool> CharacterInspectorTorsoEstimate { false };
    CustomOptional<uint32_t> CharacterInspectorMaximumPersons { 4 };
    CustomOptional<uint32_t> CharacterInspectorMaximumLabels { 8 };
    CustomOptional<uint32_t> CharacterInspectorBoxHoldMs { 80 };
    CustomOptional<uint32_t> CharacterInspectorBoxThickness { 2 };
    CustomOptional<uint32_t> CharacterInspectorLabelScalePercent { 100 };
    CustomOptional<uint32_t> CharacterInspectorUpdateIntervalMs { 33 };
    CustomOptional<bool> CharacterInspectorAutoLabelScale { true };
    CustomOptional<bool> CharacterInspectorSmartBoxHandoff { true };
    CustomOptional<bool> CharacterInspectorDetectObjects { true };
};

template<class F> bool Card(const char* label,F contents,bool open=true,ImVec2* heading=nullptr)
{
    if(heading) *heading=ImGui::GetCursorScreenPos()+ImVec2(100,ImGui::GetFrameHeight()*.5f);
    auto card=ScopedCollapsingHeader(label,open?ImGuiTreeNodeFlags_DefaultOpen:0);
    const bool visible=card.IsHeaderOpen();
    if(visible) contents();
    return visible;
}
static void Require(HRESULT result) { if(FAILED(result)) throw std::runtime_error("DX11 preview operation failed"); }
static void SaveBmp(const std::filesystem::path& path,D3D11_MAPPED_SUBRESOURCE data,int width,int height)
{
    BITMAPFILEHEADER file{};BITMAPINFOHEADER info{};
    file.bfType=0x4d42;file.bfOffBits=sizeof(file)+sizeof(info);file.bfSize=file.bfOffBits+width*height*4;
    info.biSize=sizeof(info);info.biWidth=width;info.biHeight=-height;info.biPlanes=1;info.biBitCount=32;
    std::ofstream out(path,std::ios::binary);out.write((char*)&file,sizeof(file));out.write((char*)&info,sizeof(info));
    std::vector<unsigned char> row(width*4);
    for(int y=0;y<height;++y) {
        auto* pixels=(unsigned char*)data.pData+y*data.RowPitch;
        for(int x=0;x<width;++x) { row[x*4]=pixels[x*4+2];row[x*4+1]=pixels[x*4+1];row[x*4+2]=pixels[x*4];row[x*4+3]=255; }
        out.write((char*)row.data(),row.size());
    }
    if(!out) throw std::runtime_error("Preview frame could not be saved");
}
int main(int argc,char** argv)
{
    try {
        const std::filesystem::path output=argc>1?argv[1]:"builds/ui-release/sleek/frames";
        std::filesystem::create_directories(output);
        const std::string previewMode=argc>2?argv[2]:"";
        const bool fidelity=previewMode.starts_with("--fidelity");
        const int width=previewMode=="--fidelity-compact"?640:1200,height=1100;
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
        Require(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context));
        D3D11_TEXTURE2D_DESC desc{};desc.Width=width;desc.Height=height;desc.MipLevels=desc.ArraySize=1;
        desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.BindFlags=D3D11_BIND_RENDER_TARGET;
        ComPtr<ID3D11Texture2D> target,staging;Require(device->CreateTexture2D(&desc,nullptr,&target));
        ComPtr<ID3D11RenderTargetView> view;Require(device->CreateRenderTargetView(target.Get(),nullptr,&view));
        desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        Require(device->CreateTexture2D(&desc,nullptr,&staging));
        ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=io.LogFilename=nullptr;
        io.DisplaySize={float(width),float(height)};io.DeltaTime=1.0f/60;io.MouseDrawCursor=true;
        io.FontDefault=Neurotic::AddInterfaceFont(io.Fonts,16);Neurotic::AddLanguageFonts(io.Fonts,16);
        if(!ImGui_ImplDX11_Init(device.Get(),context.Get())) throw std::runtime_error("Preview renderer initialization failed");
        Neurotic::Sleek::WaterTransition water;Neurotic::Sleek::WaterColors waterColors;
        const bool inspectorPreview=argc>2 && std::string(argv[2])=="--inspector";
        const bool preflightPreview=argc>2 && std::string(argv[2])=="--preflight";
        const bool focusedPreview=inspectorPreview||preflightPreview||fidelity;
        InspectorFixture inspector;ImVec2 inspectorPoint{};
        const bool waterCycle=argc>2 && std::string(argv[2])=="--water-cycle";
        Theme theme;StageFixture config;bool enabled=false,apply=true,multipass=false,mouse=false,keyboard=false,controller=false;
        if(previewMode=="--fidelity-light")theme.LightTheme.v=true;
        Neurotic::Sleek::ApplyMetrics(ImGui::GetStyle());
        bool graphs=false,reduced=false;float strength=.65f;int page=preflightPreview?1:2,menuScale=0,language=0,neuralPage=0,generalChild=0;
        Neurotic::Sleek::MenuHeightControl menuHeight;
        constexpr float scale=1.2f; ImGui::GetStyle().ScaleAllSizes(scale);
        ImVec2 togglePoint{},savePoint{},generalPoint{},neuralPoint{},lightPoint{},comboPoint{},themeHeader{},copyPoint{},optionPoint{},advancedPoint{},supportPoint{};
        bool copied=false,saved=false,supported=false,advancedCollapsed=false,advancedReopened=false;
        double waterMilliseconds=0,waterMaximum=0;int waterFrames=0;
        for(int frame=0;frame<(waterCycle?720:480);++frame)
        {
            if(frame==200)
            {
                ImGui_ImplDX11_InvalidateDeviceObjects();
                if(GImGui->UserTextures.Size!=1 || GImGui->UserTextures[0]->Status!=ImTextureStatus_Destroyed ||
                   GImGui->UserTextures[0]->BackendUserData!=nullptr)
                    throw std::runtime_error("Official logo did not release its renderer texture on invalidation");
            }
            if(waterCycle) {
                if(frame==480 || frame==600) theme.LightTheme.v=false;
                if(frame==570 || frame==630) theme.LightTheme.v=true;
                if(frame==700) reduced=true;
            }
            ImVec2 pointer{-100,-100};
            if(frame>=25&&frame<65) pointer=togglePoint;
            if(frame>=65&&frame<100) pointer=comboPoint;
            if(frame>=100&&frame<110) pointer=optionPoint;
            if(frame>=110&&frame<145) pointer=copyPoint;
            if(frame>=145&&frame<170) pointer=generalPoint;
            if(frame>=170&&frame<240) pointer=themeHeader;
            if(frame>=240&&frame<290) pointer=lightPoint;
            if(frame>=290&&frame<325) pointer=themeHeader;
            if(frame>=325) pointer=neuralPoint;
            if(frame>=338) pointer=advancedPoint;
            if(frame>=365&&frame<385) pointer=savePoint;
            if(frame>=420&&frame<445) pointer=supportPoint;
            if(focusedPreview) pointer=frame<60?inspectorPoint:ImVec2(-100,-100);
            io.AddMousePosEvent(pointer.x,pointer.y);
            for(int press : {42,78,102,125,150,180,220,260,300,330,340,370,402,425,447}) {
                if(frame==press) io.AddMouseButtonEvent(0,true);
                if(frame==press+3) io.AddMouseButtonEvent(0,false);
            }
            ImGui_ImplDX11_NewFrame();ImGui::NewFrame();
            water.Update(theme.LightTheme.v,reduced,ImGui::GetTime());
            Neurotic::Sleek::ApplyTheme(&theme,water.DrawingLight());
            if(water.Active()) {
                ImVec4 current[ImGuiCol_COUNT],opposite[ImGuiCol_COUNT];
                std::copy_n(ImGui::GetStyle().Colors,ImGuiCol_COUNT,current);
                Neurotic::Sleek::ApplyTheme(&theme,!water.DrawingLight());
                std::copy_n(ImGui::GetStyle().Colors,ImGuiCol_COUNT,opposite);
                std::copy_n(current,ImGuiCol_COUNT,ImGui::GetStyle().Colors);
                waterColors.Set(water.DrawingLight()?opposite:current,water.DrawingLight()?current:opposite,water.DrawingLight());
                Neurotic::Sleek::waterColorCapture=&waterColors;
            }
            Neurotic::Sleek::Scope scope(reduced);
            ImGui::PushFont(nullptr,16*scale);
            const auto previewSize=menuHeight.Size(io.DisplaySize,scale);
            ImGui::SetNextWindowPos(Neurotic::Sleek::WindowPosition({36,24},{0,0},io.DisplaySize,previewSize));ImGui::SetNextWindowSize(previewSize);
            ImGui::Begin("NeuRotic  |  Interface preview###Preview",nullptr,ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoCollapse|Neurotic::Sleek::FixedShellFlags);
            auto* waterRoot=ImGui::GetCurrentWindow();
            const auto head=ImGui::GetCursorScreenPos();const float edge=head.x+ImGui::GetContentRegionAvail().x;
            const float side=ImGui::GetFrameHeight()*1.25f,gap=ImGui::GetStyle().ItemSpacing.x;
            const float saveWidth=Neurotic::Sleek::ButtonWidth("Save Settings")+ImGui::GetFontSize()*2;
            lightPoint={edge-side*2-gap*2-saveWidth+side*.5f,head.y+side*.5f};
            savePoint={edge-side-gap-saveWidth*.5f,head.y+side*.5f};
            const auto header=Neurotic::Sleek::HeaderActions(theme.LightTheme.v,nullptr,&graphs);
            if(header.saveClicked) { saved=true;Neurotic::Sleek::CompleteButtonFeedback(header.saveId,true); }
            Neurotic::Sleek::Brand("Offline component preview",Neurotic::Sleek::HeaderActionsWidth(true,false)+gap);
            ImGui::Spacing();ImGui::Separator();ImGui::Spacing();
            if(frame<60 && !fidelity) {
                const Neurotic::Sleek::DetectedSource sources[]={{"nvngx_dlss",true},{"nvngx_dlssd",false},{"libxess",true},
                    {"FSR Hooks",true},{"FSR 3.1",false},{"FSR 3.1 SR",false},{"FSR 3.1 FG",false}};
                Neurotic::Sleek::UpscalerNotice("Please select DLSS or XeSS as upscaler from game options and load a save game to enable Opti settings. Upscalers don't always work in menus.",sources,7);
            }
            const auto navStart=ImGui::GetCursorScreenPos();
            const float cell=(ImGui::GetContentRegionAvail().x-ImGui::GetStyle().ItemSpacing.x*6)/7;
            generalPoint=navStart+ImVec2(cell*.5f,ImGui::GetFontSize()*1.35f);
            neuralPoint=navStart+ImVec2(2*(cell+ImGui::GetStyle().ItemSpacing.x)+cell*.5f,ImGui::GetFontSize()*1.35f);
            Neurotic::Sleek::NavigationStatus navigationStatus[Neurotic::Sleek::PageCount]{};
            navigationStatus[1]=Neurotic::Sleek::UpscalingNavigationStatus(false,false,false);
            navigationStatus[2].running=!fidelity;
            navigationStatus[3]=Neurotic::Sleek::FrameGenerationNavigationStatus(false,false,false,false);
            Neurotic::Sleek::Navigation(page,650,false,navigationStatus);
            if(page==2) {
                Neurotic::Sleek::NeuralNavigation(neuralPage,!fidelity,frame>=450&&!fidelity,frame>=450&&!fidelity,inspector.CharacterInspectorEnabled.value_or_default(),fidelity);
                auto* bar=GImGui->TabBars.GetByKey(ImGui::GetID("##NeuralPages"));
                if(bar && bar->Tabs.Size==6) inspectorPoint={bar->BarRect.Min.x+bar->Tabs[4].Offset+bar->Tabs[4].Width*.5f,bar->BarRect.GetCenter().y};
            }
            else if(page==0 || page==1) {
                Neurotic::Sleek::SectionNavigation(page,generalChild);
                auto* tabBar=GImGui->TabBars.GetByKey(ImGui::GetID("##SectionPages"));
                if(preflightPreview && tabBar && tabBar->Tabs.Size==6) inspectorPoint={tabBar->BarRect.Min.x+tabBar->Tabs[0].Offset+tabBar->Tabs[0].Width*.5f,tabBar->BarRect.GetCenter().y};
                if(tabBar && tabBar->Tabs.Size==5) themeHeader={tabBar->BarRect.Min.x+tabBar->Tabs[3].Offset+tabBar->Tabs[3].Width*.5f,tabBar->BarRect.GetCenter().y};
            }
            const float pageHeight=ImGui::GetContentRegionAvail().y-Neurotic::Sleek::FooterHeight(true)-menuHeight.RowHeight()-ImGui::GetStyle().ItemSpacing.y*2;
            ImGui::PushID(page);
            ImGui::PushStyleColor(ImGuiCol_ChildBg,ImVec4(0,0,0,0));
            ImGui::BeginChild("##MainMenuPageContent",{0,pageHeight},ImGuiChildFlags_None);
            ImGui::PopStyleColor();
            if(frame>=348 && frame<395 && !fidelity) ImGui::SetScrollY(ImGui::GetScrollMaxY());
            if(frame==395) ImGui::SetScrollY(0);
            {
                Neurotic::Sleek::PageReveal reveal(page);
                if(fidelity) {
                    // Production card, navigation, theme, switch, slider and stage controls.
                    // Values are inert examples; this preview does not load a game or provider.
                    using namespace Neurotic::Sleek;
                    ImGui::TextDisabled("Neural Rendering is off.");
                    {
                        CardColumns columns("##NrMainCards");
                        {
                            ContentCard rendering("##NrRenderingCard","Rendering");
                            ImGui::Checkbox("Enable Neural Rendering",&enabled);
                            ImGui::Checkbox("Apply the model",&apply);
                            ControlDivider();
                            DlssNr::StageUi::RenderControls(config,false,false,false,true);
                            int style=0,preset=0;const char* styles[]={"Standard","Natural","Cinematic"};
                            const char* presets[]={"Default","Preset 1","Preset 2","Preset 3"};
                            ControlLabel("Style");ImGui::Combo("##NrStyle",&style,styles,3);
                            ControlLabel("Preset");ImGui::Combo("##NrModelPreset",&preset,presets,4);
                            ImGui::TextDisabled("NR: unavailable | Output: unavailable");
                        }
                        columns.Next();
                        {
                            ContentCard composition("##NrAppearanceCard","Model & composition");
                            for(const char* label:{"Model Strength","Detail Strength","Colour Strength","Local Structure","Local Tone","Skin Structure"}) {
                                ImGui::PushID(label);ControlLabel(label);float value=std::string(label)=="Skin Structure"?-1.f:1.f;
                                const float reset=ImGui::CalcTextSize("Reset").x+ImGui::GetStyle().FramePadding.x*2;
                                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x-reset-ImGui::GetStyle().ItemSpacing.x);
                                CardSliderFloat("##Value",&value,value<0?-1.f:0.f,2.f,"%.2f");ImGui::SameLine();ImGui::SmallButton("Reset");ControlDivider();ImGui::PopID();
                            }
                            bool mask=true;ImGui::Checkbox("Auto skin mask",&mask);
                        }
                    }
                    ImGui::Spacing();ImGui::CollapsingHeader("Advanced Neural Rendering Features");
                    ImGui::CollapsingHeader("Compare");
                }
                else if(preflightPreview && page==1) {
                    const Neurotic::Sleek::DetectedSource sources[]={{"nvngx_dlss",true},{"nvngx_dlssd",false},{"libxess",false},{"FSR Hooks",true},{"FSR 3.1",false},{"FSR 3.1 SR",false},{"FSR 3.1 FG",false}};
                    Neurotic::Sleek::UpscalerNotice("Offline UI fixture: component presence is illustrative. The production panel uses existing game observations.",sources,7);
                }
                else if(page==2 && neuralPage==2) Neurotic::Semantic::Character::RenderInspectorMenu(inspector,false);
                else if(page==2) {
                    DlssNr::MenuControls::EmphasizedCheckbox("Enable Neural Rendering",&enabled,false,true);
                    togglePoint=ImGui::GetItemRectMin()+ImVec2(20,14);
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX()+DlssNr::MenuControls::PrimaryCheckboxPadding());
                    ImGui::Checkbox("Apply the model",&apply);
                    ImGui::Spacing();ImGui::Separator();ImGui::Spacing();
                    {
                        comboPoint=ImGui::GetCursorScreenPos()+ImVec2(80,ImGui::GetFrameHeight()*.5f);
                        DlssNr::StageUi::RenderControls(config);
                        if(GImGui->OpenPopupStack.Size && GImGui->OpenPopupStack.back().Window) {
                            const auto* window=GImGui->OpenPopupStack.back().Window;
                            optionPoint=window->Pos+window->WindowPadding+ImVec2(60,ImGui::GetFontSize()*.5f);
                        }
                        ImGui::TextDisabled("NR: unavailable | Output: unavailable");
                    }
                    int style=2;const char* styles[]={"Standard","Natural","Cinematic"};
                    ImGui::SetNextItemWidth(180);ImGui::Combo("Style",&style,styles,3);
                    const bool advancedVisible=Card("Advanced Neural Rendering Features",[&] {
                        Card("Advanced resampling",[&] {ImGui::TextDisabled("Example controls");},false);
                        ImGui::Spacing();
                        Card("Reversible proxy",[&] {ImGui::TextDisabled("Example controls");},false);
                        ImGui::Spacing();
                        Card("Colour",[&] {ImGui::TextDisabled("Example controls");},false);
                    },false,&advancedPoint);
                    if(frame==440) advancedCollapsed=!advancedVisible;
                    if(frame==479) advancedReopened=advancedVisible;
                    {
                        ImGui::TextUnformatted("Signal Status");
                        const auto actions=Neurotic::Sleek::ObservationActions();
                        if(actions.copyClicked) {copied=true;Neurotic::Sleek::CompleteButtonFeedback(actions.copyId,true);}
                        copyPoint=(ImGui::GetItemRectMin()+ImGui::GetItemRectMax())*.5f;
                        ImGui::TextDisabled("No game owner is connected to this offline preview.");
                    }
                } else if(page==0) {
                    if(generalChild==0) { ImGui::TextUnformatted("NeuRotic");ImGui::TextLink("Open NeuRotic on GitHub"); }
                    if(generalChild==1) ImGui::TextUnformatted("Key Binds");
                    if(generalChild==2) {
                        ImGui::Checkbox("Allow mouse input in game",&mouse);ImGui::Checkbox("Allow keyboard input in game",&keyboard);ImGui::Checkbox("Allow controller input in game",&controller);
                        ImGui::Button("Save Input Settings");
                    }
                    if(generalChild==3) {
                        ImGui::Checkbox("Light Theme",&theme.LightTheme.v);
                        ImGui::Checkbox("Reduce motion",&reduced);
                    }
                    if(generalChild==4) { ImGui::SetNextItemWidth(320);float brightness=1;ImGui::SliderFloat("UI brightness",&brightness,.1f,2,"%.2f"); }
                } else ImGui::TextWrapped("This offline preview demonstrates the shared interface components.");
            }
            ImGui::EndChild();ImGui::PopID();
            menuHeight.Draw(io.DisplaySize,scale,Neurotic::Sleek::FooterHeight(true));
            ImGui::SetCursorScreenPos({ImGui::GetWindowPos().x+ImGui::GetStyle().WindowPadding.x,
                ImGui::GetWindowPos().y+ImGui::GetWindowSize().y-Neurotic::Sleek::FooterBottomInset()-Neurotic::Sleek::FooterHeight(true)});
            const auto footer=Neurotic::Sleek::FooterControls(language,menuScale,"Auto (1.2)");
            supported|=footer.supportClicked;
            const auto footerWindow=ImGui::GetCurrentWindow();
            // Match the production footer's optional prompt when locating the native support button.
            const float promptWidth=ImGui::CalcTextSize("Enjoying NeuRotic?").x;
            const float rightWidth=Neurotic::Sleek::LanguageControlWidth()+Neurotic::Sleek::FieldLabelReserve("Language")+
                Neurotic::Sleek::ScaleControlWidth()+Neurotic::Sleek::FieldLabelReserve("Scale")+gap*2;
            const bool prompt=ImGui::GetContentRegionAvail().x-rightWidth-gap>=Neurotic::Sleek::ButtonWidth("Send Ko-fi")+gap*2+promptWidth;
            supportPoint={footerWindow->Pos.x+ImGui::GetStyle().WindowPadding.x+(prompt?promptWidth+gap:0)+Neurotic::Sleek::ButtonWidth("Send Ko-fi")*.5f,
                footerWindow->Pos.y+footerWindow->Size.y-Neurotic::Sleek::FooterBottomInset()-Neurotic::Sleek::FooterHeight(true)+ImGui::GetFrameHeight()*.5f};
            Neurotic::Sleek::FooterReadout("1920x1080 -> 3840x2160 (2.0) [3840x2160 (2.0)] | 1200 | GPU: Example device");
            ImGui::End();ImGui::PopFont();ImGui::Render();
            Neurotic::Sleek::waterColorCapture=nullptr;
            const auto waterStart=std::chrono::steady_clock::now();
            water.Render(ImGui::GetDrawData(),waterRoot,waterColors);
            if(water.Active()){const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-waterStart).count();waterMilliseconds+=ms;waterMaximum=(std::max)(waterMaximum,ms);++waterFrames;}
            const float clear[]={.025f,.03f,.042f,1};context->ClearRenderTargetView(view.Get(),clear);
            auto* rtv=view.Get();context->OMSetRenderTargets(1,&rtv,nullptr);ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            if(GImGui->UserTextures.Size!=1 || GImGui->UserTextures[0]->Status!=ImTextureStatus_OK ||
               !GImGui->UserTextures[0]->BackendUserData || GImGui->UserTextures[0]->TexID==ImTextureID_Invalid)
                throw std::runtime_error("Official logo was not uploaded by the production renderer");
            if(frame==63 || frame==238 || frame==336 || frame==477) {
                context->CopyResource(staging.Get(),target.Get());D3D11_MAPPED_SUBRESOURCE mapped{};
                Require(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped));
                char name[32];std::snprintf(name,sizeof(name),"frame-%03d.bmp",frame);
                SaveBmp(output/name,mapped,width,height);context->Unmap(staging.Get(),0);
            }
        }
        if(!focusedPreview && (!theme.LightTheme.v || page!=2 || !copied || !saved || !supported || config.DlssNrRoute.value_or_default()!=0)) throw std::runtime_error("Preview interaction script did not complete theme/navigation/copy/save/support/dropdown selection actions");
        if(!focusedPreview && (!advancedCollapsed || !advancedReopened)) throw std::runtime_error("Preview did not complete the section collapse/reopen cycle");
        if(preflightPreview && (page!=1 || generalChild!=5)) throw std::runtime_error("Pre-flight tab did not open");
        if(inspectorPreview && (neuralPage!=2 || inspector.CharacterInspectorEnabled.value_or_default())) throw std::runtime_error("Inspector preview did not preserve disabled settings");
        std::printf("Water CPU transform: %d active frames, %.3f ms mean, %.3f ms maximum\n",waterFrames,waterMilliseconds/(std::max)(1,waterFrames),waterMaximum);
        ImGui_ImplDX11_Shutdown();
        if(GImGui->UserTextures.Size!=1 || GImGui->UserTextures[0]->Status!=ImTextureStatus_Destroyed ||
           GImGui->UserTextures[0]->BackendUserData!=nullptr)
            throw std::runtime_error("Official logo retained its renderer texture after shutdown");
        ImGui::DestroyContext();
        std::puts("PASS: scripted frames rendered through the production DX11 ImGui backend (WARP); official logo upload, reset/reupload and shutdown verified");return 0;
    } catch(const std::exception& e) { std::fprintf(stderr,"FAIL: %s\n",e.what());return 1; }
}
