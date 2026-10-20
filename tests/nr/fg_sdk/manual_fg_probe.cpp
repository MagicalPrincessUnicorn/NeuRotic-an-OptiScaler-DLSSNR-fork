// Explicit manual-only measurement. Build scripts NEVER execute this program.
#define NOMINMAX
#include <Windows.h>
#include <bcrypt.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <ffx_api/ffx_api.h>
#include <ffx_api/ffx_framegeneration.h>
#include <ffx_api/dx12/ffx_api_dx12.h>
#include "external/FidelityFX-CheckedClosure/CheckedExperimentalService.h"
#include <filesystem>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <mutex>
#include <cstdio>
namespace fs=std::filesystem;
using namespace ffx::nr;
static HANDLE logFile=INVALID_HANDLE_VALUE;
static std::mutex logMutex;
static uint64_t sequence=0;
static fs::path outputDirectory;
static void need(bool b,const char* why){if(!b)throw std::runtime_error(why);}
static void event(const char* name,int64_t result=0,const std::string& fields={}) {
 std::lock_guard lock(logMutex);std::ostringstream s;s<<"{\"schema_version\":1,\"process_id\":"<<GetCurrentProcessId()<<",\"sequence\":"<<++sequence<<",\"event\":\""<<name<<"\",\"result\":"<<result<<fields<<"}\n";
 auto text=s.str();DWORD n=0;need(WriteFile(logFile,text.data(),DWORD(text.size()),&n,nullptr)&&n==text.size()&&FlushFileBuffers(logFile),"event_write");
}
static void hr(HRESULT r,const char* stage){event(stage,int32_t(r));need(SUCCEEDED(r),stage);}
static std::string hex64(uint64_t n){std::ostringstream s;s<<std::hex<<std::setfill('0')<<std::setw(16)<<n;return s.str();}
static std::string hashFile(HANDLE file) {
 BCRYPT_ALG_HANDLE alg=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;DWORD length=0,n=0;
 need(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)==0,"hash_open");
 need(BCryptGetProperty(alg,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&length),sizeof(length),&n,0)==0,"hash_size");
 std::vector<UCHAR> object(length),data(65536);UCHAR digest[32]{};
 need(BCryptCreateHash(alg,&hash,object.data(),length,nullptr,0,0)==0,"hash_create");
 for(;;){need(ReadFile(file,data.data(),DWORD(data.size()),&n,nullptr)!=0,"hash_read");if(!n)break;need(BCryptHashData(hash,data.data(),n,0)==0,"hash_data");}
 need(BCryptFinishHash(hash,digest,32,0)==0,"hash_finish");BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(alg,0);
 std::ostringstream s;for(auto b:digest)s<<std::hex<<std::setfill('0')<<std::setw(2)<<unsigned(b);return s.str();
}
template<class T>static T symbol(HMODULE module,const char* name){auto p=GetProcAddress(module,name);need(p!=nullptr,name);HMODULE owner=nullptr;need(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(p),&owner)&&owner==module,"export_owner");return reinterpret_cast<T>(p);}
static std::string writeDomains(const FfxNrDrainHandleV1& drain,uint64_t revision,const std::vector<FfxNrDomainEvidenceV1>& domains) {
 std::ostringstream s;s<<"{\"schema_version\":1,\"process_id\":"<<GetCurrentProcessId()<<",\"owner_id\":"<<drain.context.owner_id<<",\"revision\":"<<revision<<",\"domains\":[";
 for(size_t i=0;i<domains.size();++i){const auto& d=domains[i];if(i)s<<',';s<<"{\"domain_kind\":"<<d.domain_kind<<",\"logical_role_bits\":"<<d.logical_role_bits<<",\"domain_id\":"<<d.domain_id<<",\"domain_generation\":"<<d.domain_generation<<",\"queue_id\":"<<d.queue_id<<",\"queue_generation\":"<<d.queue_generation<<",\"fence_id\":"<<d.fence_id<<",\"fence_generation\":"<<d.fence_generation<<",\"required_target\":"<<d.required_target<<",\"last_attempted_target\":"<<d.last_attempted_target<<",\"last_successful_target\":"<<d.last_successful_target<<",\"observed_completed_value\":"<<d.observed_completed_value<<",\"last_api_result\":"<<d.last_api_result<<",\"device_removed_reason\":"<<d.device_removed_reason<<",\"evidence_flags\":"<<d.evidence_flags<<'}';}
 s<<"]}\n";auto bytes=s.str();HANDLE file=CreateFileW((outputDirectory/L"fg-domains.json").c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);need(file!=INVALID_HANDLE_VALUE,"domain_file");DWORD n=0;need(WriteFile(file,bytes.data(),DWORD(bytes.size()),&n,nullptr)&&n==bytes.size()&&FlushFileBuffers(file),"domain_write");LARGE_INTEGER zero{};need(SetFilePointerEx(file,zero,nullptr,FILE_BEGIN)!=0,"domain_seek");auto digest=hashFile(file);CloseHandle(file);return digest;
}
struct Probe {
 ExperimentalServiceV1 api{};ffxContext fg=nullptr,sc=nullptr;FfxNrContextTicketV1 ticket{};
 decltype(&ffxDispatch) dispatch=nullptr;
 ID3D12Device* device=nullptr;ID3D12Resource* depth=nullptr;ID3D12Resource* motion=nullptr;
 ID3D12DescriptorHeap* cpu=nullptr;ID3D12DescriptorHeap* gpu=nullptr;ID3D12DescriptorHeap* rtv=nullptr;UINT stride=0;
 std::shared_ptr<OwnedOutputLease> output;std::shared_ptr<AlgorithmLease> algorithm;unsigned consumers=0,submissions=0;
 static HRESULT prepare(void* user,const std::shared_ptr<OwnedOutputLease>& lease,ID3D12GraphicsCommandList*) {
  auto& p=*static_cast<Probe*>(user);p.output=lease;auto view=lease->inspect();need(p.algorithm->Owns(lease)&&p.algorithm->inspect().registrationComplete,"registration");
  D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=1;hr(p.device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&p.rtv)),"rtv_created");p.device->CreateRenderTargetView(view.resource,nullptr,p.rtv->GetCPUDescriptorHandleForHeapStart());
  hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=2;hr(p.device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&p.cpu)),"cpu_heap_created");hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;hr(p.device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&p.gpu)),"gpu_heap_created");p.stride=p.device->GetDescriptorHandleIncrementSize(hd.Type);
  ID3D12Resource** resources[]={&p.depth,&p.motion};DXGI_FORMAT formats[]={DXGI_FORMAT_R32_FLOAT,DXGI_FORMAT_R16G16_FLOAT};
  for(unsigned i=0;i<2;++i){D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;rd.Width=256;rd.Height=144;rd.DepthOrArraySize=1;rd.MipLevels=1;rd.Format=formats[i];rd.SampleDesc.Count=1;rd.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
   hr(p.device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(resources[i])),i?"motion_created":"depth_created");
   D3D12_UNORDERED_ACCESS_VIEW_DESC u{};u.Format=formats[i];u.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;auto a=p.cpu->GetCPUDescriptorHandleForHeapStart();a.ptr+=i*p.stride;auto b=p.gpu->GetCPUDescriptorHandleForHeapStart();b.ptr+=i*p.stride;p.device->CreateUnorderedAccessView(*resources[i],nullptr,&u,a);p.device->CreateUnorderedAccessView(*resources[i],nullptr,&u,b);
  }
  event("writer_prepared",0,",\"allocation_generation\":"+std::to_string(view.allocationGeneration));return S_OK;
 }
 static HRESULT write(void* user,const std::shared_ptr<OwnedOutputLease>& lease,ID3D12GraphicsCommandList* list) {
  auto& p=*static_cast<Probe*>(user);auto v=lease->inspect();need(lease->beginWriterAction(list,v.writerRecording),"writer_action");
  D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition.pResource=v.resource;b.Transition.StateBefore=D3D12_RESOURCE_STATE_PRESENT;b.Transition.StateAfter=D3D12_RESOURCE_STATE_RENDER_TARGET;list->ResourceBarrier(1,&b);float color[]={.08f,.12f,.2f,1.f};list->ClearRenderTargetView(p.rtv->GetCPUDescriptorHandleForHeapStart(),color,0,nullptr);std::swap(b.Transition.StateBefore,b.Transition.StateAfter);list->ResourceBarrier(1,&b);
  list->SetDescriptorHeaps(1,&p.gpu);ID3D12Resource* resources[]={p.depth,p.motion};
  for(unsigned i=0;i<2;++i){auto cpu=p.cpu->GetCPUDescriptorHandleForHeapStart();cpu.ptr+=i*p.stride;auto gpu=p.gpu->GetGPUDescriptorHandleForHeapStart();gpu.ptr+=i*p.stride;float value[]={i?0.f:.5f,0,0,0};list->ClearUnorderedAccessViewFloat(gpu,cpu,resources[i],value,0,nullptr);b.Transition.pResource=resources[i];b.Transition.StateBefore=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;b.Transition.StateAfter=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;list->ResourceBarrier(1,&b);}
  ffxDispatchDescFrameGenerationPrepare d{};d.header.type=FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE;d.commandList=list;d.frameID=0;d.renderSize={256,144};d.motionVectorScale={256,144};d.frameTimeDelta=16.6667f;d.cameraNear=.1f;d.cameraFar=1000;d.cameraFovAngleVertical=1;d.viewSpaceToMetersFactor=1;d.depth=ffxApiGetResourceDX12(p.depth);d.motionVectors=ffxApiGetResourceDX12(p.motion);
  const auto result=p.dispatch(&p.fg,&d.header);event("algorithm_prepare_returned",result);lease->endAction();return result==FFX_API_RETURN_OK?S_OK:E_FAIL;
 }
 static HRESULT writerBegin(void*,const std::shared_ptr<OwnedOutputLease>&,void** guard){*guard=nullptr;return S_OK;}
 static void writerEnd(void*,void*,const OwnedWriteReceipt& r){event("writer_submitted",r.signalResult,",\"execute_invoked\":"+std::to_string(r.executeInvoked?1:0)+",\"target\":"+std::to_string(r.target));}
 static FfxNrStatusV1 consumerBegin(void* user,const std::shared_ptr<OwnedOutputLease>& output,const std::shared_ptr<AlgorithmLease>& algorithm,const FfxNrDispatchTicketV1* t,void** guard){auto& p=*static_cast<Probe*>(user);if(!algorithm->Owns(output)||!algorithm->inspect().registrationComplete||!output->beginConsumerAction(*t))return FFX_NR_UNSUPPORTED;*guard=output.get();++p.consumers;event("algorithm_consumer_entered",0,",\"dispatch_id\":"+std::to_string(t->dispatch_id)+",\"allocation_generation\":"+std::to_string(output->inspect().allocationGeneration)+",\"algorithm_generation\":"+std::to_string(algorithm->inspect().algorithmGeneration));return FFX_NR_COMPLETE;}
 static void consumerEnd(void*,void* guard,int32_t result){static_cast<OwnedOutputLease*>(guard)->endAction();event("algorithm_dispatch_returned",result);}
 static FfxNrStatusV1 submitBegin(void* user,const FfxNrDispatchTicketV1* t,void** guard){auto& p=*static_cast<Probe*>(user);if(!p.output->beginConsumerAction(*t))return FFX_NR_BUSY;*guard=p.output.get();return FFX_NR_COMPLETE;}
 static void submitEnd(void* user,void* guard,const FfxNrSubmitResultV1* r){auto& p=*static_cast<Probe*>(user);if(guard)static_cast<OwnedOutputLease*>(guard)->endAction();FfxNrSubmitResultV1 kept{};need(p.output->inspectSubmission(kept)&&kept.dispatch.dispatch_id==r->dispatch.dispatch_id,"retained_submission");++p.submissions;event("algorithm_submitted",r->signal_result,",\"dispatch_id\":"+std::to_string(r->dispatch.dispatch_id)+",\"callback_result\":"+std::to_string(r->callback_result)+",\"close_result\":"+std::to_string(r->close_result)+",\"execute_invoked\":"+std::to_string(r->execute_invoked)+",\"output_published\":"+std::to_string(r->output_published));}
};
int wmain(int argc,wchar_t** argv) {
 fs::path output,sdk;std::string expected;uint64_t requested=0;bool consent=false,warp=false,luidGiven=false;
 try {
  for(int i=1;i<argc;++i){std::wstring a=argv[i];auto value=[&](){need(i+1<argc,"missing_argument");return std::wstring(argv[++i]);};if(a==L"--run-manual-full-algorithm")consent=true;else if(a==L"--warp")warp=true;else if(a==L"--output")output=value();else if(a==L"--sdk")sdk=value();else if(a==L"--sdk-sha256"){auto v=value();expected.assign(v.begin(),v.end());}else if(a==L"--adapter-luid"){auto v=value();size_t read=0;requested=std::stoull(v,&read,16);need(read==v.size()&&v.size()<=16,"luid_format");luidGiven=true;}else throw std::runtime_error("unknown_argument");}
  need(consent&&warp!=luidGiven&&output.is_absolute()&&sdk.is_absolute()&&expected.size()==64,"explicit_manual_arguments_required");for(char c:expected)need((c>='0'&&c<='9')||(c>='a'&&c<='f'),"sha_format");
  if(!fs::exists(output))fs::create_directory(output);outputDirectory=output;logFile=CreateFileW((output/L"fg-events.jsonl").c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);need(logFile!=INVALID_HANDLE_VALUE,"fresh_events_required");event("manual_consent");
  HANDLE pinned=CreateFileW(sdk.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);need(pinned!=INVALID_HANDLE_VALUE,"pin_sdk");need(hashFile(pinned)==expected,"sdk_hash_mismatch");event("module_verified",0,",\"sdk_sha256\":\""+expected+"\"");
  HMODULE module=LoadLibraryExW(sdk.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);need(module!=nullptr,"load_sdk");
  auto create=symbol<decltype(&ffxCreateContext)>(module,"ffxCreateContext");auto destroy=symbol<decltype(&ffxDestroyContext)>(module,"ffxDestroyContext");auto configure=symbol<decltype(&ffxConfigure)>(module,"ffxConfigure");Probe p;p.dispatch=symbol<decltype(&ffxDispatch)>(module,"ffxDispatch");
  p.api.manualOptIn=ExperimentalManualOptInV1;auto query=symbol<decltype(&ffxNeuRoticQueryExperimentalServiceV1)>(module,"ffxNeuRoticQueryExperimentalServiceV1");need(query(&p.api)==FFX_NR_COMPLETE,"experimental_service");event("experimental_service_selected");
  IDXGIFactory4* factory=nullptr;IDXGIAdapter1* adapter=nullptr;hr(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"factory_created");if(warp)hr(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)),"adapter_created");else{LUID luid{DWORD(requested),LONG(requested>>32)};hr(factory->EnumAdapterByLuid(luid,IID_PPV_ARGS(&adapter)),"adapter_created");}
  DXGI_ADAPTER_DESC1 ad{};hr(adapter->GetDesc1(&ad),"adapter_description");uint64_t actual=(uint64_t(uint32_t(ad.AdapterLuid.HighPart))<<32)|ad.AdapterLuid.LowPart;need(warp||actual==requested,"adapter_identity");event("adapter_selected",0,",\"adapter_luid\":\""+hex64(actual)+"\",\"warp\":"+(warp?"true":"false"));
  hr(D3D12CreateDevice(adapter,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&p.device)),"device_created");ID3D12CommandQueue* queue=nullptr;D3D12_COMMAND_QUEUE_DESC q{};hr(p.device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)),"queue_created");
  WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"W03ManualFgProbe";need(RegisterClassW(&wc)!=0,"window_class");HWND window=CreateWindowW(wc.lpszClassName,L"Explicit manual FG measurement",WS_OVERLAPPEDWINDOW,0,0,320,200,nullptr,nullptr,wc.hInstance,nullptr);need(window!=nullptr,"window");
  IDXGISwapChain4* swapchain=nullptr;DXGI_SWAP_CHAIN_DESC1 sd{};sd.Width=256;sd.Height=144;sd.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;sd.SampleDesc.Count=1;sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;sd.BufferCount=2;sd.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
  ffxCreateContextDescFrameGenerationSwapChainForHwndDX12 sc{};sc.header.type=FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_FOR_HWND_DX12;sc.swapchain=&swapchain;sc.hwnd=window;sc.desc=&sd;sc.dxgiFactory=factory;sc.gameQueue=queue;auto result=create(&p.sc,&sc.header,nullptr);event("swapchain_created",result);need(result==0,"swapchain_create");
  ffxCreateBackendDX12Desc backend{};backend.header.type=FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;backend.device=p.device;ffxCreateContextDescFrameGeneration fg{};fg.header.type=FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;fg.header.pNext=&backend.header;fg.displaySize={256,144};fg.maxRenderSize={256,144};fg.backBufferFormat=FFX_API_SURFACE_FORMAT_R16G16B16A16_FLOAT;result=create(&p.fg,&fg.header,nullptr);event("algorithm_created",result);need(result==0,"algorithm_create");
  AlgorithmConsumerObserver consumer{&p,Probe::consumerBegin,Probe::consumerEnd};need(p.api.get_context(swapchain,&p.ticket)==FFX_NR_COMPLETE,"context_identity");need(p.api.select_algorithm(p.fg,swapchain,&consumer,&p.algorithm)==FFX_NR_COMPLETE,"algorithm_select");event("algorithm_bound",0,",\"algorithm_generation\":"+std::to_string(p.algorithm->inspect().algorithmGeneration));
  ffxConfigureDescFrameGeneration config{};config.header.type=FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;config.swapChain=swapchain;config.frameGenerationEnabled=true;config.generationRect={0,0,256,144};result=configure(&p.fg,&config.header);event("algorithm_configured",result);need(result==0,"algorithm_configure");
  FfxNrSubmissionObserverV1 observer{sizeof(observer),1,&p,Probe::submitBegin,Probe::submitEnd};
  const auto observerResult=p.api.set_submit_observer(&p.ticket,&observer);
  event("submission_observer_returned",observerResult);need(observerResult==FFX_NR_COMPLETE,"submit_observer");
  FfxNrEnrollRequestV1 enroll{sizeof(enroll),1,0xF65CDD1201001003ULL,ControlledOutputEnrollment,0};
  const auto enrollmentResult=p.api.enroll(swapchain,&enroll,&p.ticket);
  event("sdk_enrollment_returned",enrollmentResult);need(enrollmentResult==FFX_NR_COMPLETE,"sdk_enroll");
  event("sdk_enrolled",0,",\"owner_id\":"+std::to_string(p.ticket.owner_id));
  OwnedWriterCallbacks writer{Probe::prepare,Probe::write,Probe::writerBegin,Probe::writerEnd};OwnedWriteReceipt write{};need(p.api.write_owned_output(&p.ticket,&writer,&p,&write)==FFX_NR_COMPLETE,"owned_writer");
  OwnedWriterRetirementReceipt retired{};auto limit=GetTickCount64()+10000;FfxNrStatusV1 status;do{status=p.api.finish_writer(&p.ticket,&retired);if(status==FFX_NR_PENDING)Sleep(1);}while(status==FFX_NR_PENDING&&GetTickCount64()<limit);need(status==FFX_NR_COMPLETE&&retired.completed&&retired.oldRecordingRetired,"writer_retirement");event("writer_retired",0,",\"old_recording\":"+std::to_string(retired.oldRecording)+",\"new_recording\":"+std::to_string(retired.newRecording));
  hr(swapchain->Present(0,0),"present_returned");need(p.consumers==1&&p.submissions==1,"exact_callback_counts");
  LARGE_INTEGER now{},frequency{};QueryPerformanceCounter(&now);QueryPerformanceFrequency(&frequency);FfxNrDrainRequestV1 request{sizeof(request),1,p.ticket,1,now.QuadPart+frequency.QuadPart*10,1,0};FfxNrDrainHandleV1 handle{};FfxNrDrainSummaryV1 summary{sizeof(summary),1};status=p.api.begin(&request,&handle,&summary);
  do{if(status!=FFX_NR_PENDING&&status!=FFX_NR_BUSY)break;Sleep(1);status=p.api.poll(&handle,request.absolute_deadline_qpc,&summary);}while(GetTickCount64()<limit+10000);
  need(status==FFX_NR_COMPLETE&&summary.terminated_worker_count==2,"sdk_drain");std::vector<FfxNrDomainEvidenceV1> domains(summary.domain_count);uint32_t written=0;need(p.api.read_domains(&handle,summary.evidence_revision,0,domains.data(),uint32_t(domains.size()),&written)==FFX_NR_COMPLETE&&written==domains.size(),"domain_inventory");auto domainsHash=writeDomains(handle,summary.evidence_revision,domains);for(const auto& d:domains)need(d.observed_completed_value!=UINT64_MAX&&d.observed_completed_value>=d.required_target&&d.last_successful_target>=d.required_target&&d.device_removed_reason>=0,"domain_complete");event("sdk_drain_complete",0,",\"owner_id\":"+std::to_string(handle.context.owner_id)+",\"revision\":"+std::to_string(summary.evidence_revision)+",\"domain_count\":"+std::to_string(written)+",\"workers_terminated\":2,\"domains_sha256\":\""+domainsHash+"\"");
  hr(p.device->GetDeviceRemovedReason(),"device_status");
  AlgorithmReleaseReceipt release{};const auto releaseStatus=p.algorithm->releaseAfterDrain(handle,summary.evidence_revision,release);
  const auto releaseFields=",\"algorithm_generation\":"+std::to_string(release.algorithmGeneration)+",\"revision\":"+std::to_string(release.evidenceRevision)+",\"cleanup_operations\":"+std::to_string(release.cleanupOperations);
  event("algorithm_release_returned",releaseStatus,releaseFields+",\"actual_result\":"+std::to_string(release.actualResult)+",\"whole_context_destroyed\":"+std::to_string(release.wholeContextDestroyed?1:0));
  need(releaseStatus==FFX_NR_COMPLETE&&release.wholeContextDestroyed,"algorithm_release");event("algorithm_destroyed",release.actualResult,releaseFields);
  need(p.api.consume(&handle,summary.evidence_revision)==FFX_NR_COMPLETE,"sdk_consume");event("sdk_consumed");result=destroy(&p.sc,nullptr);event("swapchain_context_destroyed",result);need(result==0,"swapchain_destroy");swapchain->Release();event("probe_complete",0,",\"qualification_granted\":false");
  // The loaded module and retained leases live until process termination.
  CloseHandle(logFile);logFile=INVALID_HANDLE_VALUE;return 0;
 }catch(const std::exception& error){std::fprintf(stderr,"FG_PROBE_FAILURE: %s\n",error.what());std::fflush(stderr);if(logFile!=INVALID_HANDLE_VALUE){try{event("probe_failed",-1,",\"uncertainty_retained\":true");}catch(...){}CloseHandle(logFile);}return 2;}
}
