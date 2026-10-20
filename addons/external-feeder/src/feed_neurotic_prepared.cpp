// Capture-only prepared consumer for DLSS5-Feeder v1.18.0-beta.1.
// Provider names/texture conventions adapted from Jean-Laurent ROUZIES (MIT).
// Original shader and notice are retained alongside this adapter. No NGX consumer.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <reshade.hpp>
#include "../../prepared-guides/src/ExternalFeederAbi.h"
#include "../../prepared-guides/src/depth_binding.h"
#include <array>
#include <mutex>
#include <map>
#include <string>
#include <limits>
#include <cstring>
extern "C" __declspec(dllexport) const char* NAME="NeuRotic External Feeder 1";
extern "C" __declspec(dllexport) const char* DESCRIPTION="Versioned DLSS5-Feeder capture-only prepared consumer. Uses NeuRotic; no manufactured DLAA or second neural renderer.";
namespace {
namespace A=reshade::api;namespace E=nrpg::external;namespace F=Neurotic::Feed::Prepared;
constexpr char Effect[]="DLSS5_Feed.fx";
struct Provider {int mode;const char* file;const char* technique;const char* texture;};
constexpr Provider providers[]={
 {0,"MotionEstimation.fx","DRME","texMotionVectors"},{0,"qUINT_motionvectors.fx","MotionVectors","texMotionVectors"},
 {0,"dh_uber_motion.fx","DH_UBER_MOTION_020","texMotionVectors"},
 {1,"MartysMods_LAUNCHPAD.fx","MartysMods_Launchpad","Deferred::MotionVectorsTex"},
 {2,"vort_Motion.fx","vort_MotionEffects","MotVectTexVort"},
 {3,"lumenite_Kernel.fx","Lumenite_Kernel","Kernel::tFlow"},{4,"lumenite_QuantMotion.fx","Lumenite_QuantMotion","QuantMotion::tFlow"}};
struct Session {std::uint64_t id=0,device=0,generation=1,capture=0,previous=0;unsigned width=0,height=0;
 bool armed=false,seen=false,unsafe=false;int mode=-1;A::effect_technique provider{};std::string last;};
struct Registry {std::recursive_mutex mutex;std::map<A::effect_runtime*,Session> sessions;std::uint64_t next=1;
 HMODULE consumer=nullptr;E::EvaluateFn evaluate=nullptr;};
Registry& State(){static auto* state=new Registry;return *state;}
void Report(Session& s,const char* reason){if(s.last==reason)return;s.last=reason;reshade::log::message(reshade::log::level::warning,reason);}
bool Increment(std::uint64_t& n){if(n==UINT64_MAX)return false;++n;return true;}
void Reset(Session& s){s.previous=0;s.armed=false;s.seen=false;if(!Increment(s.generation))s.unsafe=true;}
A::resource Texture(A::effect_runtime* rt,const char* name){auto v=rt->find_texture_variable(Effect,name);A::resource_view srv{},srgb{};
 if(v.handle)rt->get_texture_binding(v,&srv,&srgb);return srv.handle?rt->get_device()->get_resource_from_view(srv):A::resource{};}
bool Bind(){auto& st=State();if(st.evaluate)return true;HMODULE modules[1024]{};DWORD bytes=0;
 if(!K32EnumProcessModules(GetCurrentProcess(),modules,sizeof(modules),&bytes)||bytes>sizeof(modules))return false;
 FARPROC selected=nullptr;
 for(DWORD i=0;i<bytes/sizeof(HMODULE);++i){auto proc=GetProcAddress(modules[i],"NeuRotic_EvaluateExternalFeederV1");
#ifndef _WIN64
  if(!proc)proc=GetProcAddress(modules[i],"_NeuRotic_EvaluateExternalFeederV1");
#endif
  if(!proc)continue;if(selected)return false;selected=proc;
 }if(!selected)return false;HMODULE lease=nullptr;
 if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(selected),&lease))return false;
 st.consumer=lease;st.evaluate=reinterpret_cast<E::EvaluateFn>(selected);return true;
}
bool CompetingConsumer(){HMODULE modules[1024]{};DWORD bytes=0;
 if(!K32EnumProcessModules(GetCurrentProcess(),modules,sizeof(modules),&bytes)||bytes>sizeof(modules))return true;
 for(DWORD i=0;i<bytes/sizeof(HMODULE);++i){wchar_t path[MAX_PATH]{};if(!GetModuleFileNameW(modules[i],path,MAX_PATH))return true;
  std::wstring lower(path);for(auto& c:lower)c=static_cast<wchar_t>(towlower(c));
  auto pos=lower.find_last_of(L"\\/");auto name=lower.substr(pos==std::wstring::npos?0:pos+1);
  if(name.starts_with(L"dlss5-feed.")||name.starts_with(L"renodx-dlss")||name==L"dlss5-feed-host64.exe")return true;
 }return false;
}
void Init(A::effect_runtime* rt){auto& st=State();std::lock_guard lock(st.mutex);if(st.sessions.contains(rt)||st.sessions.size()>=4||st.next>UINT64_MAX-2)return;
 auto& s=st.sessions[rt];s.id=st.next++;s.device=st.next++;}
