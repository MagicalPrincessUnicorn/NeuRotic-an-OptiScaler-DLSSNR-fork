#define NR_GPU_SAFETY_TEST
#include "../../../OptiScaler/dlssnr/NrGpuSafety.cpp"
#include <dxgi1_4.h>
#include <d3d12sdklayers.h>
#include <iostream>
#include <iomanip>
#include <vector>
#if __has_include(<nr/lifecycle/NativeOwnedContinuation.h>)
#include <nr/lifecycle/NativeOwnedContinuation.h>
#include <nr/lifecycle/SelectedFsr3ResourceOwner.h>
#include <nr/lifecycle/Fsr3SourceBoundConsumerAdapter.h>
#define NR_OWNED_CONTINUATION_AVAILABLE
#endif
using Microsoft::WRL::ComPtr;
static void Api(HRESULT h){if(FAILED(h))throw std::runtime_error("controlled WARP prerequisite failed");}
#ifdef NR_OWNED_CONTINUATION_AVAILABLE
namespace L=Neurotic::Lifecycle;namespace C=Neurotic::Contracts;namespace G=DlssNr::GpuSafety;
template<class Host>constexpr bool AcceptsSharedOwner=requires(ID3D12Device* device,std::shared_ptr<L::NativeOwnerSet> owners)
{Host::Create(device,std::move(owners),13,7,2);};
namespace Neurotic::Lifecycle
{
class NativeOwnedContinuationTestAccess
{
  public:
    static ID3D12CommandQueue* Queue(NativeOwnedContinuation& host){return host.state_->queue.Get();}
    static void Replay(NativeOwnedContinuation& host)
    {ID3D12CommandList* lists[]={host.state_->list.Get()};host.state_->queue->ExecuteCommandLists(1,lists);}
    static bool SubmitReplayable(NativeOwnedContinuation& host)
    {
        auto& s=*host.state_;if(FAILED(s.list->Close()))return false;s.closed=s.submitted=true;
        ID3D12CommandList* lists[]={s.list.Get()};s.queue->ExecuteCommandLists(1,lists);
        return SUCCEEDED(s.queue->Signal(s.fence.Get(),1));
    }
    static bool ResetAfterDrain(NativeOwnedContinuation& host)
    {return SUCCEEDED(host.state_->allocator->Reset())&&SUCCEEDED(host.state_->list->Reset(host.state_->allocator.Get(),nullptr));}
    static void Revoke(NativeOwnedContinuation& host){host.state_->owners->Resources().CloseAdmissions();}
    static std::weak_ptr<NativeOwnerSet> Owners(NativeOwnedContinuation& host){return host.state_->owners;}
    static G::Ticket Recording(NativeOwnedContinuation& host){return host.state_->ticket;}
    static ComPtr<ID3D12GraphicsCommandList> List(NativeOwnedContinuation& host){return host.state_->list;}
    static ComPtr<ID3D12CommandAllocator> Allocator(NativeOwnedContinuation& host){return host.state_->allocator;}
    static ComPtr<ID3D12Fence> Fence(NativeOwnedContinuation& host){return host.state_->fence;}
    static ComPtr<ID3D12Resource> Texture(NativeOwnedContinuation& host,std::size_t index){return host.state_->textures[index];}
    template<class Host>static ID3D12CommandQueue* SpareQueue(Host& host)
    {if constexpr(requires{host.state_->spare.queue;})return host.state_->spare.queue.Get();else return nullptr;}
    template<class Host>static ComPtr<ID3D12CommandQueue> ReplaceSpareQueue(Host& host,ID3D12CommandQueue* queue)
    {auto previous=host.state_->spare.queue;host.state_->spare.queue=queue;return previous;}
};
}
// Fault injection only. Successful Wait calls still execute on the real owned
// queue; the opaque producer's actual private point is observed solely by tests.
class WaitProbe final:public ID3D12CommandQueue
{
    std::atomic<ULONG> refs{1};
    ComPtr<ID3D12CommandQueue> real;
    ComPtr<ID3D12Fence> expectedFence;
    UINT64 expectedValue;
  public:
    bool failWait=false,unknownDevice=false,unsupportedType=false;
    ComPtr<ID3D12Device> otherDevice;
    unsigned waits=0,exactWaits=0;
    WaitProbe(ID3D12CommandQueue* queue,const G::Ticket& producer):real(queue),
        expectedFence(producer->points.at(0).timeline->fence),expectedValue(producer->points.at(0).value){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out)override
    {if(!out)return E_POINTER;*out=nullptr;if(id!=__uuidof(IUnknown)&&id!=__uuidof(ID3D12Object)&&
        id!=__uuidof(ID3D12DeviceChild)&&id!=__uuidof(ID3D12CommandQueue))return E_NOINTERFACE;
        *out=static_cast<ID3D12CommandQueue*>(this);AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{const auto count=--refs;if(!count)delete this;return count;}
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID id,UINT* size,void* data)override{return real->GetPrivateData(id,size,data);}
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID id,UINT size,const void* data)override{return real->SetPrivateData(id,size,data);}
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID id,const IUnknown* data)override{return real->SetPrivateDataInterface(id,data);}
    HRESULT STDMETHODCALLTYPE SetName(LPCWSTR name)override{return real->SetName(name);}
    HRESULT STDMETHODCALLTYPE GetDevice(REFIID id,void** device)override
    {if(unknownDevice){if(device)*device=nullptr;return E_FAIL;}return otherDevice?otherDevice->QueryInterface(id,device):real->GetDevice(id,device);}
    void STDMETHODCALLTYPE UpdateTileMappings(ID3D12Resource* resource,UINT count,const D3D12_TILED_RESOURCE_COORDINATE* starts,
        const D3D12_TILE_REGION_SIZE* sizes,ID3D12Heap* heap,UINT ranges,const D3D12_TILE_RANGE_FLAGS* flags,
        const UINT* offsets,const UINT* counts,D3D12_TILE_MAPPING_FLAGS mapping)override
    {real->UpdateTileMappings(resource,count,starts,sizes,heap,ranges,flags,offsets,counts,mapping);}
    void STDMETHODCALLTYPE CopyTileMappings(ID3D12Resource* dest,const D3D12_TILED_RESOURCE_COORDINATE* destStart,
        ID3D12Resource* source,const D3D12_TILED_RESOURCE_COORDINATE* sourceStart,const D3D12_TILE_REGION_SIZE* size,
        D3D12_TILE_MAPPING_FLAGS flags)override{real->CopyTileMappings(dest,destStart,source,sourceStart,size,flags);}
    void STDMETHODCALLTYPE ExecuteCommandLists(UINT count,ID3D12CommandList* const* lists)override{real->ExecuteCommandLists(count,lists);}
    void STDMETHODCALLTYPE SetMarker(UINT metadata,const void* data,UINT size)override{real->SetMarker(metadata,data,size);}
    void STDMETHODCALLTYPE BeginEvent(UINT metadata,const void* data,UINT size)override{real->BeginEvent(metadata,data,size);}
    void STDMETHODCALLTYPE EndEvent()override{real->EndEvent();}
    HRESULT STDMETHODCALLTYPE Signal(ID3D12Fence* fence,UINT64 value)override{return real->Signal(fence,value);}
    HRESULT STDMETHODCALLTYPE Wait(ID3D12Fence* fence,UINT64 value)override
    {
        ++waits;if(fence!=expectedFence.Get()||value!=expectedValue)return E_INVALIDARG;
        ++exactWaits;return failWait?E_FAIL:real->Wait(fence,value);
    }
    HRESULT STDMETHODCALLTYPE GetTimestampFrequency(UINT64* value)override{return real->GetTimestampFrequency(value);}
    HRESULT STDMETHODCALLTYPE GetClockCalibration(UINT64* gpu,UINT64* cpu)override{return real->GetClockCalibration(gpu,cpu);}
    D3D12_COMMAND_QUEUE_DESC STDMETHODCALLTYPE GetDesc()override
    {auto description=real->GetDesc();if(unsupportedType)description.Type=D3D12_COMMAND_LIST_TYPE_COPY;return description;}
};
static C::Symbol Symbol(const std::string& text){C::Symbol s;if(!s.Assign(text))throw std::runtime_error("test symbol");return s;}
template<class Host,class Make,class Check>void BoundedRetention(Make& make,Check& check,ID3D12Device* device,
    const std::vector<std::byte>& pattern)
{
    if constexpr(requires{Host::MaxHosts;Host::CollectCompleted();})
    {
        ComPtr<ID3D12Fence> gate;Api(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate)));
        std::array<std::weak_ptr<L::NativeOwnerSet>,Host::MaxHosts> held;
        for(std::size_t i=0;i<Host::MaxHosts;++i)
        {
            auto host=make();if(!host||host->Upload(pattern)!=Host::Result::Recorded||
                host->Copy(host->Request(0,1))!=Host::Result::Recorded)throw std::runtime_error("retained capacity fixture failed");
            Api(L::NativeOwnedContinuationTestAccess::Queue(*host)->Wait(gate.Get(),1));
            held[i]=L::NativeOwnedContinuationTestAccess::Owners(*host);
            if(!host->Submit())throw std::runtime_error("retained capacity submit failed");host.reset();
        }
        check(std::all_of(held.begin(),held.end(),[](const auto& owner){return !owner.expired();})&&Host::CollectCompleted()==0,
            "abandoned submitted hosts retain exact owners while real queue completion is pending");
        auto overflow=make();check(!overflow,"retained host capacity refuses new construction before GPU commands");
        Api(gate->Signal(1));if(!G::Drain(5000))throw std::runtime_error("retained capacity drain failed");
        check(Host::CollectCompleted()==Host::MaxHosts&&std::all_of(held.begin(),held.end(),[](const auto& owner){return owner.expired();}),
            "actual terminal completion reclaims every bounded abandoned host");
        auto next=make();check(next&&next->Upload(pattern)==Host::Result::Recorded&&next->CancelUnsubmitted()&&next->TryRetire(),
            "terminal collection restores bounded construction capacity");
    }
    else check(false,"abandoned submitted hosts have bounded retained capacity and terminal reclamation");
}
template<class Host,class Make,class Check>void RecordedPathAndPrivateRetirement(Make& make,Check& check,
    ID3D12Device* device,const std::vector<std::byte>& pattern)
{
    using R=typename Host::Result;using A=L::NativeOwnedContinuationTestAccess;
    if constexpr(requires(Host& host){host.ProveRecordedPath(std::size_t{0},C::ResourceIdentityToken{});})
    {
        auto host=make(4);if(!host||host->Upload(pattern)!=R::Recorded)throw std::runtime_error("recorded path upload failed");
        auto nonempty=std::make_unique<L::NativeOwnerSet>(Symbol("sp06.nonempty.refusal"));
        auto retainedResource=A::Texture(*host,0);const auto* observed=nonempty->Resources().Observe(retainedResource.Get());
        if(!observed)throw std::runtime_error("nonempty owner refusal fixture failed");const auto retainedVersion=observed->identity;
        const auto* originalOwner=nonempty.get();auto refused=Host::Create(device,std::move(nonempty),13,7,2);
        const auto* stillObserved=nonempty?nonempty->Resources().Observe(retainedResource.Get()):nullptr;
        check(!refused&&nonempty.get()==originalOwner&&stillObserved&&stillObserved->identity==retainedVersion&&
            bool(nonempty->Resources().PublishNativeLease(retainedResource.Get(),C::ScopeRef{nonempty->SourceJournal().Event().evidence.record})),
            "nonempty owner refusal preserves caller ownership current allocation and native use rights");
        const auto root=host->View(0)->identity;
        auto identity=host->ProveRecordedPath(0,root);
        check(identity&&identity->StepCount()==0&&identity->Source()==root&&host->ValidateRecordedPath(*identity),
            "recorded path identity uses actual upload content with no invented write");
        check(!host->ProveRecordedPath(2,host->View(2)->identity),"planned output allocation has no recorded content path");
        auto unknown=host->Request(0,2);unknown.operation=Host::Operation::Unknown;
        check(host->Copy(unknown)==R::Refused&&!host->ProveRecordedPath(2,host->View(2)->identity),
            "unsupported transform cannot create a recorded path from matching labels");
        if(host->Copy(host->Request(0,1))!=R::Recorded||host->Copy(host->Request(1,2))!=R::Recorded)
            throw std::runtime_error("recorded path copies failed");
        check(!host->FullCopyAncestry(0),"Resource ancestry refuses a recorded copy without actual unique submission");
        const auto old=host->View(2)->identity;auto proof=host->ProveRecordedPath(2,old);
        check(proof&&proof->StepCount()==2&&proof->Source()==root&&proof->Destination()==old&&host->ValidateRecordedPath(*proof),
            "opaque path traverses every actual copy and exact recorded content version");
        auto foreign=make();check(foreign&&proof&&!foreign->ValidateRecordedPath(*proof),
            "recorded path from another physical owner cannot be transplanted");
        if(!foreign->CancelUnsubmitted()||!foreign->TryRetire())throw std::runtime_error("path foreign cleanup failed");
        if(host->Copy(host->Request(0,2))!=R::Recorded)throw std::runtime_error("path overwrite failed");
        check(proof&&!host->ValidateRecordedPath(*proof)&&!host->ProveRecordedPath(2,old),
            "later actual output overwrite invalidates the exact old recorded path");
        auto current=host->ProveRecordedPath(2,host->View(2)->identity);
        check(current&&current->StepCount()==1&&host->ValidateRecordedPath(*current),
            "fresh actual overwrite has its own exact recorded path");
        ComPtr<ID3D12Fence> proofGate;Api(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&proofGate)));
        Api(A::Queue(*host)->Wait(proofGate.Get(),1));
        if(host->RecordReadback(2,host->View(2)->identity)!=R::Recorded||!host->Submit())
            throw std::runtime_error("recorded path readback submit failed");
        const auto ancestry=host->FullCopyAncestry(2);
        check(ancestry&&ancestry->Source().identity==root&&ancestry->Endpoint().identity==host->View(2)->identity,
            "Resource issues exact original and endpoint after actual full copy and unique submission");
        check(!host->FullCopyAncestry(1),"overwritten endpoint cannot issue stale Resource ancestry");
        check(current&&!host->ValidateRecordedPath(*current)&&!host->ProveRecordedPath(2,host->View(2)->identity),
            "submitted recorded path refuses unknown completion behind actual GPU gate");
        Api(proofGate->Signal(1));if(!host->Complete(5000))throw std::runtime_error("recorded path completion failed");
        std::vector<std::byte> actual(pattern.size());
        check(host->Readback(actual)&&actual==pattern&&current&&host->ValidateRecordedPath(*current),
            "recorded path positive has actual completed pixel equality");
        if(!host->TryRetire())throw std::runtime_error("recorded path retirement failed");
        check(current&&!host->ValidateRecordedPath(*current),"retired path metadata cannot authorize a current resource");
        auto cancelled=make();if(!cancelled||cancelled->Upload(pattern)!=R::Recorded||cancelled->Copy(cancelled->Request(0,1))!=R::Recorded)
            throw std::runtime_error("path cancellation fixture failed");
        auto abandoned=cancelled->ProveRecordedPath(1,cancelled->View(1)->identity);
        check(abandoned&&cancelled->CancelUnsubmitted()&&!cancelled->ValidateRecordedPath(*abandoned)&&cancelled->TryRetire(),
            "owner reset cancellation invalidates an unexecuted recorded path");
        auto replay=make();if(!replay||replay->Upload(pattern)!=R::Recorded||replay->Copy(replay->Request(0,1))!=R::Recorded)
            throw std::runtime_error("path replay fixture failed");
        auto replayProof=replay->ProveRecordedPath(1,replay->View(1)->identity);
        if(!A::SubmitReplayable(*replay)||!G::Drain(5000))throw std::runtime_error("path replayable submit failed");
        check(replayProof&&!replay->ValidateRecordedPath(*replayProof),"completed replayable recording cannot validate a recorded path");
        A::Replay(*replay);if(!G::Drain(5000)||!A::ResetAfterDrain(*replay))throw std::runtime_error("path replay reset failed");
        check(!replay->ValidateRecordedPath(*replayProof)&&replay->TryRetire(),
            "actual replay stays invalid for a recorded path after terminal reset");
        auto reset=make();if(!reset||reset->Upload(pattern)!=R::Recorded||reset->Copy(reset->Request(0,1))!=R::Recorded||
            !reset->Submit()||!reset->Complete(5000)||!A::ResetAfterDrain(*reset))throw std::runtime_error("path completed reset fixture failed");
        auto resetProof=reset->ProveRecordedPath(1,reset->View(1)->identity);
        check(resetProof&&reset->ValidateRecordedPath(*resetProof)&&reset->TryRetire(),
            "unique completed recording preserves actual content proof after authentic reset");
    }
    else check(false,"bounded opaque proof traverses actual recorded content paths");
    if constexpr(requires(Host& host){host.TryRetireResource(std::size_t{0},C::ResourceIdentityToken{});})
    {
        auto host=make();if(!host||host->Upload(pattern)!=R::Recorded||host->Copy(host->Request(0,1))!=R::Recorded)
            throw std::runtime_error("private retirement producer setup failed");
        const auto scratch=host->View(0)->identity,intermediate=host->View(1)->identity;
        const auto producer=A::Recording(*host);
        check(!host->TryRetireResource(0,scratch),"recorded unsubmitted resource cannot retire without authentic terminal use");
        check(host->Copy(host->Request(0,2))==R::Refused,"resource retirement request closes new C03 admission before pending drain");
        if(!host->Submit()||!host->Complete(5000)||!host->BeginCrossQueue(1,intermediate,producer))
            throw std::runtime_error("private retirement cross queue setup failed");
        if(host->Copy(host->Request(1,2))!=R::Recorded||host->RecordReadback(2,host->View(2)->identity)!=R::Recorded)
            throw std::runtime_error("private retirement consumer setup failed");
        const auto final=host->View(2)->identity;
        check(host->TryRetireResource(0,scratch)&&!host->View(0)&&host->View(2)->identity==final,
            "independently terminal scratch retires while downstream physical output remains retained");
        check(!host->TryRetireResource(1,intermediate)&&!host->TryRetireResource(2,final),
            "pending downstream reader and output holds each prevent premature resource release");
        ComPtr<ID3D12Fence> gate;Api(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate)));
        Api(A::Queue(*host)->Wait(gate.Get(),1));
        if(!host->Submit())throw std::runtime_error("private retirement consumer submit failed");
        check(!host->TryRetireResource(1,intermediate)&&!host->TryRetireResource(2,final),
            "unknown submitted completion cannot release a resource-specific hold graph");
        Api(gate->Signal(1));if(!host->Complete(5000))throw std::runtime_error("private retirement consumer completion failed");
        std::vector<std::byte> actual(pattern.size());
        check(host->Readback(actual)&&actual==pattern&&host->TryRetireResource(1,intermediate)&&host->TryRetireResource(2,final),
            "every exact resource use terminates before independent final storage retirement");
        check(!host->View(1)&&!host->View(2)&&host->Copy(host->Request(1,2))==R::Refused&&host->TryRetire(),
            "revoked allocation metadata cannot grant new reads or writes after private retirement");
    }
    else check(false,"bounded resource-specific retirement preserves every physical use");
}
template<class Host,class Make,class Check>void CrossQueueMechanics(Make& make,Check& check,ID3D12Device* device,
    const std::vector<std::byte>& pattern)
{
    using A=L::NativeOwnedContinuationTestAccess;
    if constexpr(requires(Host& host){host.BeginCrossQueue(std::size_t{1},C::ResourceIdentityToken{},G::Ticket{});})
    {
        auto host=make();if(!host||host->Upload(pattern)!=Host::Result::Recorded||host->Copy(host->Request(0,1))!=Host::Result::Recorded)
            throw std::runtime_error("cross queue producer fixture failed");
        const auto version=host->View(1)->identity;const auto producer=A::Recording(*host);
        check(!host->BeginCrossQueue(1,version,producer),"L07 unsubmitted producer cannot establish cross queue order");
        ComPtr<ID3D12Fence> gate;Api(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate)));
        Api(A::Queue(*host)->Wait(gate.Get(),1));
        if(!host->Submit())throw std::runtime_error("cross queue producer submit failed");
        ComPtr<WaitProbe> probe;probe.Attach(new WaitProbe(A::SpareQueue(*host),producer));
        A::ReplaceSpareQueue(*host,probe.Get());
        auto stale=version;stale.contentRevision={};
        check(!host->BeginCrossQueue(1,stale,producer)&&!host->BeginCrossQueue(1,version,{}),
            "L07 wrong exact content or producer ticket refuses before wait");
        auto foreign=make(1);if(!foreign||foreign->Upload(pattern)!=Host::Result::Recorded||
            foreign->Copy(foreign->Request(0,1))!=Host::Result::Recorded)throw std::runtime_error("cross queue foreign fixture failed");
        check(!host->BeginCrossQueue(1,version,A::Recording(*foreign))&&probe->waits==0,
            "L07 different authentic producer ticket refuses before queue wait");
        if(!foreign->Submit()||!foreign->Complete(5000))throw std::runtime_error("cross queue capacity fixture failed");
        check(!foreign->BeginCrossQueue(1,foreign->View(1)->identity,A::Recording(*foreign)),
            "L07 exhausted edge capacity refuses before consumer recording");
        if(!foreign->TryRetire())throw std::runtime_error("cross queue capacity cleanup failed");
        const bool began=host->BeginCrossQueue(1,version,producer);
        check(began,"L07 sealed unique producer establishes actual second queue wait before completion");
        if(!began){Api(gate->Signal(1));if(!G::Drain(5000))throw std::runtime_error("cross queue refused cleanup failed");return;}
        check(!host->BeginCrossQueue(1,version,producer),"L07 cross queue continuation cannot replay its handoff");
        check(host->Copy(host->Request(1,0))==Host::Result::Refused,
            "L07 in-flight producer destination cannot be overwritten as a fresh consumer allocation");
        if(host->Copy(host->Request(1,2))!=Host::Result::Recorded||
            host->RecordReadback(2,host->View(2)->identity)!=Host::Result::Recorded||!host->Submit())
        {Api(gate->Signal(1));throw std::runtime_error("cross queue consumer fixture failed");}
        check(host->EdgeAt(1)->recordingTicket!=producer&&host->EdgeAt(1)->producerDependency==producer&&host->EdgeAt(1)->source==version,
            "L07 second queue edge binds current recording and exact prior content");
        check(probe->waits>=2&&probe->waits==probe->exactWaits,
            "L07 every admitted producer read waits on exact owner signal fence value and consumer queue");
        const auto consumerFence=A::Fence(*host);HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        if(!event)throw std::runtime_error("cross queue event failed");Api(consumerFence->SetEventOnCompletion(1,event));
        const auto early=WaitForSingleObject(event,250);
        std::vector<std::byte> actual(pattern.size());
        const bool premature=host->Readback(actual)||host->TryRetire()||host->Complete(0);
        Api(gate->Signal(1));
        if(consumerFence->GetCompletedValue()<1&&WaitForSingleObject(event,5000)!=WAIT_OBJECT_0)
            throw std::runtime_error("cross queue event cleanup failed");CloseHandle(event);
        check(early==WAIT_TIMEOUT&&!premature,"L07 actual consumer queue waits for exact blocked producer fence value");
        const bool completed=host->Complete(5000),pixels=host->Readback(actual)&&actual==pattern,retired=host->TryRetire();
        check(completed&&pixels&&retired,"L07 actual cross queue completion readback and all-ticket retirement succeed");

        auto held=make();if(!held||held->Upload(pattern)!=Host::Result::Recorded||held->Copy(held->Request(0,1))!=Host::Result::Recorded||!held->Submit())
            throw std::runtime_error("cross queue retained producer setup failed");
        if(!held->Complete(5000))throw std::runtime_error("cross queue producer completion failed");
        ComPtr<ID3D12Fence> consumerGate;Api(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&consumerGate)));
        Api(A::SpareQueue(*held)->Wait(consumerGate.Get(),1));
        if(!held->BeginCrossQueue(1,held->View(1)->identity,A::Recording(*held))||held->Copy(held->Request(1,2))!=Host::Result::Recorded||!held->Submit())
        {Api(consumerGate->Signal(1));throw std::runtime_error("cross queue retained consumer setup failed");}
        auto weak=A::Owners(*held);held.reset();
        check(!weak.expired()&&Host::CollectCompleted()==0,"L07 completed producer root stays held for pending downstream consumer");
        Api(consumerGate->Signal(1));if(!G::Drain(5000))throw std::runtime_error("cross queue retained drain failed");
        check(Host::CollectCompleted()==1&&weak.expired(),"L07 every terminal ticket permits downstream root reclamation");

        auto unsealed=make();if(!unsealed||unsealed->Upload(pattern)!=Host::Result::Recorded||
            unsealed->Copy(unsealed->Request(0,1))!=Host::Result::Recorded||!A::SubmitReplayable(*unsealed))
            throw std::runtime_error("cross queue unsealed fixture failed");
        const auto ticket=A::Recording(*unsealed);
        check(!unsealed->BeginCrossQueue(1,unsealed->View(1)->identity,ticket),"L07 replayable producer cannot authorize other queue");
        if(!G::Drain(5000))throw std::runtime_error("cross queue first replay drain failed");A::Replay(*unsealed);
        if(!G::Drain(5000))throw std::runtime_error("cross queue second replay drain failed");
        check(!unsealed->BeginCrossQueue(1,unsealed->View(1)->identity,ticket),"L07 actually replayed producer cannot authorize other queue");
        if(!A::ResetAfterDrain(*unsealed)||!unsealed->TryRetire())throw std::runtime_error("cross queue replay cleanup failed");

        auto faulted=make();if(!faulted||faulted->Upload(pattern)!=Host::Result::Recorded||
            faulted->Copy(faulted->Request(0,1))!=Host::Result::Recorded||!faulted->Submit()||!faulted->Complete(5000))
            throw std::runtime_error("cross queue fault fixture failed");
        const auto faultTicket=A::Recording(*faulted);const auto faultVersion=faulted->View(1)->identity;
        auto original=A::ReplaceSpareQueue(*faulted,A::Queue(*faulted));
        check(!faulted->BeginCrossQueue(1,faultVersion,faultTicket),"L07 producer queue cannot impersonate distinct consumer queue");
        ComPtr<WaitProbe> faults;faults.Attach(new WaitProbe(original.Get(),faultTicket));A::ReplaceSpareQueue(*faulted,faults.Get());
        faults->unknownDevice=true;
        check(!faulted->BeginCrossQueue(1,faultVersion,faultTicket)&&faults->waits==0,"L07 unknown device refuses without queue wait");
        faults->unknownDevice=false;faults->unsupportedType=true;
        check(!faulted->BeginCrossQueue(1,faultVersion,faultTicket)&&faults->waits==0,"L07 unsupported consumer queue type refuses without queue wait");
        faults->unsupportedType=false;
