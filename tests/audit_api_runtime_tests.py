from pathlib import Path
import sys,subprocess
root=Path(__file__).resolve().parents[1]
baseline="--baseline" in sys.argv
out=root/'builds/audit-api';out.mkdir(parents=True,exist_ok=True)
def fn(path,name):
 s=subprocess.check_output(["git","show","4ea28822:"+path],cwd=root,text=True) if baseline and "dx11_with_dx12_sc.cpp" in path else (root/path).read_text();start=s.index(name);a=s.index('{',start);depth=1;i=a+1
 while depth:
  depth+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[start:i]
source=r'''
#include <cassert>
#include <cstdio>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <thread>
#include <future>
#include <map>
#include <shared_mutex>
#include <mutex>
#include <wrl/client.h>
#include "../../OptiScaler/framegen/ScopedFgReentry.h"
#include "../../OptiScaler/with_dx12/IncrementalPresent.h"
#include "../../apps/NeuRoticWindowWorker/nr/OwnedFrameRing.h"
#undef LOG_ERROR
#define LOG_ERROR(...) (void)0
#define LOG_DEBUG(...) (void)0
using UINT64=uint64_t;
static unsigned waits=0,waitResult=WAIT_TIMEOUT;static bool finishOnWait=false;
struct Fence {UINT64 completed=0;HRESULT arm=S_OK;UINT64 requested=0;UINT64 GetCompletedValue(){return completed;} HRESULT SetEventOnCompletion(UINT64 v,HANDLE){requested=v;return arm;}};
static Fence* waiting=nullptr;
static DWORD TestWait(HANDLE,DWORD){++waits;if(finishOnWait&&waiting)waiting->completed=waiting->requested;return waitResult;}
#define WaitForSingleObject TestWait
struct Dx11wDx12SC {
 bool _copyCompletionUnknown=false,_ingressPending=false;UINT64 _lastDx11FenceValue=0;
 Fence *_copyFence=nullptr,*_dx11Fence=nullptr;HANDLE _copyFenceEvent=(HANDLE)1,_dx11FenceEvent=(HANDLE)1;
 UINT64 _lastInteropCopyFenceValue=0;std::vector<UINT64> _copyAllocatorFenceValues;
 bool _WaitForCopyQueueIdle();bool _WaitForCopyAllocator(UINT);
};
'''
for name in ['bool Dx11wDx12SC::_WaitForCopyQueueIdle()','bool Dx11wDx12SC::_WaitForCopyAllocator(UINT slot)']:
 source+=fn('OptiScaler/with_dx12/dx11_with_dx12_sc.cpp',name)+'\n'
source+=r'''
struct List {HRESULT close=S_OK;unsigned executions=0;HRESULT Close(){return close;}};
using ID3D12CommandList=List;
struct Queue {HRESULT result=S_OK;unsigned executions=0;void ExecuteCommandLists(unsigned,List**){++executions;}HRESULT Signal(Fence* f,UINT64 v){f->requested=v;return result;}};
struct Identity final : IUnknown {
 IUnknown* canonical=this;ULONG refs=1;
 HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override {if(id!=__uuidof(IUnknown))return E_NOINTERFACE;*out=canonical;canonical->AddRef();return S_OK;}
 ULONG STDMETHODCALLTYPE AddRef() override{return ++refs;} ULONG STDMETHODCALLTYPE Release() override{return --refs;}
};
enum class FG_ResourceType {Depth,Velocity,HudlessColor,UIColor,Distortion};
struct IFGFeature_Dx12 {
 static constexpr unsigned BUFFER_COUNT=4;
 IUnknown* _swapChain=nullptr;bool OwnsSwapchain(IUnknown*) const;
 bool _scCompletionUnknown=false;UINT64 _scFenceValue=0;Fence* _scFence=nullptr;HANDLE _scFenceEvent=(HANDLE)1;
 std::vector<int> _scPins;Queue* _gameCommandQueue=nullptr;
 List* _scCommandList[4]{};bool _scCommandListResetted[4]{};UINT64 _scAllocatorFenceValues[4]{};
 std::map<FG_ResourceType,int> _frameResources[4],_resourceReady[4],_resourceCopy[4];std::shared_mutex _resourceMutex[4];
 bool _noHudless[4]{},_noUi[4]{},_noDistortionField[4]{};
 void RevokeResource(FG_ResourceType type,int index);
 bool RetireSCWork();bool SubmitSCCommandList(UINT index);
};
'''
for name in ['bool IFGFeature_Dx12::RetireSCWork()','bool IFGFeature_Dx12::SubmitSCCommandList(UINT index)','void IFGFeature_Dx12::RevokeResource(FG_ResourceType type, int index)','bool IFGFeature_Dx12::OwnsSwapchain(IUnknown* candidate) const']:
 source+=fn('OptiScaler/framegen/IFGFeature_Dx12.cpp',name)+'\n'
