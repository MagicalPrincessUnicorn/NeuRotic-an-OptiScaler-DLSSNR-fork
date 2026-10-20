$ErrorActionPreference='Stop'
$repo=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$output=Join-Path $repo 'builds/vulkan-experiment/tests/ingress-exception'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$source=Get-Content -Raw -LiteralPath (Join-Path $repo 'OptiScaler/inputs/NVNGX_DLSS_Vk.cpp')
$first=$source.IndexOf('try { upscaleResult = deviceContext->Evaluate(InCmdList, InParameters); }')
$last=$source.IndexOf('const bool restored = scope.Restore();',$first)
if($first -lt 0 -or $last -lt $first){throw 'Selected ingress exception boundary missing'}
$fixture=@'
#include "nvsdk_ngx_defs.h"
#include <stdexcept>
#include <cstdlib>
#include <cstdio>
struct Counts { int failedGuides=0,restored=0,reset=0,invalidated=0,rejected=0,completed=0; } counts;
struct Settings { bool diagnostic=false; bool IsPrivateDiagnostic()const{return diagnostic;} } settings;
struct Admission { bool enabled=true,nativeSelected=true; } nrAdmission;
struct Scope { void Restore(){++counts.restored;} } scope;
struct Context { bool throws=false; bool Evaluate(int,int){if(throws)throw std::runtime_error("provider");return true;} } context;
namespace DlssNr {
void CompleteVulkanGuideEvaluation(int,bool ok){if(ok)std::abort();++counts.failedGuides;}
void RequestHistoryResetVk(){++counts.reset;}
struct Registry {void InvalidateNativeRenderSize(int){++counts.invalidated;}} registry;
Registry& GetVulkanPresentRegistry(){return registry;}
void RejectVulkanNativeGuides(const char*){++counts.rejected;}
void CompletePreSrVk(int,bool,bool ok){if(ok)std::abort();++counts.completed;}
}
Settings* nrSettings=&settings;
Context* deviceContext=&context;
bool bridged=false,nativePerformanceSelected=true,bound=true;
int selectedEvaluation=1,vkDevice=1,preSr=1,InCmdList=1,InParameters=1;
extern "C" NVSDK_NGX_Result invokeSelected(){bool upscaleResult=false;
'@ + $source.Substring($first,$last-$first) + @'
return upscaleResult?NVSDK_NGX_Result_Success:NVSDK_NGX_Result_Fail;}
int main(){
 auto check=[](bool ok){if(!ok)std::exit(1);};
 check(invokeSelected()==NVSDK_NGX_Result_Success&&counts.failedGuides==0);
 context.throws=true;
 check(invokeSelected()==NVSDK_NGX_Result_Fail);
 check(counts.failedGuides==1&&counts.restored==1&&counts.reset==1&&counts.invalidated==1&&counts.rejected==1&&counts.completed==1);
 counts={};nrAdmission.enabled=false;nativePerformanceSelected=false;
 check(invokeSelected()==NVSDK_NGX_Result_Fail);
 check(counts.failedGuides==1&&counts.restored==1&&counts.reset==0&&counts.invalidated==1&&counts.rejected==1&&counts.completed==0);
 std::puts("PASS actual selected-SR exception handler returns API failure with restoration, invalidation and selected reset; normal evaluation preserved");
}
'@
$cpp=Join-Path $output 'ingress-exception.cpp'
$fixture | Set-Content -LiteralPath $cpp -Encoding utf8
$cmd=Join-Path $output 'run.cmd'
@('@echo off','call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul',
 "cl /nologo /std:c++20 /EHsc /W4 /WX /I `"$repo/external/nvngx_dlss_sdk`" `"$cpp`" /Fo:`"$output/ingress-exception.obj`" /Fe:`"$output/ingress-exception.exe`"",
 'if errorlevel 1 exit /b 1',"`"$output/ingress-exception.exe`"") | Set-Content -LiteralPath $cmd -Encoding ascii
& cmd.exe /d /c "`"$cmd`""
if($LASTEXITCODE -ne 0){throw "Selected ingress exception regression failed: $LASTEXITCODE"}
