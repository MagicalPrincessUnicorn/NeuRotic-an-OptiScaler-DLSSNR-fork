param([string]$Label='streamline-ingress')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$output=Join-Path $root 'builds/vulkan-experiment/tests/streamline-ingress'
$evidence=Join-Path $root 'docs/implementation-evidence/vulkan-return-165420-20261002'
New-Item -ItemType Directory -Force -Path $output,$evidence | Out-Null
$source=Get-Content -Raw (Join-Path $root 'OptiScaler/hooks/Streamline_Hooks.cpp')
$start=$source.IndexOf('                const bool observeProvenance=RP::Enabled();')
$end=$source.IndexOf('                // Publish hook-visible policy',$start)
if($start -lt 0 -or $end -le $start){throw 'Production registration block missing'}
$registration=$source.Substring($start,$end-$start)
$originals=[regex]::Matches($registration,'\bo_sl\w+') | ForEach-Object Value | Sort-Object -Unique
$hooks=[regex]::Matches($registration,'\bhksl\w+') | ForEach-Object Value | Sort-Object -Unique
$declarations=($originals | ForEach-Object {"void* $_=reinterpret_cast<void*>(1);"}) -join "`n"
$declarations+="`n"+(($hooks | ForEach-Object {"void $_(){}"}) -join "`n")
$fixture=@'
#include <set>
#include <cstdio>
#include <cstdlib>
using PVOID=void*;
enum class FGInput { NoFG,NvngxFG,DLSSG };
struct State {FGInput activeFgInput=FGInput::NoFG;static State& Instance(){static State s;return s;}};
namespace RP {bool enabled=false;bool Enabled(){return enabled;}}
std::set<void**> attached;
void DetourTransactionBegin(){} int GetCurrentThread(){return 0;} void DetourUpdateThread(int){}
void DetourAttach(void** p,void(*)()){attached.insert(p);}
#define LOG_TRACE(...) ((void)0)
'@ + "`n"+$declarations+"`nvoid Install(){`n"+$registration+@'
}
int main(){
 unsigned failures=0;
 auto check=[&](bool ok,const char* message){if(!ok){++failures;std::fprintf(stderr,"FAIL: %s\n",message);}};
 for(auto input:{FGInput::NoFG,FGInput::NvngxFG,FGInput::DLSSG})for(bool provenance:{false,true}){
  State::Instance().activeFgInput=input;RP::enabled=provenance;attached.clear();Install();
  check(attached.contains(&o_slSetTagForFrame),"public frame tags must reach Vulkan NR with native FG and provenance disabled");
  check(attached.contains(&o_slGetFeatureFunction),"native public FG option/state resolver must remain observable without replacement FG");
  check(attached.contains(&o_slEvaluateFeature),"public evaluations must authenticate matching provider/frame identity");
  check(attached.contains(&o_slSetFeatureLoaded),"public provider unload must remain authoritative");
  check(attached.contains(&o_slSetTag)==(input!=FGInput::NoFG),"legacy replacement tag hook policy preserved");
  check(attached.contains(&o_slSetConstants)==(input!=FGInput::NoFG),"replacement constants policy preserved");
 }
 std::printf("Production Streamline hook registration: %u failure(s)\n",failures);
 return failures?1:0;
}
'@
[IO.File]::WriteAllText((Join-Path $output 'fixture.cpp'),$fixture)
$tagStart=$source.IndexOf('    if(renderApi==sl::RenderAPI::eVulkan){',$source.IndexOf('sl::Result StreamlineHooks::hkslSetTagForFrame('))
$tagEnd=$source.IndexOf('        return result;',$tagStart)
$tagEnd=$source.IndexOf('    }',$tagEnd)+5
$tagBody=$source.Substring($tagStart,$tagEnd-$tagStart)
$ledgerSource=Get-Content -Raw (Join-Path $root 'OptiScaler/dlssnr/StreamlinePreFg.h')
$ledgerStart=$ledgerSource.IndexOf('inline sl::Result Tags(')
$ledgerEnd=$ledgerSource.IndexOf('inline sl::Result Constants(',$ledgerStart)
$ledgerBody=$ledgerSource.Substring($ledgerStart,$ledgerEnd-$ledgerStart)
$tagFixture=(Get-Content -Raw (Join-Path $PSScriptRoot 'vulkan_streamline_ingress.cpp.in')).Replace('// PUBLIC_VULKAN_TAG_BOUNDARY',$tagBody).Replace('// PRODUCTION_LEDGER_TAGS',$ledgerBody)
[IO.File]::WriteAllText((Join-Path $output 'tag-fixture.cpp'),$tagFixture)
$include='/I "'+$root+'/OptiScaler" /I "'+$root+'/external/streamline" /I "'+$root+'/external/vulkan/include" /I "'+$root+'/external/nvngx_dlss_sdk"'
$sources='"'+$root+'/OptiScaler/dlssnr/VulkanNrStreamline.cpp" "'+$root+'/OptiScaler/dlssnr/VulkanNrPreFg.cpp" "'+$root+'/OptiScaler/dlssnr/VulkanNrRecording.cpp"'
[IO.File]::WriteAllLines((Join-Path $output 'run.cmd'),@('@echo off','call "C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat" -vcvars_ver=14.44 >nul','if errorlevel 1 exit /b 1','cl /nologo /std:c++20 /EHsc /W4 /WX fixture.cpp /Fe:fixture.exe /Fo:fixture.obj','if errorlevel 1 exit /b 1','fixture.exe','if errorlevel 1 exit /b 1',('cl /nologo /std:c++20 /EHsc /W4 /WX '+$include+' tag-fixture.cpp '+$sources+' /Fe:tag-fixture.exe'),'if errorlevel 1 exit /b 1','tag-fixture.exe','exit /b %errorlevel%'))
Push-Location $output
try{
 $log=(& cmd.exe /d /c run.cmd 2>&1 | Out-String);$result=$LASTEXITCODE
 [IO.File]::WriteAllText((Join-Path $evidence ($Label+'.log')),$log);Write-Output $log
 if($result -ne 0){throw "Streamline ingress fixture failed: $result"}
}finally{Pop-Location}
