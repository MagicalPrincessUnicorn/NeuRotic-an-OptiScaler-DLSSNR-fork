$ErrorActionPreference='Stop'
$repo=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$output=Join-Path $repo 'builds/vulkan-experiment/tests/native-delivery'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$source=Get-Content -Raw -LiteralPath (Join-Path $repo 'OptiScaler/dlssnr/DlssNrFeature_Vk.cpp')
$first=$source.IndexOf('void ObserveNativeResultVk(')
$last=$source.IndexOf('static std::optional<double> NativeGpuTimeVkLocked();',$first)
if($first -lt 0 -or $last -lt $first){throw 'Native delivery source boundary missing'}
$body=$source.Substring($first,$last-$first)
if(!$body.Contains('bool selected')){$body=$body.Replace('bool succeeded)','bool succeeded, bool selected=true)')}
$body=$body.Replace('bool selected)','bool selected=true)')
$fixture=@'
#include "dlssnr/VulkanNrSession.h"
#include <mutex>
#include <cstdio>
#include <cstdlib>
namespace DlssNr {
std::mutex g_vkMutex;
struct { bool nativeDeliveryAccepted=false,reset=false;uint64_t resetRevision=0;
 std::string nativeWaitingReason;struct {bool pending=false;std::string reason;} workload;
} g_nativeVk;
'@ + $body + @'
}
int main(){using namespace DlssNr;
 auto check=[](bool ok){if(!ok)std::exit(1);};
 VkRecordResult success;success.status=VkRecordStatus::Complete;
 success.modelRecorded=success.outputWriteRecorded=true;
 ObserveNativeResultVk(success,false,false,true);check(g_nativeVk.nativeDeliveryAccepted&&!g_nativeVk.reset);
 ObserveNativeResultVk({},false,false,true,false);check(g_nativeVk.nativeDeliveryAccepted&&!g_nativeVk.reset);
 ObserveNativeResultVk(success,false,false,false,false);check(g_nativeVk.reset&&g_nativeVk.resetRevision==1&&!g_nativeVk.nativeDeliveryAccepted);
 ObserveNativeResultVk(success,false,false,true);check(g_nativeVk.reset&&g_nativeVk.resetRevision==1);
 ObserveNativeResultVk(success,true,false,true);check(g_nativeVk.resetRevision==2&&!g_nativeVk.nativeDeliveryAccepted);
 ObserveNativeResultVk(success,true,true,true);check(g_nativeVk.resetRevision==2&&g_nativeVk.reset);
 std::puts("PASS actual Native delivery body preserves selected-failure and unbound gaps across later success");
}
'@
$cpp=Join-Path $output 'native-delivery.cpp'
$fixture | Set-Content -LiteralPath $cpp -Encoding utf8
$cmd=Join-Path $output 'run.cmd'
@("@echo off",'call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul',
 "cd /d `"$repo`"", "cl /nologo /std:c++20 /EHsc /W4 /I OptiScaler /I external/vulkan/include /I external/nvngx_dlss_sdk `"$cpp`" /Fo:`"$output/native-delivery.obj`" /Fe:`"$output/native-delivery.exe`"",
 'if errorlevel 1 exit /b 1',"`"$output/native-delivery.exe`"") | Set-Content -LiteralPath $cmd -Encoding ascii
& cmd.exe /d /c "`"$cmd`""
if($LASTEXITCODE -ne 0){throw "Native delivery regression failed: $LASTEXITCODE"}