#ifndef NR_LAB_WARP_ONLY
        ComPtr<IDXGIFactory4> devices;Api(CreateDXGIFactory1(IID_PPV_ARGS(&devices)));
        for(UINT index=0;!faults->otherDevice;++index)
        {
            ComPtr<IDXGIAdapter1> adapter;if(devices->EnumAdapters1(index,&adapter)==DXGI_ERROR_NOT_FOUND)break;
            ComPtr<ID3D12Device> candidate;
            if(adapter&&SUCCEEDED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&candidate)))&&candidate.Get()!=device)
                faults->otherDevice=std::move(candidate);
        }
#endif
        if(faults->otherDevice)
        {
            const auto id=faults->otherDevice->GetAdapterLuid();
            std::cout<<"L07 wrong_device_negative_luid="<<id.HighPart<<":"<<id.LowPart<<'\n';
            check(!faulted->BeginCrossQueue(1,faultVersion,faultTicket)&&faults->waits==0,
                "L07 different actual D3D12 device refuses without queue wait");
            faults->otherDevice.Reset();
        }
        else std::cout<<"NOT_RUN L07 actual distinct D3D12 device unavailable; unknown-device fault checked separately\n";
        faults->failWait=true;
        const bool acceptedFailure=faulted->BeginCrossQueue(1,faultVersion,faultTicket);
        check(!acceptedFailure&&faults->waits==1&&faults->exactWaits==1,
            "L07 actual E_FAIL from exact consumer Wait refuses admission");
        if(acceptedFailure)
        {faulted->CancelUnsubmitted();faulted->TryRetire();return;}
        faults->failWait=false;
        check(faulted->BeginCrossQueue(1,faultVersion,faultTicket),"L07 actual successful Wait permits retry after earlier refusal");
        A::Revoke(*faulted);
        check(faulted->Copy(faulted->Request(1,2))==Host::Result::Refused&&faulted->EdgeCount()==1&&!faulted->TryRetire(),
            "L07 fresh C03 revocation refuses consumer edge and preserves producer and consumer roots");
        if(!faulted->CancelUnsubmitted()||!faulted->TryRetire())throw std::runtime_error("cross queue fault cleanup failed");
        check(!faulted->BeginCrossQueue(1,faultVersion,faultTicket)&&faulted->Copy(faulted->Request(1,2))==Host::Result::Refused,
            "L07 stale handoff and edge after reset retirement cannot record again");

        auto cancelled=make();if(!cancelled||cancelled->Upload(pattern)!=Host::Result::Recorded||
            cancelled->Copy(cancelled->Request(0,1))!=Host::Result::Recorded)throw std::runtime_error("cross queue cancellation fixture failed");
        ComPtr<ID3D12Fence> lastGate;Api(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&lastGate)));
        Api(A::Queue(*cancelled)->Wait(lastGate.Get(),1));
        if(!cancelled->Submit()||!cancelled->BeginCrossQueue(1,cancelled->View(1)->identity,A::Recording(*cancelled)))
        {Api(lastGate->Signal(1));throw std::runtime_error("cross queue cancellation begin failed");}
        check(cancelled->CancelUnsubmitted()&&!cancelled->TryRetire(),
            "L07 actual unsubmitted consumer reset cannot release pending producer roots");
        auto pendingOwner=A::Owners(*cancelled);cancelled.reset();
        check(!pendingOwner.expired()&&Host::CollectCompleted()==0,"L07 abandoned cancelled consumer retains unknown producer completion");
        Api(lastGate->Signal(1));if(!G::Drain(5000))throw std::runtime_error("cross queue cancellation drain failed");
        check(Host::CollectCompleted()==1&&pendingOwner.expired(),"L07 genuine producer completion plus consumer reset reclaims all roots");
    }
    else check(false,"L07 real controlled cross queue continuation is implemented");
}
#endif
int main(int argc,char** argv)
try
{
    unsigned checks=0,failures=0;
    const auto check=[&](bool ok,const char* name){++checks;
        std::cout<<"ASSERT {\"name\":"<<std::quoted(name)<<",\"matched\":"<<(ok?"true":"false")<<"}\n";
        if(!ok){++failures;std::cerr<<"FAIL "<<name<<'\n';}};
#ifdef NR_OWNED_CONTINUATION_AVAILABLE
    using Host=L::NativeOwnedContinuation;using Result=Host::Result;
    check(!AcceptsSharedOwner<Host>,"controlled host construction requires exclusive canonical owner transfer");
    ComPtr<ID3D12Debug> debug;Api(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));debug->EnableDebugLayer();
