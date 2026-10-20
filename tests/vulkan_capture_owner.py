"""Run the collector's real selection expression and Present copy body.

Distinct inert producer values expose accidental D3D12 reads in Vulkan captures.
"""
from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[1]
out=root/'builds/vulkan-experiment/tests/capture-owner';out.mkdir(parents=True,exist_ok=True)
collector=(root/'OptiScaler/nr/diagnostics/capability/CapabilityStatusCollector.cpp').read_text()
start=collector.index('auto o=',collector.index('if(service.native)'))
selection=collector[start:collector.index(';',start)+1]
present=(root/'OptiScaler/dlssnr/DlssNr_Present.cpp').read_text()
start=present.index('Capability::PresentObservation CopyPresentCapabilityObservation(')
copy=present[start:present.index('\n}',start)+2]
unit='''#include <nr/diagnostics/capability/CapabilityOwnerAdapters.h>
#include <cassert>
#include <mutex>
#include <iostream>
#include <new>
enum class ApiUpscalerInput { DLSS_VK,XeSS_VK,FFX_VK,FSR2X_VK,DLSS_DX12 };
enum class API { Vulkan, DX12 };
struct State { API api=API::Vulkan;ApiUpscalerInput currentInputApiName=ApiUpscalerInput::DLSS_VK;
 static State& Instance(){static State s;return s;} };
namespace DlssNr {
Capability::NativeObservation CopyCapabilityObservation(const Capability::WriterPort&,const Capability::WriterPort*) {Capability::NativeObservation o;o.counters[0]=12;return o;}
Capability::NativeObservation CopyCapabilityObservationVk(const Capability::WriterPort&,const Capability::WriterPort*) {Capability::NativeObservation o;o.counters[0]=77;return o;}
struct T {uint64_t modelEvaluations=0,compositeEvaluations=0,skippedFrames=0,modelSubmissions=0,compositeSubmissions=0,presentAttempts=0,consecutiveFallbacks=0,pendingSlots=0;};
struct {std::mutex mutex;T telemetry;} g_present;
struct VkStatus {uint64_t recorded=1,composed=1,submitted=77,completed=70,attempts=88,consecutiveFallbacks=0;};
bool failSnapshot=false;
struct VkOwner {VkStatus Snapshot(){if(failSnapshot)throw std::bad_alloc();return {};}};
VkOwner& GetVulkanPresentStatus(){static VkOwner s;return s;}
'''+copy+'''
}
int main(){
 using namespace DlssNr::Capability;Store store("fixture");AdapterFactory factory(store);
 struct {std::optional<WriterPort> native,lifecycle;} service;
 service.native=factory.RegisterWriter(ProducerId::Native,"native").port;
 service.lifecycle=factory.RegisterWriter(ProducerId::Lifecycle,"native").port;
 for(auto api:{ApiUpscalerInput::DLSS_VK,ApiUpscalerInput::XeSS_VK,ApiUpscalerInput::FFX_VK,ApiUpscalerInput::FSR2X_VK,ApiUpscalerInput::DLSS_DX12}){
  State::Instance().currentInputApiName=api;State::Instance().api=api==ApiUpscalerInput::DLSS_DX12?API::DX12:API::Vulkan;
  '''+selection+'''
  auto wanted=api==ApiUpscalerInput::DLSS_DX12?12u:77u;
  if(o.counters[0]!=wanted){std::cerr<<"FAIL capture returned the wrong Native owner\\n";return 1;}
 }
 auto port=factory.RegisterWriter(ProducerId::Present,"present").port;
 State::Instance().api=API::Vulkan;
 auto p=DlssNr::CopyPresentCapabilityObservation(*port);
 if(p.counters[3]!=77||p.counters[5]!=88||p.counters[7]!=7){std::cerr<<"FAIL capture returned D3D12 Present counters on Vulkan\\n";return 1;}
 DlssNr::failSnapshot=true;p=DlssNr::CopyPresentCapabilityObservation(*port);assert(p.sequence==0);
 std::cout<<"PASS four Vulkan Native inputs, D3D12 fallback and selected Present counters\\n";
}
'''
(out/'capture-owner.cpp').write_text(unit)
src='OptiScaler\\nr\\diagnostics\\capability\\'
command=f'''@echo off
call "C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Auxiliary\\Build\\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
cd /d "{root}"
cl /nologo /std:c++20 /EHsc /W4 /WX /I OptiScaler "{out/'capture-owner.cpp'}" {src}CapabilityStore.cpp {src}CapabilityStaging.cpp /Fe:"{out/'capture-owner.exe'}" /Fo:"{out}/"
if errorlevel 1 exit /b 1
"{out/'capture-owner.exe'}"
'''
(out/'run.cmd').write_text(command)
raise SystemExit(subprocess.run(['cmd','/d','/c',str(out/'run.cmd')],cwd=root).returncode)