source+=r'''
int main(){
 {Identity first,alias,second;alias.canonical=&first;IFGFeature_Dx12 owner;owner._swapChain=&first;
  assert(owner.OwnsSwapchain(&first)&&owner.OwnsSwapchain(&alias)&&!owner.OwnsSwapchain(&second)&&!owner.OwnsSwapchain(nullptr));
  owner._swapChain=&second;assert(!owner.OwnsSwapchain(&alias));assert(first.refs==1&&second.refs==1);
 }

 {IFGFeature_Dx12 revoke;
  for(auto type:{FG_ResourceType::Depth,FG_ResourceType::Velocity,FG_ResourceType::HudlessColor,FG_ResourceType::UIColor,FG_ResourceType::Distortion}) {
   revoke._frameResources[0][type]=1;revoke._resourceReady[0][type]=1;revoke._resourceCopy[0][type]=17;
   revoke.RevokeResource(type,0);assert(!revoke._frameResources[0].contains(type)&&!revoke._resourceReady[0].contains(type)&&revoke._resourceCopy[0][type]==17);
  }
  assert(revoke._noHudless[0]&&revoke._noUi[0]&&revoke._noDistortionField[0]);
 }

 Fence f;Dx11wDx12SC bridge;bridge._copyFence=&f;bridge._copyAllocatorFenceValues={7};bridge._lastInteropCopyFenceValue=7;waiting=&f;
 assert(!bridge._WaitForCopyQueueIdle()&&bridge._copyAllocatorFenceValues[0]==7);
 f.arm=E_FAIL;assert(!bridge._WaitForCopyQueueIdle());f.arm=S_OK;
 waitResult=WAIT_FAILED;assert(!bridge._WaitForCopyAllocator(0));
 waitResult=WAIT_OBJECT_0;assert(!bridge._WaitForCopyQueueIdle()); // stale event is not completion
 finishOnWait=true;assert(bridge._WaitForCopyQueueIdle()&&bridge._copyAllocatorFenceValues[0]==0);
 bridge._copyCompletionUnknown=true;assert(!bridge._WaitForCopyQueueIdle());bridge._copyCompletionUnknown=false;
 bridge._copyAllocatorFenceValues[0]=8;f.completed=UINT64_MAX;finishOnWait=false;waitResult=WAIT_TIMEOUT;
 assert(!bridge._WaitForCopyAllocator(0));
 Queue queue;List list;IFGFeature_Dx12 fg;fg._scFence=&f;fg._gameCommandQueue=&queue;fg._scCommandList[0]=&list;
 fg._scCommandListResetted[0]=true;f.completed=0;fg._scPins={1};
 assert(!fg.SubmitSCCommandList(0)&&queue.executions==1&&fg._scAllocatorFenceValues[0]==1&&!fg._scPins.empty());
 assert(!fg.RetireSCWork());f.completed=1;assert(fg.RetireSCWork()&&fg._scPins.empty());
 fg._scCommandListResetted[0]=true;list.close=E_FAIL;assert(!fg.SubmitSCCommandList(0)&&queue.executions==1);
 list.close=S_OK;queue.result=E_FAIL;assert(!fg.SubmitSCCommandList(0)&&fg._scCompletionUnknown);f.completed=999;assert(!fg.RetireSCWork());
 {
  ScopedFgReentry first((void*)1,1);assert(!first.Recursive());
  ScopedFgReentry other((void*)2,1);assert(!other.Recursive());
  auto independent=std::async(std::launch::async,[]{ScopedFgReentry scope((void*)1,1);return !scope.Recursive();});assert(independent.get());
  ScopedFgReentry nested((void*)1,1);assert(nested.Recursive());
 }
 {ScopedFgReentry next((void*)1,1);assert(!next.Recursive());}
 using namespace Neurotic::Presentation;
 RECT dirty{0,3,4,4},scroll{0,0,4,3};POINT offset{0,-1};DXGI_PRESENT_PARAMETERS params{1,&dirty,&scroll,&offset};
 assert(!Plan(4,4,&params,false));auto plan=Plan(4,4,&params,true);assert(plan&&!plan->full&&plan->scroll);
 assert(plan->scroll->source.top==1&&plan->scroll->source.bottom==4&&plan->scroll->y==0);
 std::vector<int> previous(16),current(16,-1),actual(16);for(int i=0;i<16;++i)previous[i]=i;for(int i=12;i<16;++i)current[i]=100+i;
 for(int frame=0;frame<5;++frame){actual=previous;const auto r=*plan->scroll;
  for(int y=r.source.top;y<r.source.bottom;++y)for(int x=r.source.left;x<r.source.right;++x)actual[(r.y+y-r.source.top)*4+r.x+x-r.source.left]=previous[y*4+x];
  for(const auto& d:plan->dirty)for(int y=d.top;y<d.bottom;++y)for(int x=d.left;x<d.right;++x)actual[y*4+x]=current[y*4+x];
  for(int y=0;y<3;++y)for(int x=0;x<4;++x)assert(actual[y*4+x]==previous[(y+1)*4+x]);
  for(int x=0;x<4;++x)assert(actual[12+x]==current[12+x]);previous=actual;
 }
 params.DirtyRectsCount=0;assert(Plan(4,4,&params,false)->full);
 std::puts("PASS production interop/SC retirement failures, chain/thread reentry and incremental grid reconstruction");
}
'''
(out/'api-contracts.cpp').write_text(source)
(out/'run-contracts.cmd').write_text('@echo off\ncall "C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat" -vcvars_ver=14.44 >nul\ncl /nologo /std:c++20 /EHsc /W4 /DNOMINMAX builds/audit-api/api-contracts.cpp /Fo:builds/audit-api/ /Fe:builds/audit-api/api-contracts.exe\nif errorlevel 1 exit /b 1\nbuilds\\audit-api\\api-contracts.exe\n')
