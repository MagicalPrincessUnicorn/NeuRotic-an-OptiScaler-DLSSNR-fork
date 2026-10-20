$ErrorActionPreference='Stop'
$repo=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$output=Join-Path $repo 'builds/vulkan-experiment/tests/route-scope'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$source=Get-Content -Raw -LiteralPath (Join-Path $repo 'OptiScaler/dlssnr/DlssNrFeature_Vk.cpp')
$match=[regex]::Match($source,'(?:thread_local )?VkState\* g_activeVk =')
$last=$source.IndexOf('uint64_t GenerationBytesVk()', $match.Index)
if(!$match.Success -or $last -le $match.Index){throw 'Actual route scope source boundary missing'}
$fixture=@'
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <cstdio>
#include <cstdlib>
enum class VkNrRoute { Native,Present };
struct DlssNr_Vk {};
struct VkState { std::unique_ptr<DlssNr_Vk> pass;DlssNr_Vk* borrowedPass=nullptr; };
VkState g_nativeVk,g_presentVk,nativeChild,presentChild;
VkState& PassStateVk(VkNrRoute route,unsigned pass){return route==VkNrRoute::Native?(pass?nativeChild:g_nativeVk):(pass?presentChild:g_presentVk);}
'@ + $source.Substring($match.Index,$last-$match.Index) + @'
int main(){
 std::mutex control;std::atomic<unsigned> phase{0};bool nativeKept=false,presentKept=false;
 auto until=[&](unsigned p){while(phase.load()<p)std::this_thread::yield();};
 std::thread a([&]{std::unique_lock lock(control);ScopedVkSession scope(VkNrRoute::Native);
   phase=1;lock.unlock();until(2);lock.lock();nativeKept=&ActiveVk()==&g_nativeVk;phase=3;});
 std::thread b([&]{until(1);std::unique_lock lock(control);ScopedVkSession scope(VkNrRoute::Present);
   phase=2;lock.unlock();until(3);lock.lock();presentKept=&ActiveVk()==&g_presentVk;});
 a.join();b.join();
 if(!nativeKept||!presentKept){std::fprintf(stderr,"FAIL actual scopes cross-route after unlocked wait: native=%d present=%d\n",nativeKept,presentKept);return 1;}
 {ScopedVkSession outer(VkNrRoute::Present);
  {ScopedVkSession inner(VkNrRoute::Native,1);if(&ActiveVk()!=&nativeChild)return 2;}
  if(&ActiveVk()!=&g_presentVk)return 3;
 }
 if(&ActiveVk()!=&g_nativeVk)return 4;
 std::puts("PASS actual route scopes retain per-thread route across unlocked provider/wait spans and restore nested pass scopes");
}
'@
$cpp=Join-Path $output 'route-scope.cpp';$fixture | Set-Content -LiteralPath $cpp -Encoding utf8
$cmd=Join-Path $output 'run.cmd'
@('@echo off','call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul',
 "cl /nologo /std:c++20 /EHsc /W4 /WX `"$cpp`" /Fo:`"$output/route-scope.obj`" /Fe:`"$output/route-scope.exe`"",
 'if errorlevel 1 exit /b 1',"`"$output/route-scope.exe`"") | Set-Content -LiteralPath $cmd -Encoding ascii
& cmd.exe /d /c "`"$cmd`""
if($LASTEXITCODE -ne 0){throw "Vulkan route scope regression failed: $LASTEXITCODE"}
