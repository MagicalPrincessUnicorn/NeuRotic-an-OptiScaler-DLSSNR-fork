#pragma once
// Owned hidden-App measurement only. No physical input, screenshots or worker.
#include <array>
#include <thread>
#include <atomic>
#include <chrono>
#include <stdexcept>
#include <fstream>
#include <d3d11.h>
#include "ui/HubViewModel.h"
namespace nh {
using Microsoft::WRL::ComPtr;
struct ResizeTimingFixture {
 static constexpr UINT Sentinel=WM_APP+73;
 enum Phase {Timer,Exit,Resize,Model,Artwork,Ui,Remember,Editor,Present,PhaseCount};
 static constexpr const char* Names[]={"timer","exit","resize","model","artwork","ui","remember","editor","present"};
 struct Timing {double maximum=0,total=0;unsigned count=0;};
 struct Input {long long queued=0,dispatched=0;unsigned count=0;HWND capture=nullptr;};
 std::array<Timing,PhaseCount> timings{};
 std::array<Input,3> input{};
 std::array<unsigned,3> order{};unsigned orderCount=0;
 unsigned resizeCount=0,rememberCount=0,rememberExtentChanges=0,reentrantCount=0;
 UINT rememberedWidth=0,rememberedHeight=0;
 HRESULT lastPresent=S_OK;unsigned presentFailures=0;
 unsigned renderErrors=0,skippedPresents=0;UINT lastSync=0,lastFlags=0;std::string lastRenderError,control;HWND ownedWindow=nullptr;
 unsigned ordinaryPresentCount=0;UINT lastOrdinarySync=0,lastOrdinaryFlags=0;
 HANDLE start=nullptr,queued=nullptr;std::thread producer;
 bool measuring=false,armed=false,dropRelease=false,slow=false;
 bool pumpTimedOut=false;
 bool producerOk=false;std::atomic<bool> enqueued=false;long long frequency=0;
 ResizeTimingFixture(){LARGE_INTEGER f;QueryPerformanceFrequency(&f);frequency=f.QuadPart;}
 ~ResizeTimingFixture(){if(start)SetEvent(start);if(producer.joinable())producer.join();if(start)CloseHandle(start);if(queued)CloseHandle(queued);}
 static long long Now(){LARGE_INTEGER t;QueryPerformanceCounter(&t);return t.QuadPart;}
 double Milliseconds(long long elapsed)const{return 1000.*elapsed/frequency;}
 struct Scope {ResizeTimingFixture* owner=nullptr;Phase phase=Timer;long long begin=0;Scope(ResizeTimingFixture* o,Phase p):owner(o&&o->measuring?o:nullptr),phase(p),begin(owner?Now():0){} ~Scope(){if(owner){auto ms=owner->Milliseconds(Now()-begin);auto& t=owner->timings[phase];++t.count;t.total+=ms;t.maximum=std::max(t.maximum,ms);}}};
 void Observe(UINT msg){if(!measuring)return;int i=msg==WM_LBUTTONDOWN?0:msg==WM_LBUTTONUP?1:msg==Sentinel?2:-1;if(i<0)return;auto& in=input[i];in.dispatched=Now();++in.count;in.capture=GetCapture();if(orderCount<order.size())order[orderCount++]=unsigned(i);}
 void BeforeTimer(){if(!armed)return;armed=false;SetEvent(start);if(WaitForSingleObject(queued,2000)!=WAIT_OBJECT_0||!enqueued.load(std::memory_order_acquire))throw std::runtime_error("Resize fixture producer did not enqueue");if(slow)Sleep(250);}
 void NoteRemember(UINT width,UINT height){if(!measuring)return;++rememberCount;if(width!=rememberedWidth||height!=rememberedHeight){++rememberExtentChanges;rememberedWidth=width;rememberedHeight=height;}}
 void NotePresent(HRESULT hr){if(!measuring)return;lastPresent=hr;if(FAILED(hr))++presentFailures;}
 void RecordFailure(const char* message){++renderErrors;lastRenderError.assign(message,std::min<size_t>(strlen(message),256));}
 void NoteSkippedPresent(){if(measuring)++skippedPresents;}
 HRESULT PresentCall(IDXGISwapChain* chain,UINT sync,UINT flags){
  if(measuring){lastSync=sync;lastFlags=flags;
   if(control=="busy")return DXGI_ERROR_WAS_STILL_DRAWING;
   if(control=="device-loss")return DXGI_ERROR_DEVICE_REMOVED;
   if(control=="reentrant")SendMessageW(ownedWindow,WM_TIMER,1,0);
   if(control=="blocked-present"){if(sync||!(flags&DXGI_PRESENT_DO_NOT_WAIT)){Sleep(250);return S_OK;}return DXGI_ERROR_WAS_STILL_DRAWING;}
  }else{++ordinaryPresentCount;lastOrdinarySync=sync;lastOrdinaryFlags=flags;}
  return chain->Present(sync,flags);
 }
 void Begin(HWND window,const std::string& requestedControl){
  timings={};input={};order={};orderCount=resizeCount=rememberCount=rememberExtentChanges=reentrantCount=presentFailures=0;lastPresent=S_OK;
  control=requestedControl;ownedWindow=window;pumpTimedOut=false;renderErrors=skippedPresents=ordinaryPresentCount=0;lastRenderError.clear();lastSync=lastFlags=lastOrdinarySync=lastOrdinaryFlags=0;slow=control=="slow";dropRelease=control=="drop-release";producerOk=false;enqueued=false;start=CreateEventW(nullptr,TRUE,FALSE,nullptr);queued=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!start||!queued)throw std::runtime_error("Resize fixture event allocation failed");measuring=armed=true;
  producer=std::thread([this,window]{if(WaitForSingleObject(start,2000)!=WAIT_OBJECT_0){SetEvent(queued);return;}bool ok=true;input[0].queued=Now();ok=PostMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(4,4))&&ok;input[1].queued=Now();if(!dropRelease)ok=PostMessageW(window,WM_LBUTTONUP,0,MAKELPARAM(4,4))&&ok;input[2].queued=Now();ok=PostMessageW(window,Sentinel,0,0)&&ok;producerOk=ok;enqueued.store(true,std::memory_order_release);SetEvent(queued);});
 }
 void End(){measuring=armed=false;if(producer.joinable())producer.join();CloseHandle(start);CloseHandle(queued);start=queued=nullptr;}
 Json Result(HWND window,UINT requestedWidth,UINT requestedHeight,UINT appliedWidth,UINT appliedHeight){
  Json phases=Json::object();for(unsigned i=0;i<PhaseCount;++i)phases[Names[i]]={{"count",timings[i].count},{"maximumMs",timings[i].maximum},{"totalMs",timings[i].total}};
  Json inputs=Json::array();bool timely=true;for(unsigned i=0;i<input.size();++i){const auto& in=input[i];double lag=in.dispatched?Milliseconds(in.dispatched-in.queued):0;inputs.push_back({{"kind",i==0?"down":i==1?"up":"sentinel"},{"count",in.count},{"queueToDispatchMs",in.dispatched?Json(lag):Json(nullptr)},{"captureOwnedAfterDispatch",in.capture==window}});timely=timely&&in.count==1&&lag<=100.;}
  const bool ordered=orderCount==3&&order[0]==0&&order[1]==1&&order[2]==2;
  const bool released=input[1].count==1&&input[1].capture!=window&&GetCapture()!=window;
  const bool geometry=appliedWidth==requestedWidth&&appliedHeight==requestedHeight;
  const bool boundedPresentation=(control!="busy"&&control!="blocked-present")||(lastSync==0&&(lastFlags&DXGI_PRESENT_DO_NOT_WAIT)&&skippedPresents>0);
  const bool reentryRejected=control!="reentrant"||reentrantCount>0;
  const bool imguiReleased=!ImGui::GetIO().MouseDown[0];
  const bool ordinaryRestored=ordinaryPresentCount>0&&lastOrdinarySync==1&&lastOrdinaryFlags==0;
  return {{"phases",phases},{"input",inputs},{"budgetMs",100},{"timely",timely},{"orderedExactlyOnce",ordered},{"captureReleased",released},{"imguiMouseReleased",imguiReleased},{"pumpTimedOut",pumpTimedOut},{"requestedExtent",{requestedWidth,requestedHeight}},{"appliedExtent",{appliedWidth,appliedHeight}},{"latestGeometryApplied",geometry},{"resizeCount",resizeCount},{"rememberCopies",rememberCount},{"rememberExtentChanges",rememberExtentChanges},{"reentrantRenderAttempts",reentrantCount},{"lastPresentHresult",static_cast<long long>(lastPresent)},{"presentFailureCount",presentFailures},{"renderErrors",renderErrors},{"lastRenderError",lastRenderError},{"skippedPresents",skippedPresents},{"lastPresentSyncInterval",lastSync},{"lastPresentFlags",lastFlags},{"boundedBusyPresentation",boundedPresentation},{"reentryRejected",reentryRejected},{"ordinaryPresentCount",ordinaryPresentCount},{"lastOrdinarySyncInterval",lastOrdinarySync},{"lastOrdinaryFlags",lastOrdinaryFlags},{"ordinaryPresentationRestored",ordinaryRestored},{"passed",producerOk&&timely&&ordered&&released&&imguiReleased&&!pumpTimedOut&&geometry&&boundedPresentation&&reentryRejected&&ordinaryRestored&&renderErrors==0}};
 }
};
// Exercises the real App WndProc/backend/renderFrame. Synthetic sends are NOT
// a native border drag or physical pointer qualification. Receipts contain only
// timings, geometry and state; storage is fixed per scenario (nine timings/three inputs).
inline int RunResizeTimingFixture(HWND window,IDXGISwapChain* chain,ResizeTimingFixture& probe,
 const std::function<void()>& render,HubModel& model,const std::filesystem::path& directory,const std::string& control){
 ComPtr<IDXGIDevice> dxgiDevice;ComPtr<IDXGIAdapter> adapter;DXGI_ADAPTER_DESC adapterDesc{};
 // GetDevice cannot return IDXGIAdapter directly.
 if(SUCCEEDED(chain->GetDevice(IID_PPV_ARGS(&dxgiDevice)))&&SUCCEEDED(dxgiDevice->GetAdapter(&adapter)))adapter->GetDesc(&adapterDesc);
 Json results=Json::array();bool passed=true;
 model.loaded=false;model.anythingUi.preferencesLoaded=true;
 for(bool reduced:{false,true})for(bool anythingPage:{false,true})for(bool vertical:{false,true}){
  model.reducedMotion=reduced;model.page=anythingPage?2:1;
  for(int warm=0;warm<3;++warm)render();
  const UINT width=vertical?1040:1080,height=vertical?790:760;
  probe.Begin(window,control);
  SendMessageW(window,WM_ENTERSIZEMOVE,0,0);
  // Multiple requests exercise latest-extent coalescing without user input.
  SendMessageW(window,WM_SIZE,SIZE_RESTORED,MAKELPARAM(width-8,height-8));
  SendMessageW(window,WM_SIZE,SIZE_RESTORED,MAKELPARAM(width,height));
  SendMessageW(window,WM_TIMER,1,0);
  MSG msg{};const auto deadline=GetTickCount64()+2000;
  while(probe.input[2].count==0&&GetTickCount64()<deadline){while(probe.input[2].count==0&&GetTickCount64()<deadline&&PeekMessageW(&msg,window,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}if(probe.input[2].count==0)Sleep(1);}
  probe.pumpTimedOut=probe.input[2].count==0;
  SendMessageW(window,WM_EXITSIZEMOVE,0,0);
  probe.measuring=false;
  try{render();}catch(const std::exception& error){probe.RecordFailure(error.what());}
  // ImGui deliberately trickles down/up queued in one backend batch across
  // frames. Observe final state after at most two ordinary frames, separately
  // from the producer-to-backend service timing; do not alter that policy.
  for(unsigned drain=0;drain<2&&ImGui::GetIO().MouseDown[0];++drain){try{render();}catch(const std::exception& error){probe.RecordFailure(error.what());}}
  ComPtr<ID3D11Texture2D> buffer;D3D11_TEXTURE2D_DESC desc{};if(SUCCEEDED(chain->GetBuffer(0,IID_PPV_ARGS(&buffer))))buffer->GetDesc(&desc);
  auto row=probe.Result(window,width,height,desc.Width,desc.Height);row["reducedMotion"]=reduced;row["page"]=anythingPage?"NR Anything idle":"Library idle";row["direction"]=vertical?"vertical":"horizontal";passed=passed&&row["passed"].get<bool>();results.push_back(std::move(row));
  probe.End();if(GetCapture()==window)ReleaseCapture(); // Only this fixture's owned window, after the oracle records failure.
 }
 Json receipt={{"synthetic",true},{"physicalPointerQualified",false},{"workerStarted",false},{"screenshotsWritten",false},{"control",control},{"adapterVendor",adapterDesc.VendorId},{"adapterDevice",adapterDesc.DeviceId},{"adapterLuidHigh",adapterDesc.AdapterLuid.HighPart},{"adapterLuidLow",adapterDesc.AdapterLuid.LowPart},{"scenarios",results},{"passed",passed}};
 std::ofstream(directory/L"resize-timing.json")<<receipt.dump(2);return passed?0:1;
}
}