#ifdef NR_LAB_WARP_ONLY
    const bool hardware=false;
    if(argc!=1)throw std::runtime_error("this diagnostic build permits WARP only");
#else
    const bool hardware=argc==2&&std::string_view(argv[1])=="--hardware";
    if(argc>2||(argc==2&&!hardware))throw std::runtime_error("unknown controlled adapter option");
#endif
    ComPtr<IDXGIFactory4> factory;ComPtr<IDXGIAdapter1> adapter;ComPtr<ID3D12Device> device;
    Api(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
#ifndef NR_LAB_WARP_ONLY
    if(hardware)
    {
        for(UINT index=0;;++index)
        {
            ComPtr<IDXGIAdapter1> candidate;
            if(factory->EnumAdapters1(index,&candidate)==DXGI_ERROR_NOT_FOUND)break;
            if(!candidate)continue;DXGI_ADAPTER_DESC1 description{};Api(candidate->GetDesc1(&description));
            char candidateName[512]{};WideCharToMultiByte(CP_UTF8,0,description.Description,-1,candidateName,sizeof(candidateName),nullptr,nullptr);
            std::cout<<"enumerated_adapter="<<candidateName<<" vendor="<<description.VendorId<<" device="<<description.DeviceId
                <<" flags="<<description.Flags<<" luid="<<description.AdapterLuid.HighPart<<":"<<description.AdapterLuid.LowPart<<'\n';
            // Some remote environments omit SOFTWARE on Basic Render Driver.
            // Its concrete known software identity never becomes hardware proof.
            if((description.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)||
               (description.VendorId==0x1414&&description.DeviceId==0x008c)||
               std::wstring_view(description.Description)==L"Microsoft Basic Render Driver")continue;
            if(SUCCEEDED(D3D12CreateDevice(candidate.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device))))
            {adapter=candidate;break;}
        }
        if(!adapter){std::cout<<"NOT_RUN hardware D3D12 adapter unavailable\n";return 3;}
    }
    else
