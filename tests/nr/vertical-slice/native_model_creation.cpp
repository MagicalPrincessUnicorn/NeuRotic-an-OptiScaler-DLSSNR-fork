#define NR_GPU_SAFETY_TEST
#include "../../../OptiScaler/dlssnr/NrGpuSafety.cpp"
#if __has_include(<dlssnr/NativeModelCreation.h>)
#include <dlssnr/NativeModelCreation.h>
#define NR_SPECTRE_SOURCE_TESTING
#include <dlssnr/NativeRendererRetirement.h>
#define HAS_NATIVE_CREATION_OWNER 1
#endif
#include <dxgi1_4.h>
#include <d3d12sdklayers.h>
#include <cstdio>
#include <stdexcept>
using Microsoft::WRL::ComPtr;
namespace G=DlssNr::GpuSafety;
namespace DlssNr {
class NativeRendererRetirementTestAccess {
public:
    static auto Receipt(std::shared_ptr<NativeProviderRetirementOwner> owner,bool complete){
        auto r=std::shared_ptr<NativeRendererRetirement>(new NativeRendererRetirement);
        r->Add(owner);r->observation_.attempted=1;r->observation_.succeeded=complete?1:0;return r;
    }
};
}
static void Api(HRESULT value){if(FAILED(value))throw std::runtime_error("controlled D3D12 prerequisite failed");}
int main()
{
#ifndef HAS_NATIVE_CREATION_OWNER
    std::puts("FAIL missing real creation recording boundary");return 1;
#else
 try{
    unsigned checks=0,failures=0;
    const auto check=[&](bool value,const char* name){++checks;if(!value){++failures;std::printf("FAIL %s\n",name);}};
    ComPtr<ID3D12Debug> debug;Api(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));debug->EnableDebugLayer();
    ComPtr<IDXGIFactory4> factory;ComPtr<IDXGIAdapter> warp;ComPtr<ID3D12Device> device;
    Api(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));Api(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)));
    Api(D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
    ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC q{};q.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
    Api(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)));
    ComPtr<ID3D12Resource> resource;D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
    d.Width=256;d.Height=1;d.DepthOrArraySize=1;d.MipLevels=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    Api(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&resource)));
    struct Recording {ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;G::Ticket ticket;};
    const auto fresh=[&]{Recording r;Api(device->CreateCommandAllocator(q.Type,IID_PPV_ARGS(&r.allocator)));
        Api(device->CreateCommandList(0,q.Type,r.allocator.Get(),nullptr,IID_PPV_ARGS(&r.list)));
        if(!G::Record(r.list.Get()))throw std::runtime_error("recording unavailable");
        Api(r.list->Close());Api(r.list->Reset(r.allocator.Get(),nullptr));r.ticket=G::Record(r.list.Get());return r;};
    auto first=fresh(),next=fresh();auto action=G::BeginLocalAction(first.ticket,first.list.Get(),resource.Get());
    auto later=G::BeginLocalAction(next.ticket,next.list.Get(),resource.Get());
    if(!action||!later)throw std::runtime_error("action unavailable");
    DlssNr::NativeModelCreation creation;unsigned attempts=0;int feature=0,foreign=0;
    check(!creation.ReadyFor(*later,&feature),"empty creation cannot qualify feature");
    check(creation.TryCreate(*action,[&]()->void*{++attempts;return &feature;}),"one actual create return retained");
    check(!creation.TryCreate(*action,[&]()->void*{++attempts;return &feature;})&&attempts==1,"no duplicate creation");
    check(!creation.ReadyFor(*action,&feature),"same recording never evaluates creation");
    check(!creation.ReadyFor(*later,&feature),"different recording without submission insufficient");
    action.reset();Api(first.list->Close());ID3D12CommandList* lists[]={first.list.Get()};queue->ExecuteCommandLists(1,lists);
    // A separate marker waits for the real queue, not for the synthetic model.
    ComPtr<ID3D12Fence> marker;Api(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&marker)));
    Api(queue->Signal(marker.Get(),1));HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    if(!event)throw std::runtime_error("event unavailable");Api(marker->SetEventOnCompletion(1,event));
    const auto wait=WaitForSingleObject(event,5000);CloseHandle(event);if(wait!=WAIT_OBJECT_0)throw std::runtime_error("queue timeout");
    check(!creation.ReadyFor(*later,&feature),"completed replayable creation still refused");
    Api(first.list->Reset(first.allocator.Get(),nullptr));
    check(creation.ReadyFor(*later,&feature),"completed nonreplayable creation can prepare on new recording");
    check(!creation.ReadyFor(*later,&foreign),"foreign feature refused");
    Api(next.list->Close());check(!creation.ReadyFor(*later,&feature),"closed current action refused");later.reset();
    Api(next.list->Reset(next.allocator.Get(),nullptr));next.ticket=G::Record(next.list.Get());
    later=G::BeginLocalAction(next.ticket,next.list.Get(),resource.Get());
    if(!later)throw std::runtime_error("replacement action unavailable");
    check(creation.ReadyFor(*later,&feature),"revoke control is ready before owner retirement");
    const auto revoke=[]<class Owner>(Owner& owner){if constexpr(requires{owner.Revoke();})owner.Revoke();};
    revoke(creation);
    check(!creation.ReadyFor(*later,&feature),"retired feature address cannot revive creation");
    check(!creation.TryCreate(*later,[&]()->void*{++attempts;return &feature;})&&attempts==1,"revoked owner cannot recreate");
    auto prior=std::make_shared<DlssNr::NativeProviderRetirementOwner>(
        DlssNr::ProviderLifetimeContract::ControlledCallerList(),Neurotic::Contracts::ProviderIncarnation{});
    auto foreignOwner=std::make_shared<DlssNr::NativeProviderRetirementOwner>(
        DlssNr::ProviderLifetimeContract::ControlledCallerList(),Neurotic::Contracts::ProviderIncarnation{});
    const auto nextCreation=[]<class Receipt>(const Receipt& receipt,const auto& owner){
        if constexpr(requires{receipt->NextModelCreation(owner);})return receipt->NextModelCreation(owner);
        else return std::unique_ptr<DlssNr::NativeModelCreation>{};
    };
    auto incomplete=DlssNr::NativeRendererRetirementTestAccess::Receipt(prior,false);
    check(!nextCreation(incomplete,prior),"incomplete retirement cannot start new model owner");
    auto retired=DlssNr::NativeRendererRetirementTestAccess::Receipt(prior,true);
    check(!nextCreation(retired,foreignOwner),"foreign retirement cannot start new model owner");
    check(!nextCreation(retired,std::shared_ptr<DlssNr::NativeProviderRetirementOwner>{}),"nonempty retirement cannot omit owner");
    auto replacement=nextCreation(retired,prior);
    check(bool(replacement)&&!replacement->Attempted(),"completed retirement starts a fresh creation owner");
    check(replacement&&replacement->TryCreate(*later,[&]()->void*{++attempts;return &foreign;}),"fresh epoch permits its own actual creation boundary");
    check(!creation.TryCreate(*later,[&]()->void*{++attempts;return &feature;})&&attempts==2,"old revoked epoch never reopens");
    check(!nextCreation(retired,prior),"retirement authorization consumed once");
    check(replacement&&!replacement->ReadyFor(*later,&foreign),"fresh creation still needs its own completed nonreplayable recording");
    auto emptyIncomplete=DlssNr::NativeRendererRetirementTestAccess::Receipt({},false);
    check(!nextCreation(emptyIncomplete,std::shared_ptr<DlssNr::NativeProviderRetirementOwner>{}),"failed empty shutdown cannot issue a new epoch");
    auto emptyRetired=DlssNr::NativeRendererRetirementTestAccess::Receipt({},true);
    check(!nextCreation(emptyRetired,prior),"empty retirement cannot cover a live owner");
    check(bool(nextCreation(emptyRetired,std::shared_ptr<DlssNr::NativeProviderRetirementOwner>{})),"successful empty shutdown permits first model in next epoch");
    check(!nextCreation(emptyRetired,std::shared_ptr<DlssNr::NativeProviderRetirementOwner>{}),"empty retirement cannot be replayed");
    later.reset();Api(next.list->Close());
    Api(first.list->Close());
    auto cancelled=fresh();auto cancelAction=G::BeginLocalAction(cancelled.ticket,cancelled.list.Get(),resource.Get());
    if(!cancelAction)throw std::runtime_error("cancel action unavailable");
    DlssNr::NativeModelCreation cancelledCreation;
    check(cancelledCreation.TryCreate(*cancelAction,[&]()->void*{return &feature;}),"cancel test actual owner control");
    cancelAction.reset();Api(cancelled.list->Close());Api(cancelled.list->Reset(cancelled.allocator.Get(),nullptr));
    auto cancelledNew=G::Record(cancelled.list.Get());auto afterCancel=G::BeginLocalAction(cancelledNew,cancelled.list.Get(),resource.Get());
    if(!afterCancel)throw std::runtime_error("after cancel unavailable");
    check(G::Reusable(cancelled.ticket)&&!cancelledCreation.ReadyFor(*afterCancel,&feature),"cancelled creation is not initialized model");
    DlssNr::NativeModelCreation failed;unsigned failedCalls=0;
    check(!failed.TryCreate(*afterCancel,[&]()->void*{++failedCalls;return nullptr;}),"failed create retained");
    check(!failed.TryCreate(*afterCancel,[&]()->void*{++failedCalls;return &feature;})&&failedCalls==1,"failed create never retried");
    DlssNr::NativeModelCreation unknown;unsigned unknownCalls=0;
    try{unknown.TryCreate(*afterCancel,[&]()->void*{++unknownCalls;throw std::runtime_error("opaque failure");});}
    catch(const std::runtime_error&){}
    check(!unknown.TryCreate(*afterCancel,[&]()->void*{++unknownCalls;return &feature;})&&unknownCalls==1,"unknown create never retried");
    afterCancel.reset();Api(cancelled.list->Close());
    std::printf("Native model creation: %u checks, %u failures; controlled WARP and synthetic create; proprietary model NOT_RUN\n",checks,failures);
    return failures?1:0;
 }catch(const std::exception& error){std::printf("INFRA_ERROR %s\n",error.what());return 2;}
#endif
}
