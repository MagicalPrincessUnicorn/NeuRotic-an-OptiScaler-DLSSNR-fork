#define NR_GPU_SAFETY_TEST
#include "../OptiScaler/dlssnr/NrGpuSafety.cpp"
#include <dxgi1_6.h>
#include <array>
#include <iostream>
#include <cstdlib>
using namespace DlssNr::GpuSafety;
static unsigned checks=0;
#define CHECK(x) do {++checks;if(!(x)){std::cerr<<__LINE__<<": " #x "\n";std::exit(1);}}while(false)
int main(){
    ComPtr<IDXGIFactory4> factory;CHECK(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))));
    ComPtr<IDXGIAdapter> warp;CHECK(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp))));
    ComPtr<ID3D12Device> device;CHECK(SUCCEEDED(D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device))));
    D3D12_COMMAND_QUEUE_DESC q{};ComPtr<ID3D12CommandQueue> queue;CHECK(SUCCEEDED(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue))));
    for(bool unknownQueue:{false,true}) {
    ComPtr<ID3D12CommandQueue> otherQueue;CHECK(SUCCEEDED(device->CreateCommandQueue(&q,IID_PPV_ARGS(&otherQueue))));
    ComPtr<ID3D12CommandAllocator> allocator,readerAllocator;
    CHECK(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator))));
    CHECK(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&readerAllocator))));
    ComPtr<ID3D12GraphicsCommandList> list,reader;
    CHECK(SUCCEEDED(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list))));
    CHECK(SUCCEEDED(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,readerAllocator.Get(),nullptr,IID_PPV_ARGS(&reader))));
    CHECK(PrepareRecordingHooks(list.Get()));CHECK(PrepareRecordingHooks(reader.Get()));
    CHECK(SUCCEEDED(list->Close()));CHECK(SUCCEEDED(reader->Close()));
    CHECK(SUCCEEDED(list->Reset(allocator.Get(),nullptr)));CHECK(SUCCEEDED(reader->Reset(readerAllocator.Get(),nullptr)));
    D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=d.Height=8;d.DepthOrArraySize=d.MipLevels=1;
    d.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;d.SampleDesc.Count=1;d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    ComPtr<ID3D12Resource> texture;CHECK(SUCCEEDED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&texture))));
    const std::array resources{texture.Get()};auto producer=Record(list.Get());auto consumer=Record(reader.Get());CHECK(producer&&consumer);
    auto write=BeginLocalAction(producer,list.Get(),resources);CHECK(write);
    auto artifact=CreateDerivedArtifact(*write,resources,DerivedArtifactKind::Anchor);CHECK(artifact);
    auto read=BeginLocalAction(consumer,reader.Get(),resources);CHECK(read);
    CHECK(!ReserveDerivedRead(artifact,*read,unknownQueue?nullptr:queue.Get())); // unsubmitted
    write.reset();CHECK(SUCCEEDED(list->Close()));
    ComPtr<ID3D12Fence> gate;CHECK(SUCCEEDED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate))));
    CHECK(SUCCEEDED(queue->Wait(gate.Get(),1)));
    ID3D12CommandList* commands[]{list.Get()};queue->ExecuteCommandLists(1,commands);
    CHECK(OrderedOn(producer,queue.Get()));CHECK(!ReserveDerivedRead(artifact,*read,unknownQueue?nullptr:queue.Get())); // replayable
    // Actual host Reset removes future replay rights, without pretending that
    // the blocked GPU producer has completed or sealing a borrowed recording.
    CHECK(SUCCEEDED(list->Reset(allocator.Get(),nullptr)));CHECK(SUCCEEDED(list->Close()));
    auto lease=ReserveDerivedRead(artifact,*read,unknownQueue?nullptr:queue.Get());CHECK(lease);
    CHECK(lease->Readiness()==DerivedReadiness::DependencyOrderedImmutable);
    CHECK(!InspectRecording(producer,queue.Get()).completed);CHECK(!DerivedArtifactReusable(artifact));
    CHECK(DerivedAllocatedBytes()>0);lease.reset();read.reset();CHECK(SUCCEEDED(reader->Close()));
    commands[0]=reader.Get();auto* executingQueue=unknownQueue?otherQueue.Get():queue.Get();
    executingQueue->ExecuteCommandLists(1,commands);CHECK(SealOwnedRecording(reader.Get()));
    ComPtr<ID3D12Fence> readerDone;CHECK(SUCCEEDED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&readerDone))));
    CHECK(SUCCEEDED(executingQueue->Signal(readerDone.Get(),1)));
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);CHECK(event);
    CHECK(SUCCEEDED(readerDone->SetEventOnCompletion(1,event)));
    // On the other queue this would complete immediately without the bound
    // producer dependency. The real hook must hold it behind the gate.
    CHECK(WaitForSingleObject(event,50)==WAIT_TIMEOUT);
    CHECK(!DerivedArtifactReusable(artifact));CHECK(SUCCEEDED(gate->Signal(1)));
    CHECK(WaitForSingleObject(event,5000)==WAIT_OBJECT_0);CloseHandle(event);
    CHECK(Drain(5000));CHECK(DerivedArtifactReusable(artifact));
    artifact.reset();producer.reset();consumer.reset();CHECK(Pending().empty());CHECK(DerivedAllocatedBytes()==0);
    }
    constexpr uint64_t cap=640ull*1024*1024;
    auto a=ReserveDerivedAllocation(DerivedArtifactKind::Anchor,cap/2);
    auto b=ReserveDerivedAllocation(DerivedArtifactKind::Anchor,cap/2);CHECK(a&&b);CHECK(DerivedAllocatedBytes()==cap);
    CHECK(!ReserveDerivedAllocation(DerivedArtifactKind::Scratch,1));
    CHECK(!ReserveDerivedAllocation(DerivedArtifactKind::Anchor,1));
    auto retired=a;a.reset();CHECK(DerivedAllocatedBytes()==cap);retired.reset();CHECK(DerivedAllocatedBytes()==cap/2);
    b.reset();CHECK(DerivedAllocatedBytes()==0);
    std::cout<<"AFNR owner WARP PASS "<<checks<<" checks\n";
}
