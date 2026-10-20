#define NR_GPU_SAFETY_TEST
#include "../../../OptiScaler/dlssnr/NrGpuSafety.cpp"
#include "../context/ContextFixtureStore.h"
#if __has_include(<nr/lifecycle/ProviderLeaseBinding.h>)
#include <nr/lifecycle/ProviderLeaseBinding.h>
#define HAS_PROVIDER_LEASE 1
#endif
#include <dxgi1_4.h>
#include <d3d12sdklayers.h>
using namespace NrTest002;
using Microsoft::WRL::ComPtr;
namespace L=Neurotic::Lifecycle;
static void Api(HRESULT hr){if(FAILED(hr))throw std::runtime_error("D3D12 prerequisite or API failure");}
int main(int argc,char** argv)
{
    return Guard([&]{Report r("provider-consumption-leases",argc==2?std::string_view(argv[1]):std::string_view{});
#ifdef HAS_PROVIDER_LEASE
        ComPtr<ID3D12Debug> debug;Api(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));debug->EnableDebugLayer();
        ComPtr<IDXGIFactory4> factory;ComPtr<IDXGIAdapter> warp;ComPtr<ID3D12Device> device;
        Api(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));Api(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)));
        Api(D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
        D3D12_COMMAND_QUEUE_DESC q{};q.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
        ComPtr<ID3D12CommandQueue> producer,consumer;Api(device->CreateCommandQueue(&q,IID_PPV_ARGS(&producer)));
        Api(device->CreateCommandQueue(&q,IID_PPV_ARGS(&consumer)));
        ComPtr<ID3D12Fence> gate,ready,done;Api(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate)));
        Api(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&ready)));Api(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&done)));
        ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;
        Api(device->CreateCommandAllocator(q.Type,IID_PPV_ARGS(&allocator)));
        Api(device->CreateCommandList(0,q.Type,allocator.Get(),nullptr,IID_PPV_ARGS(&list)));
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC d{};
        d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=256;d.Height=1;d.DepthOrArraySize=1;d.MipLevels=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ComPtr<ID3D12Resource> resource;Api(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&resource)));
        L::ProviderLeaseBinding expected;expected.resource=NrContextTests::Resource(10,{}).identity;
        expected.provider=Id<C::ProviderIncarnation>("provider",1);expected.handoff=Gen(C::IdentityKind::HandoffContractGeneration,"handoff",1);
        expected.consumer=Key("provider-consumer");expected.ledgerKey={7,8,9};expected.token=41;expected.sequence=42;
        DlssNr::PreFg::CompletionLedger ledger;
        auto reservation=ledger.Reserve(resource.Get(),7,8,9,41,42);if(!reservation)throw std::runtime_error("reservation fixture failed");
        r.Check("T18.provider-uncommitted",L::ClaimProviderDependency(ledger,resource.Get(),expected,expected).has_value(),false,"ClaimProviderDependency");
        Api(producer->Wait(gate.Get(),1));Api(producer->Signal(ready.Get(),1));
        if(!ledger.Commit(reservation,resource.Get(),ready.Get(),1))throw std::runtime_error("commit fixture failed");
        // Independent owner-ledger fixtures keep one deliberately accepted negative
        // case from consuming the positive fixture and hiding its named oracle.
        const auto probe=[&](const L::ProviderLeaseBinding& current){auto trial=ledger;
            return L::ClaimProviderDependency(trial,resource.Get(),expected,current).has_value();};
        auto current=expected;current.provider=Id<C::ProviderIncarnation>("provider",2);
        r.Check("T19.provider-incarnation",probe(current),false,"ClaimProviderDependency");
        current=expected;current.resource.contentRevision=Known(C::ContentRevision{2});
        r.Check("T19.provider-content",probe(current),false,"ClaimProviderDependency");
        current=expected;current.ledgerKey.instance=10;
        r.Check("T19.provider-instance",probe(current),false,"ClaimProviderDependency");
        current=expected;current.handoff=Gen(C::IdentityKind::HandoffContractGeneration,"handoff",2);
        r.Check("T19.provider-handoff",probe(current),false,"ClaimProviderDependency");
        auto packet=L::ClaimProviderDependency(ledger,resource.Get(),expected,expected);
        if(!packet)throw std::runtime_error("matching provider packet refused");
        r.Check("T19.provider-once",L::ClaimProviderDependency(ledger,resource.Get(),expected,expected).has_value(),false,"ClaimProviderDependency");
        current=expected;current.token=43;
        r.Check("T19.provider-bind-token",packet->Bind(current,list.Get()),false,"ProviderLeaseDependency::Bind");
        r.Check("T18.provider-bind-committed",packet->Bind(expected,list.Get()),true,"ProviderLeaseDependency::Bind");
        Api(list->Close());ID3D12CommandList* lists[]={list.Get()};consumer->ExecuteCommandLists(1,lists);Api(consumer->Signal(done.Get(),1));
        r.Check("T18.provider-wait-applied",packet->Status()->applied.load(),true,"GpuSafety existing external wait");
        r.Check("T18.provider-delayed",done->GetCompletedValue()==0,true,"existing queue dependency");
        r.Check("T19.provider-not-released",packet->ProviderReleased()==L::Fact::Unknown,true,"ProviderLeaseDependency::ProviderReleased");
        Api(gate->Signal(1));HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!event)throw std::runtime_error("event failed");
        Api(done->SetEventOnCompletion(1,event));if(WaitForSingleObject(event,5000)!=WAIT_OBJECT_0)throw std::runtime_error("provider completion timeout");CloseHandle(event);
        r.Check("T19.completed-not-released",packet->ProviderReleased()==L::Fact::Unknown,true,"ProviderLeaseDependency::ProviderReleased");
        Api(list->Reset(allocator.Get(),nullptr));
#else
        r.Check("T19.provider-owner-missing",false,true,"ProviderLeaseBinding unavailable");
#endif
        return r.Finish();});
}