#endif
    {Api(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));Api(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));}
    DXGI_ADAPTER_DESC1 description{};Api(adapter->GetDesc1(&description));
    char name[512]{};WideCharToMultiByte(CP_UTF8,0,description.Description,-1,name,sizeof(name),nullptr,nullptr);
    LARGE_INTEGER driver{};const auto driverResult=adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice),&driver);
    std::cout<<"environment="<<(hardware?"HARDWARE_CONTROLLED":"WARP_CONTROLLED")<<" adapter="<<name
        <<" vendor="<<description.VendorId<<" device="<<description.DeviceId<<" luid="<<description.AdapterLuid.HighPart
        <<":"<<description.AdapterLuid.LowPart<<" driver_query_hresult="<<driverResult;
    if(SUCCEEDED(driverResult))std::cout<<" driver="<<HIWORD(driver.HighPart)<<'.'<<LOWORD(driver.HighPart)<<'.'
        <<HIWORD(driver.LowPart)<<'.'<<LOWORD(driver.LowPart);
    std::cout<<'\n';
    ComPtr<ID3D12InfoQueue> messages;Api(device.As(&messages));
    unsigned serial=0;
    const auto make=[&](unsigned edges=2){const char* reason=nullptr;auto result=Host::Create(device.Get(),
        std::make_unique<L::NativeOwnerSet>(Symbol("sp06.controlled."+std::to_string(++serial))),13,7,edges,&reason);
        if(!result)std::cerr<<"Construction refusal: "<<(reason?reason:"unknown")<<'\n';return result;};
    std::vector<std::byte> pattern(13*7*4);
    for(size_t i=0;i<pattern.size();++i)pattern[i]=std::byte((i*37+11)%251);
    auto host=make();check(bool(host),"controlled host constructs canonical owners and actual WARP allocations");
    if(!host)throw std::runtime_error("controlled host construction failed");
    check(!host->View(0)->identity.contentRevision.IsKnown(),"allocation observation does not invent source pixels");
    check(host->Upload({})==Result::Refused&&!host->View(0)->identity.contentRevision.IsKnown(),
        "incorrect upload byte count refuses without write revision");
    auto request=host->Request(0,1);
    check(host->Copy(request)==Result::Refused&&host->EdgeCount()==0,"unknown source content refuses before copy");
    check(host->Upload(pattern)==Result::Recorded,"actual upload records source content revision");
    const auto source=host->View(0)->identity;request=host->Request(0,1);
    auto partial=request;partial.rectangle.width=12;
    check(host->Copy(partial)==Result::Refused&&host->EdgeCount()==0,"partial active rectangle refuses before copy");
    auto unknown=request;unknown.operation=Host::Operation::Unknown;
    check(host->Copy(unknown)==Result::Refused&&host->EdgeCount()==0,"unknown transform refuses before copy");
    auto alias=host->Request(0,0);
    check(host->Copy(alias)==Result::Refused&&host->EdgeCount()==0,"same allocation cannot claim copy edge");
    alias.operation=Host::Operation::Identity;
    check(host->Copy(alias)==Result::Identity&&host->EdgeCount()==0&&host->View(0)->identity==source,
        "genuine identity preserves existing content version without commands");
    auto distinctIdentity=request;distinctIdentity.operation=Host::Operation::Identity;
    const auto identityTargetBefore=host->View(1)->identity;
    check(host->Copy(distinctIdentity)==Result::Refused&&host->View(1)->identity==identityTargetBefore&&host->EdgeCount()==0,
        "Identity refuses a distinct current destination without content or edge changes");
    auto stale=request;stale.sourceVersion.contentRevision={};
    check(host->Copy(stale)==Result::Refused&&host->EdgeCount()==0,"unknown or stale source revision refuses");
    auto wrongTarget=request;wrongTarget.destinationVersion.resourceGeneration={};
    check(host->Copy(wrongTarget)==Result::Refused&&host->EdgeCount()==0,"unknown destination epoch refuses");
    auto otherOwner=make();auto foreign=request;foreign.sourceVersion=otherOwner->View(0)->identity;
    check(host->Copy(foreign)==Result::Refused&&host->EdgeCount()==0,"foreign canonical resource owner identity refuses");
    check(otherOwner->CancelUnsubmitted()&&otherOwner->TryRetire(),"unused controlled owner cancels and retires");
    check(host->Copy(request)==Result::Recorded&&host->EdgeCount()==1,"first real copy records exact owned edge");
    auto first=*host->EdgeAt(0);auto middle=host->View(1)->identity;
    check(first.source==source&&first.destination==middle&&first.rectangle==C::Rectangle{0,0,13,7}&&
        first.recording.Check()==C::Error::None&&first.ordinal==1,"edge records exact content versions region and recording");
    auto failed=host->Request(1,2);failed.rectangle.height=6;
    check(host->Copy(failed)==Result::Refused&&host->EdgeCount()==1&&host->EdgeAt(0)->destination==middle&&
        !host->TryRetire(),"later edge failure retains earlier edge and physical holds");
    check(host->Copy(host->Request(1,2))==Result::Recorded&&host->EdgeCount()==2,"second real owned copy extends chain");
    check(host->EdgeAt(1)->source==middle&&host->EdgeAt(1)->recording==first.recording,
        "second edge consumes exact first destination version on same recording");
    const auto before=host->View(0)->identity;
    check(host->Copy(host->Request(2,0))==Result::Refused&&host->View(0)->identity==before&&host->EdgeCount()==2,
        "capacity refuses before destination content changes");
    check(host->RecordReadback(2,host->View(2)->identity)==Result::Recorded,"readback command reserves exact final content");
    std::vector<std::byte> actual(pattern.size());
    check(!host->Readback(actual)&&!host->TryRetire(),"recorded commands are not completion or release");
    check(host->Submit(),"owned list closes executes and signals once");
    check(!host->Submit(),"duplicate submission refuses through public host API");
    check(host->Complete(5000)&&host->Readback(actual)&&actual==pattern,
        "real fence completion and footprint row pitches yield exact two-copy pixel equality");
    check(host->TryRetire()&&host->EdgeAt(0)->source==source,"terminal retirement preserves immutable edge descriptions");

    auto reset=make();check(reset&&reset->Upload(pattern)==Result::Recorded&&reset->Copy(reset->Request(0,1))==Result::Recorded,
        "reset negative has actual pending commands");
    check(!reset->TryRetire()&&reset->CancelUnsubmitted()&&reset->TryRetire()&&!reset->Readback(actual),
        "observed reset cancels unsubmitted recording without readback success");
    check(reset->Copy(reset->Request(0,1))==Result::Refused,"retired reset generation cannot record another edge");

    auto revoked=make();check(revoked&&revoked->Upload(pattern)==Result::Recorded&&revoked->Copy(revoked->Request(0,1))==Result::Recorded,
        "revocation control has an actual owned edge");
    const auto retained=revoked->EdgeAt(0)->destination;L::NativeOwnedContinuationTestAccess::Revoke(*revoked);
    check(revoked->Copy(revoked->Request(1,2))==Result::Refused&&revoked->EdgeCount()==1&&
        revoked->EdgeAt(0)->destination==retained&&!revoked->TryRetire(),"revoked canonical C03 resource owner refuses new use and retains old edge");
    check(revoked->CancelUnsubmitted()&&revoked->TryRetire(),"revocation alone is not cancellation but real reset permits retirement");

    auto revisions=make(4);check(revisions&&revisions->Upload(pattern)==Result::Recorded&&
        revisions->Copy(revisions->Request(0,1))==Result::Recorded,"revision control records real initial write");
    const auto staleEdge=revisions->Request(1,2);
    const auto viewSnapshot=revisions->View(1);
    check(revisions->Copy(revisions->Request(0,1))==Result::Recorded&&revisions->View(1)->identity!=staleEdge.sourceVersion,
        "second actual destination write advances canonical content revision");
    check(viewSnapshot->identity==staleEdge.sourceVersion,"published view snapshot stays immutable across later owned writes");
    check(revisions->Copy(staleEdge)==Result::Refused&&revisions->EdgeCount()==2,
        "old exact content version cannot authorize a later edge");
    check(revisions->Copy(revisions->Request(1,2))==Result::Recorded&&revisions->CancelUnsubmitted()&&revisions->TryRetire(),
        "fresh version extends owned chain before authentic reset cancellation");

    auto abandoned=make();
    if(!abandoned||abandoned->Upload(pattern)!=Result::Recorded||abandoned->Copy(abandoned->Request(0,1))!=Result::Recorded)
        throw std::runtime_error("unsubmitted destructor fixture failed");
    const auto weakOwners=L::NativeOwnedContinuationTestAccess::Owners(*abandoned);
    const auto abandonedTicket=L::NativeOwnedContinuationTestAccess::Recording(*abandoned);
    auto abandonedList=L::NativeOwnedContinuationTestAccess::List(*abandoned);
    auto abandonedAllocator=L::NativeOwnedContinuationTestAccess::Allocator(*abandoned);abandoned.reset();
    const auto cancelled=G::InspectTerminalRecording(abandonedTicket);
    check(weakOwners.expired()&&cancelled&&cancelled->submissions==0&&cancelled->state==G::RecordingTerminalState::Reset,
        "unsubmitted host destruction actually resets recording before releasing physical owners");
    // Keep the RED negative from leaving a live unsubmitted recording that
    // blocks unrelated global drain checks. These are genuine owned API calls.
    if(!cancelled){Api(abandonedList->Close());Api(abandonedList->Reset(abandonedAllocator.Get(),nullptr));Api(abandonedList->Close());}
    abandonedList.Reset();abandonedAllocator.Reset();

    auto pending=make();check(pending&&pending->Upload(pattern)==Result::Recorded&&
        pending->Copy(pending->Request(0,1))==Result::Recorded&&
        pending->RecordReadback(1,pending->View(1)->identity)==Result::Recorded,"pending control records real work");
    ComPtr<ID3D12Fence> gate;Api(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate)));
    Api(L::NativeOwnedContinuationTestAccess::Queue(*pending)->Wait(gate.Get(),1));
    check(pending->Submit(),"pending control submits to blocked real queue");
    const bool premature=pending->Readback(actual)||pending->TryRetire()||pending->Complete(0)||pending->CancelUnsubmitted();
    Api(gate->Signal(1));
    check(!premature,"unknown or incomplete completion cannot read release or cancel submitted work");
    const bool pendingCompleted=pending->Complete(5000);
    const bool pendingPixels=pending->Readback(actual)&&actual==pattern;
    const bool pendingRetired=pending->TryRetire();
    check(pendingCompleted&&pendingPixels&&pendingRetired,
        "actual gate release permits completion readback and physical retirement");

    auto replay=make();check(replay&&replay->Upload(pattern)==Result::Recorded&&replay->Copy(replay->Request(0,1))==Result::Recorded&&
        replay->RecordReadback(1,replay->View(1)->identity)==Result::Recorded&&L::NativeOwnedContinuationTestAccess::SubmitReplayable(*replay),
        "replay negative has actual submitted copied content without owner seal");
    check(G::Drain(5000)&&!replay->Readback(actual)&&!replay->TryRetire(),
        "completed replayable recording cannot grant readback or physical release");
    L::NativeOwnedContinuationTestAccess::Replay(*replay);
    check(G::Drain(5000)&&!replay->Readback(actual)&&!replay->TryRetire(),"replay invalidates unique completion and retains holds");
    check(L::NativeOwnedContinuationTestAccess::ResetAfterDrain(*replay)&&replay->TryRetire()&&!replay->Readback(actual),
        "actual reset after all replay completions permits release without qualified readback");
    BoundedRetention<Host>(make,check,device.Get(),pattern);
    // Cross-queue fixtures require the bounded pool reclamation checked above.
    if(!failures)CrossQueueMechanics<Host>(make,check,device.Get(),pattern);
    if(!failures)RecordedPathAndPrivateRetirement<Host>(make,check,device.Get(),pattern);
    unsigned severe=0;
    for(UINT64 i=0;i<messages->GetNumStoredMessages();++i)
    {SIZE_T size=0;Api(messages->GetMessage(i,nullptr,&size));std::vector<std::byte> memory(size);
        auto* message=reinterpret_cast<D3D12_MESSAGE*>(memory.data());Api(messages->GetMessage(i,message,&size));
        if(message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){++severe;std::cerr<<message->pDescription<<'\n';}}
    check(severe==0,"D3D12 debug layer reports no error or corruption for controlled commands");
#else
    check(false,"bounded actual owned copy continuation is implemented");
#endif
    std::cout<<"owned-continuation checks="<<checks<<" failed="<<failures<<"; controlled mechanics/readback only; no NR/provider/game/BeforeFg/seal\n";
    return failures?1:0;
}
catch(const std::exception& e){std::cerr<<"INFRA_ERROR "<<e.what()<<'\n';return 2;}
