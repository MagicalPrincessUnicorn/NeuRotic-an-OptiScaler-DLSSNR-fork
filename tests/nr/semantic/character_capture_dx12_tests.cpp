#include "nr/semantic/character/CharacterCaptureDx12.h"
#include <dxgi1_6.h>
#include <d3d12sdklayers.h>
#include <chrono>
#include <iostream>
#include <thread>
#include "character_test_errors.h"
using namespace Neurotic::Semantic::Character;
using Microsoft::WRL::ComPtr;
static void HR(HRESULT r){if(FAILED(r))throw std::runtime_error("D3D12 operation failed");}
static std::atomic<bool> preparationEntered{false};
static void SlowPreparation(){preparationEntered=true;std::this_thread::sleep_for(std::chrono::milliseconds(350));}
int main(){try{
    CharacterTestQuietFailures();
    std::cerr<<"capture host start\n";
    ComPtr<ID3D12Debug> debug;const bool debugAvailable=SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));
    if(debugAvailable)debug->EnableDebugLayer();
    ComPtr<IDXGIFactory4> factory;HR(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
    ComPtr<IDXGIAdapter> adapter;HR(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
    ComPtr<ID3D12Device> device;HR(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
    ComPtr<ID3D12InfoQueue> diagnostic;
    if(SUCCEEDED(device.As(&diagnostic)))for(auto severity:{D3D12_MESSAGE_SEVERITY_CORRUPTION,D3D12_MESSAGE_SEVERITY_ERROR,D3D12_MESSAGE_SEVERITY_WARNING})diagnostic->SetBreakOnSeverity(severity,FALSE);
    ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC q{};HR(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)));
    ComPtr<ID3D12CommandAllocator> allocator;HR(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
    ComPtr<ID3D12GraphicsCommandList> list;HR(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)));
    ComPtr<ID3D12DescriptorHeap> rtv;D3D12_DESCRIPTOR_HEAP_DESC heap{D3D12_DESCRIPTOR_HEAP_TYPE_RTV,1,D3D12_DESCRIPTOR_HEAP_FLAG_NONE,0};HR(device->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&rtv)));
    ComPtr<ID3D12Resource> source;D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC td{};td.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;td.Width=1001;td.Height=563;td.DepthOrArraySize=1;td.MipLevels=1;td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.SampleDesc.Count=1;td.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    HR(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&td,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&source)));
    auto handle=rtv->GetCPUDescriptorHandleForHeapStart();device->CreateRenderTargetView(source.Get(),nullptr,handle);
    const float color[]{.25f,.5f,.75f,1};list->ClearRenderTargetView(handle,color,0,nullptr);
    D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={source.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PRESENT};list->ResourceBarrier(1,&b);HR(list->Close());
    auto* native=static_cast<ID3D12CommandList*>(list.Get());queue->ExecuteCommandLists(1,&native);
    CharacterCaptureDx12 capture;
    std::cerr<<"capture host device ready\n";
    CpuFrame metadata{{{1,2,3,4,5,6,7,8},1,100},0,0,0,4,{}};
    if(capture.TrySubmit(queue.Get(),source.Get(),metadata))throw std::runtime_error("cold Present allocated or submitted work");
    capture.PrepareRequested();
    if(!capture.TrySubmit(queue.Get(),source.Get(),metadata))throw std::runtime_error("warmed capture submission rejected");
    std::cerr<<"capture host initial submission\n";
    CpuFrame out;const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(!capture.TryTakeNewest(out)&&std::chrono::steady_clock::now()<end)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if(out.width!=960||!out.height||out.stride!=out.width*4||out.pixels.size()!=static_cast<std::size_t>(out.stride)*out.height||!SameKey(out.key,metadata.key))throw std::runtime_error("footprint/identity mismatch");
    for(std::size_t i=0;i<out.pixels.size();i+=4)if(std::abs(static_cast<int>(out.pixels[i])-64)>1||std::abs(static_cast<int>(out.pixels[i+1])-128)>1||std::abs(static_cast<int>(out.pixels[i+2])-191)>1||out.pixels[i+3]!=255)throw std::runtime_error("thumbnail pixel/channel mismatch");
    // Admission invalidation must not wait behind allocation/map ownership.
    const auto beforeWork=capture.AdmissionToken();
    capture.SetPreparationTestHook(SlowPreparation);
    std::thread preparing([&]{capture.PrepareRequested();});
    const auto prepareDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while(!preparationEntered&&std::chrono::steady_clock::now()<prepareDeadline)std::this_thread::yield();
    const auto invalidationStart=std::chrono::steady_clock::now();capture.InvalidateWorkAdmission();
    const auto invalidationTime=std::chrono::steady_clock::now()-invalidationStart;
    preparing.join();capture.SetPreparationTestHook(nullptr);
    if(!preparationEntered||invalidationTime>std::chrono::milliseconds(100)||capture.AdmissionToken()==beforeWork)
        throw std::runtime_error("native FG admission waited behind control preparation");
    metadata.captureGeneration=beforeWork;
    if(capture.TrySubmit(queue.Get(),source.Get(),metadata))throw std::runtime_error("native FG accepted stale admission after preparation");
    metadata.captureGeneration=capture.AdmissionToken();
    // Three submitted slots remain unavailable while their queue is deliberately held.
    ComPtr<ID3D12Fence> hold;HR(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&hold)));HR(queue->Wait(hold.Get(),1));
    std::cerr<<"capture host initial pixels verified\n";
    for(unsigned i=0;i<3;++i){metadata.key.sequence=2+i;if(!capture.TrySubmit(queue.Get(),source.Get(),metadata))throw std::runtime_error("three-slot capacity missing");}
    if(capture.TrySubmit(queue.Get(),source.Get(),metadata))throw std::runtime_error("in-flight slot reused");
    if(capture.RetireSources(1))throw std::runtime_error("resize freed pending sources");
    HR(hold->Signal(1));
    if(!capture.RetireSources(500))throw std::runtime_error("resize did not retire completed sources");
    unsigned received=0;while(received<1&&std::chrono::steady_clock::now()<end){if(capture.TryTakeNewest(out))++received;else std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    if(!received)throw std::runtime_error("delayed completion failed");
    capture.PollRetirement();
    // Deterministically pause an issuer holding old metadata until retirement
    // closes that generation; resuming it must not acquire/submit GPU ownership.
    metadata.captureGeneration=capture.AdmissionToken();
    std::atomic<bool> resume{false};bool lateAccepted=false;
    std::thread paused([&]{while(!resume.load())std::this_thread::yield();lateAccepted=capture.TrySubmit(queue.Get(),source.Get(),metadata);});
    capture.InvalidateAdmission();const bool retired=capture.RetireSources(500);resume=true;paused.join();
    if(!retired||lateAccepted)throw std::runtime_error("late pre-resize admission accepted");
    for(unsigned policy=0;policy<=2;++policy){
        ComPtr<ID3D12CommandAllocator> hdrAllocator;HR(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&hdrAllocator)));
        ComPtr<ID3D12GraphicsCommandList> hdrList;HR(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,hdrAllocator.Get(),nullptr,IID_PPV_ARGS(&hdrList)));
        auto hdrDesc=td;hdrDesc.Width=113;hdrDesc.Height=79;hdrDesc.Format=policy<=1?DXGI_FORMAT_R10G10B10A2_UNORM:DXGI_FORMAT_R16G16B16A16_FLOAT;
        ComPtr<ID3D12Resource> hdrSource;HR(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&hdrDesc,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&hdrSource)));
        device->CreateRenderTargetView(hdrSource.Get(),nullptr,handle);const float white[]{1,1,1,1};hdrList->ClearRenderTargetView(handle,policy==0?color:white,0,nullptr);
        b.Transition.pResource=hdrSource.Get();hdrList->ResourceBarrier(1,&b);HR(hdrList->Close());auto* hdrNative=static_cast<ID3D12CommandList*>(hdrList.Get());queue->ExecuteCommandLists(1,&hdrNative);
        metadata.key.sequence=10+policy;metadata.colorPolicy=policy;metadata.captureGeneration=capture.AdmissionToken();
        const bool initial=capture.TrySubmit(queue.Get(),hdrSource.Get(),metadata);
        if(policy==0&&initial)throw std::runtime_error("HDR resize allocated on Present");
        if(!initial){capture.PrepareRequested();if(!capture.TrySubmit(queue.Get(),hdrSource.Get(),metadata))throw std::runtime_error("HDR capture rejected");}
        CpuFrame hdrFrame;while(!capture.TryTakeNewest(hdrFrame)&&std::chrono::steady_clock::now()<end)std::this_thread::sleep_for(std::chrono::milliseconds(1));
        const int expected=policy==1?253:143;
        if(hdrFrame.width!=113||hdrFrame.height!=79||hdrFrame.pixels.empty())throw std::runtime_error("HDR footprint mismatch");
        const int sdrChannels[]{64,128,191};
        for(std::size_t i=0;i<hdrFrame.pixels.size();i+=4)for(unsigned c=0;c<3;++c)if(std::abs(static_cast<int>(hdrFrame.pixels[i+c])-(policy==0?sdrChannels[c]:expected))>2)throw std::runtime_error("10-bit SDR/HDR private conversion mismatch");
    }
    ComPtr<ID3D12Fence> done;HR(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&done)));HR(queue->Signal(done.Get(),1));
    while(done->GetCompletedValue()<1&&std::chrono::steady_clock::now()<end)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if(done->GetCompletedValue()<1)throw std::runtime_error("host retirement timeout");
    capture.PollRetirement();
    ComPtr<ID3D12InfoQueue> info;if(SUCCEEDED(device.As(&info)))for(UINT64 i=0;i<info->GetNumStoredMessages();++i){SIZE_T size=0;info->GetMessage(i,nullptr,&size);std::vector<unsigned char> bytes(size);auto* message=reinterpret_cast<D3D12_MESSAGE*>(bytes.data());HR(info->GetMessage(i,message,&size));if(message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){std::cerr<<message->pDescription<<'\n';throw std::runtime_error("D3D12 debug error");}}
    std::cout<<"PASS WARP: SDR pixels, PQ/scRGB private transforms, row pitch, exact key, three-slot pressure, delayed completion, resize retirement, FG invalidation without control-preparation wait and stale-token refusal; debug="<<debugAvailable<<"\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