void Reload(A::effect_runtime* rt){auto& st=State();std::lock_guard lock(st.mutex);auto it=st.sessions.find(rt);if(it!=st.sessions.end())Reset(it->second);}
void Destroy(A::effect_runtime* rt){auto& st=State();std::lock_guard lock(st.mutex);auto it=st.sessions.find(rt);
 if(it!=st.sessions.end()&&!it->second.unsafe)st.sessions.erase(it);}
void Begin(A::effect_runtime* rt,A::command_list*,A::resource_view,A::resource_view){auto& st=State();std::lock_guard lock(st.mutex);
 auto it=st.sessions.find(rt);if(it==st.sessions.end())return;auto& s=it->second;if(s.unsafe)return;
 if(!s.seen)s.previous=0;s.armed=false;s.seen=false;if(!Increment(s.capture)){s.unsafe=true;return;}
 char modeText[16]{};int mode=0;if(rt->get_preprocessor_definition_for_effect(Effect,"DLSS5_MV_PROVIDER",modeText))mode=atoi(modeText);
 A::effect_technique selected{};for(const auto& p:providers)if(p.mode==mode){auto t=rt->find_technique(p.file,p.technique);if(t.handle&&rt->get_technique_state(t)){selected=t;break;}}
 if(s.mode!=mode||s.provider!=selected){Reset(s);s.mode=mode;s.provider=selected;}
}
void Technique(A::effect_runtime* rt,A::effect_technique technique,A::command_list* commands,A::resource_view rtv,A::resource_view){
 auto& st=State();std::lock_guard lock(st.mutex);auto it=st.sessions.find(rt);if(it==st.sessions.end())return;auto& s=it->second;
 if(s.unsafe||!s.capture)return;if(technique==s.provider&&!s.seen){
  unsigned w=0,h=0;rt->get_screenshot_width_and_height(&w,&h);
  for(const auto& p:providers)if(p.mode==s.mode){auto resource=Texture(rt,p.texture);if(!resource.handle)continue;
   auto shape=rt->get_device()->get_resource_desc(resource);const bool low=p.mode>=3;
   s.armed=shape.type==A::resource_type::texture_2d&&shape.texture.width==(low?w/8:w)&&shape.texture.height==(low?h/8:h)&&
    shape.texture.samples==1&&shape.texture.format==A::format::r16g16_float;if(s.armed)break;
  }return;}
 if(technique!=rt->find_technique(Effect,"DLSS5_Feed"))return;s.seen=true;
 if(!s.armed){s.previous=0;Report(s,"External Feeder: selected motion provider did not render before capture");return;}
 if(!nrpg::PreparedDepth(rt,Effect).handle){s.previous=0;Report(s,"External Feeder: no usable ReShade depth binding");return;}
 auto geom=rt->find_uniform_variable(Effect,"GEOM_ENABLE"),sign=rt->find_uniform_variable(Effect,"MV_SIGN"),scale=rt->find_uniform_variable(Effect,"MV_SCALE");
 bool geometry=true;float signs[2]{},factor=0;if(geom.handle)rt->get_uniform_value_bool(geom,&geometry,1);
 if(sign.handle)rt->get_uniform_value_float(sign,signs,2);if(scale.handle)rt->get_uniform_value_float(scale,&factor,1);
 if(geometry||signs[0]!=1||signs[1]!=1||factor!=1){s.previous=0;Report(s,"External Feeder: use default vector sign/scale and disable fitted geometry");return;}
 if(CompetingConsumer()||!Bind()){s.previous=0;Report(s,"External Feeder: prepared intake missing or another Feeder/RenoDX consumer is loaded");return;}
 E::Frame frame;frame.runtime=reinterpret_cast<std::uintptr_t>(rt);frame.commands=reinterpret_cast<std::uintptr_t>(commands);frame.outputView=rtv.handle;
 auto* dev=rt->get_device();frame.resources[0]=dev->get_resource_from_view(rtv).handle;
 frame.resources[1]=Texture(rt,"DLSS5_Depth").handle;frame.resources[2]=Texture(rt,"DLSS5_MV").handle;frame.resources[3]=Texture(rt,"DLSS5_Mask").handle;
 if(!frame.resources[0]||!frame.resources[1]||!frame.resources[2]||!frame.resources[3]){s.previous=0;Report(s,"External Feeder: guide textures incomplete");return;}
 const auto shape=dev->get_resource_desc({frame.resources[0]});if(shape.texture.width!=s.width||shape.texture.height!=s.height){Reset(s);s.width=shape.texture.width;s.height=shape.texture.height;}
 auto& d=frame.descriptor;d.producer=E::FeederProducer;d.session=d.stream=s.id;d.device=s.device;d.generation=s.generation;d.capture=s.capture;d.previousCapture=s.previous;d.maskCapture=s.capture;
 d.color=d.depth=d.motion=d.distrust={s.width,s.height,0,0,s.width,s.height};
 d.flags=F::HasDistrust|F::HudIncluded|F::ZeroJitterPolicy|(s.previous?0:F::Reset);
 switch(dev->get_api()){case A::device_api::d3d10:d.sourceApi=F::C::GraphicsApi::D3D10;break;case A::device_api::d3d11:d.sourceApi=F::C::GraphicsApi::D3D11;break;
 case A::device_api::d3d12:d.sourceApi=F::C::GraphicsApi::D3D12;break;case A::device_api::vulkan:d.sourceApi=F::C::GraphicsApi::Vulkan;break;
 case A::device_api::opengl:d.sourceApi=F::C::GraphicsApi::OpenGL;d.rasterOrigin=F::C::RasterOrigin::BottomLeft;break;default:Report(s,"External Feeder: translate this legacy API first");return;}
 char reversed[16]{};if(!rt->get_preprocessor_definition_for_effect(Effect,"RESHADE_DEPTH_INPUT_IS_REVERSED",reversed)&&!rt->get_preprocessor_definition("RESHADE_DEPTH_INPUT_IS_REVERSED",reversed)){
  s.previous=0;Report(s,"External Feeder: set RESHADE_DEPTH_INPUT_IS_REVERSED explicitly to 0 or 1");return;}
 if(strcmp(reversed,"0")&&strcmp(reversed,"1")){s.previous=0;Report(s,"External Feeder: invalid explicit depth direction");return;}d.depthReversed=reversed[0]=='1';
 E::Receipt receipt;const auto accepted=st.evaluate(&frame,&receipt);receipt.reason[sizeof(receipt.reason)-1]=0;
 if(receipt.size!=sizeof(receipt)||receipt.version!=E::Version||receipt.reserved){s.unsafe=true;Report(s,"External Feeder: malformed consumer receipt; restart required");return;}
 if(receipt.result==E::Result::Unsafe){s.unsafe=true;Report(s,receipt.reason);return;}
 if(accepted&&receipt.result==E::Result::Delivered&&receipt.capture==d.capture&&receipt.generation==d.generation){s.previous=d.capture;Report(s,"External Feeder: exact completed NeuRotic output copied back");}
 else {s.previous=0;Report(s,receipt.reason[0]?receipt.reason:"External Feeder: current capture refused");}
}
}
BOOL APIENTRY DllMain(HMODULE module,DWORD reason,LPVOID){if(reason==DLL_PROCESS_ATTACH){if(!reshade::register_addon(module))return FALSE;
 reshade::register_event<reshade::addon_event::init_effect_runtime>(Init);reshade::register_event<reshade::addon_event::destroy_effect_runtime>(Destroy);
 reshade::register_event<reshade::addon_event::reshade_reloaded_effects>(Reload);reshade::register_event<reshade::addon_event::reshade_begin_effects>(Begin);
 reshade::register_event<reshade::addon_event::reshade_render_technique>(Technique);
 }else if(reason==DLL_PROCESS_DETACH){reshade::unregister_event<reshade::addon_event::init_effect_runtime>(Init);reshade::unregister_event<reshade::addon_event::destroy_effect_runtime>(Destroy);
 reshade::unregister_event<reshade::addon_event::reshade_reloaded_effects>(Reload);reshade::unregister_event<reshade::addon_event::reshade_begin_effects>(Begin);
 reshade::unregister_event<reshade::addon_event::reshade_render_technique>(Technique);reshade::unregister_addon(module);}return TRUE;}
