// Real WARP pixels, production GpuSafety and NativeRecording hooks/observer.
// D3D12Hooks facades use the production state owners without game/global config.
#define NR_GPU_SAFETY_TEST
#define CHARACTER_EARLY_CAPTURE_TEST
#include "../../../OptiScaler/dlssnr/NrGpuSafety.cpp"
#include <nr/d3d12/NativeRecordingHooks.h>
#include "../../../OptiScaler/nr/semantic/character/CharacterEarlyCaptureDx12.cpp"
#include <dxgi1_6.h>
#include <d3d12sdklayers.h>
#include <iostream>
#include "character_test_errors.h"
using Microsoft::WRL::ComPtr;
using namespace Neurotic::Semantic::Character;
namespace Safety=DlssNr::GpuSafety;
static ComPtr<ID3D12RootSignature> hostRoot;
static ComPtr<ID3D12PipelineState> hostPipeline;
static ComPtr<ID3D12Resource> hostOutput;
static ComPtr<ID3D12DescriptorHeap> hostHeap;
static unsigned restores=0;
static bool allowCapture=true,allowRestore=true;
static thread_local unsigned suppressionDepth=0;
namespace Native=Neurotic::D3D12;
static auto& observer=*new Native::NativeRecordingObserver;
static auto& hooks=*new Native::NativeRecordingHooks(observer);
static void HR(HRESULT value){if(FAILED(value))throw std::runtime_error("D3D12 call failed");}
static void Require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
static void HostState(ID3D12GraphicsCommandList* list){
    list->SetComputeRootSignature(hostRoot.Get());list->SetPipelineState(hostPipeline.Get());
    auto* heap=hostHeap.Get();list->SetDescriptorHeaps(1,&heap);list->SetComputeRootUnorderedAccessView(0,hostOutput->GetGPUVirtualAddress());
    list->SetComputeRoot32BitConstant(1,0xaabbccddu,0);
}
std::optional<NativeStateRestorePoint> D3D12Hooks::CapturePostSrState(ID3D12GraphicsCommandList* list,
    Neurotic::D3D12::RestoreMask mask,Neurotic::D3D12::NativeStateCaptureDiagnostic*){
    using M=Neurotic::D3D12::RestoreMask;
    Require(mask==(M::Compute|M::Pipeline|M::Heaps|M::HeapInvalidatedTables),"capture mutation mask mismatch");
    if(!allowCapture)return {};
    const auto observed=observer.Observe(list);if(!observed.active||!observed.completeCoverage)return {};
    auto saved=observer.CaptureRestorable(list,mask,hooks.Originals());if(!saved)return {};
    return NativeStateRestorePoint{list,observed.incarnation,std::move(*saved)};
}
bool D3D12Hooks::RestorePostSrState(const NativeStateRestorePoint& state){
    Require(D3D12Hooks::LegacyCaptureSuppressed(),"legacy suppression ended before native restore");
    ++restores;if(!allowRestore)return false;
    const auto observed=observer.Observe(state.nativeList);
    if(!observed.active||!observed.completeCoverage||observed.incarnation!=state.incarnation)return false;
    return observer.Restore(state.nativeList,state.incarnation,state.state,hooks.Originals());
}
bool D3D12Hooks::RegisterNativeRootLayout(ID3D12RootSignature* signature,std::vector<Native::RootParameter> layout){
    return observer.RegisterLayout(signature,std::move(layout));
}
D3D12Hooks::ScopedLegacyCaptureSuppression::ScopedLegacyCaptureSuppression() noexcept {++suppressionDepth;}
D3D12Hooks::ScopedLegacyCaptureSuppression::~ScopedLegacyCaptureSuppression(){--suppressionDepth;}
bool D3D12Hooks::LegacyCaptureSuppressed() noexcept{return suppressionDepth!=0;}
struct Fixture {
    ComPtr<ID3D12Device> device;ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> fence;UINT64 value=0;
    ComPtr<ID3D12Resource> hostReadback;
    Fixture(){
        ComPtr<IDXGIFactory4> factory;HR(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
        ComPtr<IDXGIAdapter> adapter;HR(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
        HR(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
        D3D12_COMMAND_QUEUE_DESC q{};HR(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)));
        HR(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
        HR(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)));
        HR(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
        Require(Safety::PrepareRecordingHooks(list.Get()),"tracking install failed");
        Require(hooks.Install(list.Get()),"native command hooks install failed");
        Require(!observer.Observe(list.Get()).completeCoverage,"cold install fabricated recording coverage");
        HR(list->Close());HR(list->Reset(allocator.Get(),nullptr));
        Require(observer.Observe(list.Get()).completeCoverage,"real Reset failed to establish command coverage");
        D3D12_ROOT_PARAMETER p[2]{};p[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;p[0].Descriptor={0,0};
        p[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;p[1].Constants={0,0,1};
        D3D12_ROOT_SIGNATURE_DESC signature{2,p,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_NONE};ComPtr<ID3DBlob> blob,error;
        HR(D3D12SerializeRootSignature(&signature,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error));
        HR(device->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&hostRoot)));
        Require(hooks.RegisterRootSignature(hostRoot.Get(),blob->GetBufferPointer(),blob->GetBufferSize(),D3D12CreateVersionedRootSignatureDeserializer),
            "actual host root blob registration failed");
        const char shader[]="RWByteAddressBuffer Out:register(u0); cbuffer C:register(b0){uint Value;} [numthreads(1,1,1)] void main(){Out.Store(0,Value);}";
        ComPtr<ID3DBlob> cs;HR(D3DCompile(shader,sizeof(shader),nullptr,nullptr,nullptr,"main","cs_5_0",0,0,&cs,&error));
        D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline{};pipeline.pRootSignature=hostRoot.Get();pipeline.CS={cs->GetBufferPointer(),cs->GetBufferSize()};
        HR(device->CreateComputePipelineState(&pipeline,IID_PPV_ARGS(&hostPipeline)));
        D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=256;buffer.Height=1;
        buffer.DepthOrArraySize=1;buffer.MipLevels=1;buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        buffer.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;
        HR(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&hostOutput)));
        hp.Type=D3D12_HEAP_TYPE_READBACK;buffer.Flags=D3D12_RESOURCE_FLAG_NONE;
        HR(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&hostReadback)));
        D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,1,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,0};
        HR(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&hostHeap)));
        HostState(list.Get());
    }
    void Wait(){HR(queue->Signal(fence.Get(),++value));auto end=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        while(fence->GetCompletedValue()<value&&std::chrono::steady_clock::now()<end)std::this_thread::sleep_for(std::chrono::milliseconds(1));
        Require(fence->GetCompletedValue()!=UINT64_MAX&&fence->GetCompletedValue()>=value,"GPU completion timed out");}
    void Execute(){auto* commands=static_cast<ID3D12CommandList*>(list.Get());queue->ExecuteCommandLists(1,&commands);}
    void Reset(){HR(allocator->Reset());HR(list->Reset(allocator.Get(),nullptr));HostState(list.Get());}
    ComPtr<ID3D12Resource> Source(DXGI_FORMAT format,const float* rgba){
        ComPtr<ID3D12Resource> source;D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC td{};td.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;td.Width=1001;td.Height=563;
        td.DepthOrArraySize=1;td.MipLevels=1;td.Format=format;td.SampleDesc.Count=1;td.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        HR(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&td,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&source)));
        ComPtr<ID3D12DescriptorHeap> rtv;D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_RTV,1,D3D12_DESCRIPTOR_HEAP_FLAG_NONE,0};
        HR(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&rtv)));auto cpu=rtv->GetCPUDescriptorHandleForHeapStart();device->CreateRenderTargetView(source.Get(),nullptr,cpu);
        list->ClearRenderTargetView(cpu,rgba,0,nullptr);Transition(list.Get(),source.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        HR(list->Close());Execute();Wait();Reset();return source;
    }
    void CheckHost(){
        list->Dispatch(1,1,1);Transition(list.Get(),hostOutput.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
        list->CopyBufferRegion(hostReadback.Get(),0,hostOutput.Get(),0,4);
        Transition(list.Get(),hostOutput.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    void HostResult(){void* bytes=nullptr;D3D12_RANGE read{0,4};HR(hostReadback->Map(0,&read,&bytes));
        const auto word=*static_cast<const unsigned*>(bytes);D3D12_RANGE empty{0,0};hostReadback->Unmap(0,&empty);Require(word==0xaabbccddu,"caller root bindings/PSO not restored");}
};
int main(){try{
    CharacterTestQuietFailures();
    ComPtr<ID3D12Debug> debug;if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))debug->EnableDebugLayer();
    Fixture f;const float sdr[]{.25f,.5f,.75f,1};auto source=f.Source(DXGI_FORMAT_R8G8B8A8_UNORM,sdr);
    CharacterEarlyCaptureDx12 capture;CpuFrame metadata;metadata.key={{1,2,3,4,5,6,7,8},1,100};metadata.captureGeneration=capture.AdmissionToken();
    Require(!capture.TryRecord(f.list.Get(),source.Get(),metadata),"cold capture allocated GPU objects");capture.PrepareRequested();
    allowCapture=false;Require(!capture.TryRecord(f.list.Get(),source.Get(),metadata),"unrestorable state accepted");allowCapture=true;
    Require(capture.TryRecord(f.list.Get(),source.Get(),metadata),capture.Reason());Require(restores==1,"restore was not invoked");
    f.CheckHost();HR(f.list->Close());f.Execute();f.Wait();f.HostResult();CpuFrame out;
    Require(!capture.TryTakeNewest(out),"completed replayable list published pixels");
    Require(!capture.RetireSources(1),"completed replayable source prematurely released");
    f.Reset();Require(capture.TryTakeNewest(out),"reset/completed recording did not publish");
    Require(out.width==960&&out.height==539&&out.earlySource&&SameKey(out.key,metadata.key),"thumbnail metadata mismatch");
    for(size_t i=0;i<out.pixels.size();i+=4)Require(std::abs(int(out.pixels[i])-64)<=1&&std::abs(int(out.pixels[i+1])-128)<=1&&
        std::abs(int(out.pixels[i+2])-191)<=1&&out.pixels[i+3]==255,"SDR pixels mismatch");
    std::cout<<"PASS early SDR pixels, caller bindings, completion plus real reset\n";
    ++metadata.key.sequence;
    {
        D3D12Hooks::ScopedLegacyCaptureSuppression outer;
        Require(capture.TryRecord(f.list.Get(),source.Get(),metadata),capture.Reason());
        Require(D3D12Hooks::LegacyCaptureSuppressed(),"nested capture re-enabled outer legacy tracking");
    }
    Require(!D3D12Hooks::LegacyCaptureSuppressed(),"successful capture leaked legacy suppression");
    HR(f.list->Close());f.Reset();
    Require(!capture.TryTakeNewest(out)&&capture.RetireSources(1),"unsubmitted reset fabricated frame or retained source");
    ++metadata.key.sequence;Require(capture.TryRecord(f.list.Get(),source.Get(),metadata),capture.Reason());HR(f.list->Close());f.Execute();f.Wait();f.Execute();f.Wait();f.Reset();
    Require(!capture.TryTakeNewest(out)&&capture.RetireSources(1),"replayed recording published ambiguous frame");
    std::cout<<"PASS cancelled and replayed recordings discarded\n";
    const float hdr[]{2,4,8,1};auto linear=f.Source(DXGI_FORMAT_R16G16B16A16_FLOAT,hdr);
    ++metadata.key.sequence;metadata.colorPolicy=3;metadata.preExposure=2;
    Require(capture.TryRecord(f.list.Get(),linear.Get(),metadata),capture.Reason());HR(f.list->Close());f.Execute();f.Wait();f.Reset();
    Require(capture.TryTakeNewest(out),"HDR frame unavailable");
    const auto mapped=[](float v){return int(std::lround(std::pow(v/(1+v),1.f/2.2f)*255));};
    for(size_t i=0;i<out.pixels.size();i+=4)Require(std::abs(int(out.pixels[i])-mapped(1))<=1&&std::abs(int(out.pixels[i+1])-mapped(2))<=1&&
        std::abs(int(out.pixels[i+2])-mapped(4))<=1,"linear HDR pre-exposure conversion mismatch");
    std::cout<<"PASS linear HDR pre-exposure conversion\n";
    const auto oldToken=capture.AdmissionToken();capture.InvalidateWorkAdmission();metadata.captureGeneration=oldToken;
    Require(!capture.TryRecord(f.list.Get(),linear.Get(),metadata),"stale native-work admission accepted");metadata.captureGeneration=capture.AdmissionToken();
    ComPtr<ID3D12Fence> held;HR(f.device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&held)));HR(f.queue->Wait(held.Get(),1));
    for(unsigned i=0;i<3;++i){++metadata.key.sequence;Require(capture.TryRecord(f.list.Get(),linear.Get(),metadata),capture.Reason());}
    Require(!capture.TryRecord(f.list.Get(),linear.Get(),metadata),"bounded pool exceeded");HR(f.list->Close());f.Execute();
    Require(!capture.RetireSources(1),"pending GPU resources retired");HR(held->Signal(1));f.Wait();f.Reset();
    Require(capture.TryTakeNewest(out)&&out.key.sequence==metadata.key.sequence,"newest completion selection failed");
    std::cout<<"PASS stale admission, bounded pool and pending GPU retention\n";
    auto ephemeral=std::make_unique<CharacterEarlyCaptureDx12>();metadata.captureGeneration=ephemeral->AdmissionToken();
    Require(!ephemeral->TryRecord(f.list.Get(),linear.Get(),metadata),"ephemeral cold slot accepted");ephemeral->PrepareRequested();
    Require(ephemeral->TryRecord(f.list.Get(),linear.Get(),metadata),ephemeral->Reason());HR(f.list->Close());
    ephemeral.reset();f.Execute();f.Wait();f.Reset();
    std::cout<<"PASS owner destruction preserves recorded GPU backing\n";
    allowRestore=false;metadata.captureGeneration=capture.AdmissionToken();Require(!capture.TryRecord(f.list.Get(),linear.Get(),metadata),"failed restoration accepted");
    Require(capture.RestorationFailed(),"failed restoration not reported as fatal");allowRestore=true;
    Require(!D3D12Hooks::LegacyCaptureSuppressed(),"failed capture leaked legacy suppression");
    HR(f.list->Close());f.Reset();Require(capture.RetireSources(1),"failed unsubmitted recording did not retire after reset");
    std::cout<<"PASS restoration failure exposed to caller\n";
    ComPtr<ID3D12InfoQueue> messages;if(SUCCEEDED(f.device.As(&messages))){
        for(UINT64 i=0;i<messages->GetNumStoredMessages();++i){SIZE_T bytes=0;HR(messages->GetMessage(i,nullptr,&bytes));std::vector<unsigned char> memory(bytes);
            auto* message=reinterpret_cast<D3D12_MESSAGE*>(memory.data());HR(messages->GetMessage(i,message,&bytes));
            if(message->Severity==D3D12_MESSAGE_SEVERITY_CORRUPTION||message->Severity==D3D12_MESSAGE_SEVERITY_ERROR)
                throw std::runtime_error(message->pDescription);
        }
    }
    std::cout<<"PASS no D3D12 debug errors\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}
