param([string]$OutputDirectory = 'builds/ui-release/audit-hooks')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$out = [IO.Path]::GetFullPath((Join-Path $root $OutputDirectory))
New-Item -ItemType Directory -Force -Path $out | Out-Null
$fg = Get-Content -Raw -LiteralPath (Join-Path $root 'OptiScaler/hooks/FG_Hooks.cpp')
$ngx = Get-Content -Raw -LiteralPath (Join-Path $root 'OptiScaler/framegen/nvngx/Nvngx_FG.cpp')
$xefg = Get-Content -Raw -LiteralPath (Join-Path $root 'OptiScaler/framegen/xefg/XeFG_Dx12.cpp')
$wrapper = Get-Content -Raw -LiteralPath (Join-Path $root 'OptiScaler/wrapped/wrapped_swapchain.cpp')
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
$release12 = FunctionText $ngx 'NVSDK_NGX_Result Nvngx_FG::D3D12_ReleaseFeature('
$releaseVk = FunctionText $ngx 'NVSDK_NGX_Result Nvngx_FG::VULKAN_ReleaseFeature('
$resize = FunctionText $fg 'HRESULT FGHooks::hkResizeBuffers('
$resize1 = FunctionText $fg 'HRESULT FGHooks::hkResizeBuffers1('
function BackbufferBlock([string]$source) {
    $start = $source.IndexOf('// Release swapchain backbuffers to prevent errors when resizing')
    $end = $source.IndexOf('    HRESULT result;', $start)
    if ($start -lt 0 -or $end -lt 0) { throw 'Missing resize ownership boundary' }
    return $source.Substring($start, $end - $start)
}
$bb0 = BackbufferBlock $resize
$bb1 = BackbufferBlock $resize1
$release = FunctionText $fg 'ULONG FGHooks::hkFGRelease('
$start = $release.IndexOf('            if (State::Instance().currentWrappedSwapchain != nullptr')
$end = $release.IndexOf('            // Owner aliases retired only after provider release succeeds.', $start)
$wrapped = $release.Substring($start, $end - $start)
$start = $release.IndexOf('// Release swapchain backbuffers to prevent errors when releasing FG swapchain')
$end = $release.IndexOf('// Reentry is scoped to this thread, swapchain and operation.', $start)
$releaseBuffers = $release.Substring($start, $end - $start)
$wrapperRelease = FunctionText $wrapper 'ULONG STDMETHODCALLTYPE WrappedIDXGISwapChain4::Release('
$start = $wrapperRelease.IndexOf('        if (State::Instance().currentSwapchain == this)')
$end = $wrapperRelease.IndexOf('        auto fg = ', $start)
$retireWrapper = $wrapperRelease.Substring($start, $end - $start)
function RecreateBlock([string]$source, [string]$signature) {
    $fn = FunctionText $source $signature
    $start = $fn.IndexOf('            ReleaseSwapchain(_hwnd);')
    $start += '            ReleaseSwapchain(_hwnd);'.Length
    $end = $fn.IndexOf("`n        }", $start)
    return $fn.Substring($start, $end - $start)
}
$real0 = RecreateBlock $xefg 'bool XeFG_Dx12::CreateSwapchain('
$real1 = RecreateBlock $xefg 'bool XeFG_Dx12::CreateSwapchain1('
$cpp = @'
#include <mutex>
#include <inputs/FG/FgOwnerScope.h>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
static int failures=0, checks=0;
void check(bool value,const char* name) { ++checks; if(!value) { ++failures; std::printf("FAIL: %s\n",name); } }
static bool freed=false, locked=false;
static int proxyDestroyed=0, destroyedLocked=0, freedUnlock=0;
struct TestMutex {
 void lock() { locked=true; }
 void unlock() { if(freed) ++freedUnlock; locked=false; }
};
using NVSDK_NGX_Result=int;
constexpr int NVSDK_NGX_Result_Success=0,NVSDK_NGX_Result_Fail=1,NVSDK_NGX_Result_FAIL_InvalidParameter=2,NVSDK_NGX_Result_FAIL_FeatureNotFound=3;
constexpr unsigned NVNGX_PROVIDER_ID_OFFSET=100;
struct NVSDK_NGX_Handle {
 unsigned Id;
 // Quarantine the deallocation so the original invalid unlock can be measured
 // without dereferencing physically reclaimed memory. ASan separately proves
 // the original mismatched-type delete with the actual std::shared_mutex.
 static void operator delete(void*) noexcept { freed=true; }
};
struct Provider {
 int result=0;
 int D3D12_ReleaseFeature(NVSDK_NGX_Handle*) { return result; }
 int VULKAN_ReleaseFeature(NVSDK_NGX_Handle*) { return result; }
};
class Nvngx_FG {
public:
 struct Nvngx_FG_Handle {
  unsigned id; NVSDK_NGX_Handle* nativeHandle=nullptr; TestMutex handleMutex;
  ~Nvngx_FG_Handle() { ++proxyDestroyed; if(locked) ++destroyedLocked; }
  static void operator delete(void*) noexcept { freed=true; }
 };
 static inline Provider testProvider;
 static Provider* getProvider() { return &testProvider; }
 static int D3D12_ReleaseFeature(NVSDK_NGX_Handle*);
 static int VULKAN_ReleaseFeature(NVSDK_NGX_Handle*);
};
#define LOG_DEBUG(...)
#define XEFG_RESOURCE_REF_LIMIT 1
using UINT=unsigned;
constexpr int S_OK=0;
struct ID3D12Resource { unsigned refs=2; unsigned Release() { return --refs; } };
struct Chain {
 unsigned refs=3; ID3D12Resource buffer;
 Chain* _real=nullptr;
 void RetireAliases();
 int GetBuffer(unsigned index,ID3D12Resource** out) { if(index) return 1; *out=&buffer; ++buffer.refs; return 0; }
 unsigned Release() { return --refs; }
};
using IDXGISwapChain=Chain;
#define IID_PPV_ARGS(x) x
enum class FGOutput { XeFG };
struct State {
 FGOutput activeFgOutput=FGOutput::XeFG;
 Chain* currentWrappedSwapchain=nullptr;
 Chain* currentRealSwapchain=nullptr;
 Chain* currentSwapchain=nullptr;
 struct { int OutputWindow=1; } currentSwapchainDesc;
 static State& Instance() { static State state; return state; }
};
constexpr int _hwnd=1;
'@
$cpp += "`n$release12`n$releaseVk`n"
$cpp += "void Resize0(Chain* This) {`n$bb0`n}`nvoid Resize1(Chain* This) {`n$bb1`n}`n"
$cpp += "void ClearWrapped() {`n$wrapped`n}`nvoid Recreate0() {`n$real0`n}`nvoid Recreate1() {`n$real1`n}`n"
$cpp += "void ReleaseBuffers(Chain* This) { struct { UINT BufferCount=1; } scDesc;`n$releaseBuffers`n}`n"
$cpp += "void Chain::RetireAliases() {`n$retireWrapper`n}`n"
$cpp += @'
int main() {
 for(bool vk:{false,true}) {
  for(int result:{0,7}) {
   freed=false;locked=false;proxyDestroyed=destroyedLocked=freedUnlock=0;
   auto* proxy=new Nvngx_FG::Nvngx_FG_Handle{NVNGX_PROVIDER_ID_OFFSET};
   Nvngx_FG::testProvider.result=result;
   auto* input=reinterpret_cast<NVSDK_NGX_Handle*>(proxy);
   const int returned=vk?Nvngx_FG::VULKAN_ReleaseFeature(input):Nvngx_FG::D3D12_ReleaseFeature(input);
   check(returned==result,"provider result preserved");
   check(!locked && !freedUnlock && !destroyedLocked,"mutex unlock precedes proxy destruction");
   check(result ? !freed && proxyDestroyed==0 : freed && proxyDestroyed==1,"typed deletion only after success");
   if(result) delete proxy;
   std::free(proxy);
  }
 }
 for(auto resize:{&Resize0,&Resize1}) { Chain chain;resize(&chain);check(chain.buffer.refs==2,"resize preserves swapchain and game backbuffer references"); }
 Chain releaseChain;ReleaseBuffers(&releaseChain);check(releaseChain.buffer.refs==2,"release cleanup preserves foreign backbuffer references");
 Chain wrapped;State::Instance().currentWrappedSwapchain=&wrapped;ClearWrapped();
 check(wrapped.refs==3,"borrowed wrapper alias cannot consume owner references");
 check(!State::Instance().currentWrappedSwapchain,"retired wrapper alias cleared");
 for(auto recreate:{&Recreate0,&Recreate1}) { Chain real;State::Instance().currentRealSwapchain=&real;recreate();check(real.refs==3,"borrowed real swapchain alias cannot consume owner references");check(!State::Instance().currentRealSwapchain,"retired real alias cleared"); }
 Chain wrapper,real,other;wrapper._real=&real;
 auto& state=State::Instance();state.currentSwapchain=&wrapper;state.currentWrappedSwapchain=&wrapper;state.currentRealSwapchain=&real;
 wrapper.RetireAliases();check(!state.currentSwapchain && !state.currentWrappedSwapchain && !state.currentRealSwapchain,"final wrapper release clears its borrowed aliases");
 state.currentSwapchain=state.currentWrappedSwapchain=state.currentRealSwapchain=&other;
 wrapper.RetireAliases();check(state.currentSwapchain==&other && state.currentWrappedSwapchain==&other && state.currentRealSwapchain==&other,"another live wrapper's aliases remain intact");
 std::printf("FG owned lifetimes: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
'@
Set-Content -LiteralPath (Join-Path $out 'owned-lifetimes.cpp') -Value $cpp
$cmd = @"
@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX /I"$root/OptiScaler" owned-lifetimes.cpp /Fe:owned-lifetimes.exe /Fo:owned-lifetimes.obj
if errorlevel 1 exit /b 1
owned-lifetimes.exe
"@
Set-Content -LiteralPath (Join-Path $out 'run-owned-lifetimes.cmd') -Value $cmd
Push-Location $out
try { & cmd.exe /c run-owned-lifetimes.cmd; $result=$LASTEXITCODE } finally {
    Pop-Location
    Remove-Item -LiteralPath (Join-Path $out 'owned-lifetimes.obj') -ErrorAction SilentlyContinue
}
exit $result
