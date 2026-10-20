param([string]$OutputDirectory = 'builds/ui-release/audit-hooks')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$out = [IO.Path]::GetFullPath((Join-Path $root $OutputDirectory))
New-Item -ItemType Directory -Force -Path $out | Out-Null
function FunctionText([string]$source, [string]$signature) {
    $start = $source.IndexOf($signature)
    if ($start -lt 0) { throw "Missing $signature" }
    $open = $source.IndexOf('{', $start)
    $depth = 1; $end = $open + 1
    while ($depth -and $end -lt $source.Length) {
        if ($source[$end] -eq '{') { ++$depth }
        if ($source[$end] -eq '}') { --$depth }
        ++$end
    }
    return $source.Substring($start, $end - $start)
}
$input = Get-Content -Raw -LiteralPath (Join-Path $root 'OptiScaler/inputs/NVNGX_DLSS_Dx12.cpp')
$owned = Get-Content -Raw -LiteralPath (Join-Path $root 'OptiScaler/upscalers/dlss/DLSSFeature_Dx12.cpp')
$core = FunctionText $input 'static NVSDK_NGX_Result ShutdownDx12Core()'
$shutdown1 = FunctionText $input 'NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_D3D12_Shutdown1('
$helper = FunctionText $owned 'void DLSSFeatureDx12::Shutdown(ID3D12Device* InDevice)'
$cpp = @'
#include <cstdio>
#include <optional>
#include <map>
#include <memory>
#include "../../../OptiScaler/runtime/NgxCallGate.h"
namespace Neurotic::Runtime { bool BootstrapUnavailable(){return false;} }
#define NVSDK_NGX_API
using NVSDK_NGX_Result=int;
constexpr int NVSDK_NGX_Result_Success=0,NVSDK_NGX_Result_FAIL_PlatformError=1;
struct ID3D12Device {};
static bool shutdown=false;
static ID3D12Device* D3D12Device=nullptr;
static ID3D12Device* expectedDevice=nullptr;
static int calls=0, calls1=0, providerResult=0, failures=0, checks=0;
static bool deviceMatched=true,plainAvailable=true;
struct OwnedFeature {bool ready=true; int releaseResult=0; bool CanRetire() const{return ready;} int ReleaseProvider(){return releaseResult;}};
struct OwnedContext {std::unique_ptr<OwnedFeature> feature;};
static std::map<unsigned,OwnedContext> Dx12Contexts;
void check(bool value,const char* name) { ++checks;if(!value){++failures;std::printf("FAIL: %s\n",name);} }
int NativeShutdown(){++calls;return providerResult;}
int NativeShutdown1(ID3D12Device* device){++calls1;deviceMatched=deviceMatched && device==expectedDevice;return providerResult;}
struct NVNGXProxy {
 static inline bool initialized=true;
 static bool IsDx12Inited(){return initialized;}
 static void SetDx12Inited(bool value){initialized=value;}
 static auto D3D12_Shutdown(){return plainAvailable?&NativeShutdown:nullptr;}
 static auto D3D12_Shutdown1(){return &NativeShutdown1;}
};
struct DLSSFeature {static void Shutdown(){}}; // actual base only removes hooks
struct DLSSFeatureDx12 {static inline bool _dlssInitedDx12=true;static void Shutdown(ID3D12Device*);};
namespace DlssNr {
 bool Shutdown(){return true;}
 namespace NativeDx12Source {void Close(){}}
}
static bool shutdownGuardActive=false;
struct Guard {void Complete(){shutdownGuardActive=false;}};
struct Registry {std::optional<Guard> BeginShutdown(){if(shutdownGuardActive)return {};shutdownGuardActive=true;return Guard{};}} HandleToFeature;
struct Option {bool value_or_default(){return true;}};
struct Config {Option DLSSEnabled;static Config* Instance(){static Config c;return &c;}};
struct Feature {void Shutdown(){} void DestroyFGContext(){}};
enum class FGInput{NoFG,Upscaler};
enum class FGNvngxReplacement{None};
struct State {
 bool nvngxDx12Inited=true,isShuttingDown=false,clearCapturedHudlesses=false;
 void* currentFeature=nullptr;Feature* currentFG=nullptr;
 FGInput activeFgInput=FGInput::NoFG;
 FGNvngxReplacement activeFgNvngx=FGNvngxReplacement::None;
 static State& Instance(){static State state;return state;}
};
struct Nvngx_FG {static void D3D12_Shutdown(){} static void D3D12_Shutdown1(ID3D12Device*){}};
void reset(ID3D12Device* device,int result,bool plain=true){
 shutdown=false;D3D12Device=expectedDevice=device;calls=calls1=0;providerResult=result;
 deviceMatched=true;plainAvailable=plain;NVNGXProxy::initialized=true;
 DLSSFeatureDx12::_dlssInitedDx12=true;State::Instance()=State{};
}
'@
$cpp += "`n$helper`n$core`n$shutdown1`n"
$cpp += @'
int main(){
 ID3D12Device device;
 for(bool explicitDevice:{false,true}) {
  reset(&device,0);Dx12Contexts[99].feature=std::make_unique<OwnedFeature>();
  auto* retained=Dx12Contexts[99].feature.get();retained->releaseResult=13;
  const auto result=explicitDevice?NVSDK_NGX_D3D12_Shutdown1(&device):ShutdownDx12Core();
  check(result==13&&Dx12Contexts[99].feature.get()==retained,"failed native feature release retains owning context");
  check(calls+calls1==0&&!shutdown,"failed feature release does not shut down provider core");
  retained->releaseResult=0;
  check((explicitDevice?NVSDK_NGX_D3D12_Shutdown1(&device):ShutdownDx12Core())==0&&Dx12Contexts.empty(),"successful release retry permits shutdown destruction");
 }
 for(bool plain:{false,true}) {
  reset(&device,0,plain);auto result=ShutdownDx12Core();
  check(result==0 && calls+calls1==1,"one opaque shutdown for shared native/owned session");
  check(!DLSSFeatureDx12::_dlssInitedDx12 && !NVNGXProxy::initialized,"both init flags retire for reinitialization");
  check(deviceMatched,"Shutdown1 fallback receives original live device");
  reset(&device,7,plain);result=ShutdownDx12Core();
  check(result==7 && calls+calls1==1,"failed shutdown returns provider failure without retry");
  check(NVNGXProxy::initialized,"failed shutdown does not publish successful core retirement");
 }
 for(int nativeResult:{0,7}) {
  reset(&device,nativeResult);const auto result=NVSDK_NGX_D3D12_Shutdown1(&device);
  check(result==nativeResult && calls1==1 && calls==0,"explicit Shutdown1 preserves result and never enters second shutdown");
  check(!shutdown && deviceMatched,"Shutdown1 restores wrapper state and exact device");
  check(nativeResult?NVNGXProxy::initialized:!NVNGXProxy::initialized && !DLSSFeatureDx12::_dlssInitedDx12,"Shutdown1 flags match confirmed outcome");
 }
 reset(&device,0);NVNGXProxy::initialized=false;
 ShutdownDx12Core();check(calls+calls1==0,"no opaque shutdown for an uninitialized core");
 reset(&device,0);State::Instance().isShuttingDown=true;
 ShutdownDx12Core();check(calls+calls1==0,"process detach keeps native-call suppression");
 std::printf("NGX D3D12 shutdown: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
'@
Set-Content -LiteralPath (Join-Path $out 'ngx-shutdown.cpp') -Value $cpp
$cmd = @'
@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX ngx-shutdown.cpp /Fe:ngx-shutdown.exe /Fo:ngx-shutdown.obj
if errorlevel 1 exit /b 1
ngx-shutdown.exe
'@
Set-Content -LiteralPath (Join-Path $out 'run-ngx-shutdown.cmd') -Value $cmd
Push-Location $out
try { & cmd.exe /c run-ngx-shutdown.cmd; $result=$LASTEXITCODE } finally {
    Pop-Location
    Remove-Item -LiteralPath (Join-Path $out 'ngx-shutdown.obj') -ErrorAction SilentlyContinue
}
exit $result
