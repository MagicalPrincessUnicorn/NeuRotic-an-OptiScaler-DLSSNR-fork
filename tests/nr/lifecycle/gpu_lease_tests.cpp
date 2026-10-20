#define NR_GPU_SAFETY_TEST
#include "../../../OptiScaler/dlssnr/NrGpuSafety.cpp"
#include "../context/ContextFixtureStore.h"
#if __has_include(<nr/lifecycle/GpuSafetyLeaseAdapter.h>)
#include "../../../OptiScaler/nr/lifecycle/ConsumptionLeaseAdapter.cpp"
#define HAS_NATIVE_LEASE 1
#endif
#include <dxgi1_4.h>
#include <d3d12sdklayers.h>
using namespace NrTest002;
using Microsoft::WRL::ComPtr;
namespace S=DlssNr::GpuSafety;
namespace L=Neurotic::Lifecycle;
static void Api(HRESULT hr){if(FAILED(hr))throw std::runtime_error("software D3D12 prerequisite or API failed");}
int main(int argc,char** argv)
{
    return Guard([&]{Report r("gpu-consumption-leases",argc==2?std::string_view(argv[1]):std::string_view{});
#ifdef HAS_NATIVE_LEASE
        ComPtr<ID3D12Debug> debug;Api(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));debug->EnableDebugLayer();
        ComPtr<IDXGIFactory4> factory;ComPtr<IDXGIAdapter> warp;ComPtr<ID3D12Device> device;
        Api(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));Api(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)));
        Api(D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
        ComPtr<ID3D12CommandQueue> queue,other;D3D12_COMMAND_QUEUE_DESC desc{};desc.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
        Api(device->CreateCommandQueue(&desc,IID_PPV_ARGS(&queue)));Api(device->CreateCommandQueue(&desc,IID_PPV_ARGS(&other)));
        ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;
        Api(device->CreateCommandAllocator(desc.Type,IID_PPV_ARGS(&allocator)));
        Api(device->CreateCommandList(0,desc.Type,allocator.Get(),nullptr,IID_PPV_ARGS(&list)));
        ComPtr<ID3D12Fence> gate,done;Api(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate)));
        Api(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&done)));
        auto ticket=S::Record(list.Get());if(!ticket)throw std::runtime_error("tracking unavailable");
        r.Check("T18.unsubmitted-order",L::ObserveGpuDependency(ticket,queue.Get()).ordered==L::Fact::Yes,false,"ObserveGpuDependency");
        r.Check("T17.null-ticket",L::ObserveTicketRetirement({})==L::Fact::Unknown,true,"ObserveTicketRetirement");
        Api(queue->Wait(gate.Get(),1));Api(list->Close());
        auto execute=[&]{ID3D12CommandList* lists[]={list.Get()};queue->ExecuteCommandLists(1,lists);};execute();
        auto state=L::ObserveGpuDependency(ticket,queue.Get());
        r.Check("T16.gpu-ordered-incomplete",state.ordered==L::Fact::Yes&&state.completed==L::Fact::No,true,"ObserveGpuDependency");
        r.Check("T16.gpu-order-not-reuse",state.reusable==L::Fact::Yes,false,"ObserveGpuDependency");
        r.Check("T18.cross-unsealed",L::ObserveGpuDependency(ticket,other.Get(),true).ordered==L::Fact::Yes,false,"ObserveGpuDependency");
        Api(gate->Signal(1));Api(queue->Signal(done.Get(),1));
        HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!event)throw std::runtime_error("event unavailable");
        Api(done->SetEventOnCompletion(1,event));if(WaitForSingleObject(event,5000)!=WAIT_OBJECT_0)throw std::runtime_error("GPU completion timeout");
        state=L::ObserveGpuDependency(ticket,queue.Get());
        r.Check("T17.first-completed-unsealed",state.completed==L::Fact::Yes&&state.reusable==L::Fact::No,true,"ObserveGpuDependency");
        execute();state=L::ObserveGpuDependency(ticket,queue.Get());
        r.Check("T17.gpu-replay-no-unique",state.uniqueSubmission==L::Fact::No&&state.ordered==L::Fact::No,true,"ObserveGpuDependency");
        Api(queue->Signal(done.Get(),2));Api(done->SetEventOnCompletion(2,event));
        if(WaitForSingleObject(event,5000)!=WAIT_OBJECT_0)throw std::runtime_error("replay timeout");
        Api(list->Reset(allocator.Get(),nullptr));
        r.Check("T17.reset-retirement",L::ObserveTicketRetirement(ticket)==L::Fact::Yes,true,"ObserveTicketRetirement");
        auto next=S::Record(list.Get());Api(list->Close());execute();
        if(!S::SealOwnedRecording(list.Get()))throw std::runtime_error("owned sealing failed");
        r.Check("T18.sealed-cross-order",L::ObserveGpuDependency(next,other.Get(),true).ordered==L::Fact::Yes,true,"ObserveGpuDependency");
        Api(other->Signal(done.Get(),3));Api(done->SetEventOnCompletion(3,event));
        if(WaitForSingleObject(event,5000)!=WAIT_OBJECT_0)throw std::runtime_error("cross queue timeout");
        S::ExternalWaitStatus wait;S::ExternalExecutionStatus external;
        wait.bound=true;wait.applied=true;external.evaluated=true;external.submitted=true;
        auto provider=L::ObserveExternalDependency(&wait,&external);
        r.Check("T19.submitted-not-release",provider.submitted==L::Fact::Yes&&provider.released==L::Fact::Unknown,true,"ObserveExternalDependency");
        wait.failed=true;provider=L::ObserveExternalDependency(&wait,&external);
        r.Check("T19.failed-wait",provider.healthy==L::Fact::No&&provider.applied==L::Fact::Unknown,true,"ObserveExternalDependency");
        r.Check("T19.missing-observer",L::ObserveExternalDependency(nullptr,&external).released==L::Fact::Unknown,true,"ObserveExternalDependency");
        CloseHandle(event);
#else
        r.Check("T16.native-adapter-missing",false,true,"GpuSafetyLeaseAdapter unavailable");
#endif
        return r.Finish();});
}
