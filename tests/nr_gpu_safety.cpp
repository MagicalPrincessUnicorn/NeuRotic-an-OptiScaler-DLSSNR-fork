// Runs the production submission/reset hooks on a software D3D12 device. No game or NR model.
#define NR_GPU_SAFETY_TEST
#include "../OptiScaler/dlssnr/NrGpuSafety.cpp"
#include "../OptiScaler/dlssnr/DlssNr_Capture.h"
#include "../OptiScaler/dlssnr/PreFg.h"
#include <dxgi1_4.h>
#include <d3d12sdklayers.h>
#include <cassert>
#include <dlssnr/NrSignalReceipts.h>
#include <cstdio>
#include "nr_streamline_proxy_fixture.h"
#include "nr_reshade_device_fixture.h"

using Microsoft::WRL::ComPtr;
namespace Safety = DlssNr::GpuSafety;
static void Check(HRESULT hr) { assert(SUCCEEDED(hr)); }

// Reproduce ReShade GetDevice at the actual list/queue boundary while keeping
// real WARP recording/submission/reset operations and production safety hooks.
static HRESULT (STDMETHODCALLTYPE* originalDeviceQuery)(ID3D12DeviceChild*, REFIID, void**) = nullptr;
static ID3D12DeviceChild* aliasList = nullptr;
static ID3D12DeviceChild* aliasQueue = nullptr;
static ID3D12Device* aliasDevice = nullptr;
static unsigned aliasListQueries = 0, aliasQueueQueries = 0;
static HRESULT STDMETHODCALLTYPE DeviceQuery(ID3D12DeviceChild* child, REFIID id, void** out)
{
    if (child == aliasList || child == aliasQueue)
    {
        if (child == aliasList) ++aliasListQueries; else ++aliasQueueQueries;
        return aliasDevice->QueryInterface(id, out);
    }
    return originalDeviceQuery(child, id, out);
}

// An owning base-interface QI may return IUnknown through multiple layers;
// callers must resolve the requested fence interface before using its methods.
class FenceAlias final : public ID3D12Fence
{
    std::atomic<ULONG> refs {1};
    ComPtr<ID3D12Fence> fence;
    ComPtr<IUnknown> base;
public:
    FenceAlias(ID3D12Fence* value, IUnknown* identity) : fence(value), base(identity) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override
    {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (id == DlssNr::NativeIdentity::streamlineBase)
            return base->QueryInterface(__uuidof(IUnknown), out);
        if (id == __uuidof(IUnknown) || id == __uuidof(ID3D12Object) ||
            id == __uuidof(ID3D12DeviceChild) || id == __uuidof(ID3D12Pageable) || id == __uuidof(ID3D12Fence))
        { *out = this; AddRef(); return S_OK; }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { const auto n = --refs; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID g, UINT* s, void* d) override { return fence->GetPrivateData(g,s,d); }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID g, UINT s, const void* d) override { return fence->SetPrivateData(g,s,d); }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID g, const IUnknown* d) override { return fence->SetPrivateDataInterface(g,d); }
    HRESULT STDMETHODCALLTYPE SetName(LPCWSTR n) override { return fence->SetName(n); }
    HRESULT STDMETHODCALLTYPE GetDevice(REFIID id, void** out) override { return fence->GetDevice(id,out); }
    UINT64 STDMETHODCALLTYPE GetCompletedValue() override { return fence->GetCompletedValue(); }
    HRESULT STDMETHODCALLTYPE SetEventOnCompletion(UINT64 v, HANDLE e) override { return fence->SetEventOnCompletion(v,e); }
    HRESULT STDMETHODCALLTYPE Signal(UINT64 v) override { return fence->Signal(v); }
};

static void TestMfgWaits(ID3D12Device* device)
{
    namespace P = DlssNr::PreFg;
    for (auto type : {D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_TYPE_COMPUTE})
    for (unsigned generated = 1; generated <= 5; ++generated)
    for (auto format : {DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R10G10B10A2_UNORM})
    for (bool sharedList : {false,true})
    for (bool retiring : {false,true})
    {
        ComPtr<ID3D12CommandQueue> queue;
        D3D12_COMMAND_QUEUE_DESC desc {}; desc.Type=type;
        Check(device->CreateCommandQueue(&desc,IID_PPV_ARGS(&queue)));
        ComPtr<ID3D12Fence> producer, finished;
        Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&producer)));
        Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&finished)));
        std::array<ComPtr<ID3D12CommandAllocator>,5> allocators;
        std::array<ComPtr<ID3D12GraphicsCommandList>,5> lists;
        const unsigned listCount=sharedList ? 1 : generated;
        for (unsigned i=0;i<listCount;++i) {
            Check(device->CreateCommandAllocator(type,IID_PPV_ARGS(&allocators[i])));
            Check(device->CreateCommandList(0,type,allocators[i].Get(),nullptr,IID_PPV_ARGS(&lists[i])));
        }
        ComPtr<ID3D12Resource> carrier;
        D3D12_RESOURCE_DESC carrierDesc {}; carrierDesc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        carrierDesc.Width=16; carrierDesc.Height=16; carrierDesc.DepthOrArraySize=1; carrierDesc.MipLevels=1;
        carrierDesc.Format=format; carrierDesc.SampleDesc.Count=1;
        D3D12_HEAP_PROPERTIES carrierHeap {}; carrierHeap.Type=D3D12_HEAP_TYPE_DEFAULT;
        Check(device->CreateCommittedResource(&carrierHeap,D3D12_HEAP_FLAG_NONE,&carrierDesc,
            D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&carrier)));
        auto* resource=carrier.Get();
        P::CompletionLedger ledger;
        const auto packet=ledger.Reserve(resource,3,4,5,100,101,P::CompletionKind::Probe,nullptr,true);
        assert(packet && ledger.Commit(packet,resource,producer.Get(),1,P::CompletionKind::Probe));
        const auto aggregate=ledger.Status(packet);
        P::StartupReadiness readiness;
        P::ReadinessIdentity identity {}; identity.provider=3;
        assert(readiness.Submit(identity,producer.Get(),1,101,aggregate));
        readiness.Presented(101,true);
        std::shared_ptr<Safety::ExternalWaitStatus> last;
        for (unsigned child=1;child<=generated;++child) {
            if (retiring && child==2) ledger.CloseAdmission();
            const auto claim=ledger.Claim(resource,retiring && child>1?9:3,4,5,100,101,generated,child);
            assert(claim.result==P::CompletionClaimResult::Ready);
            last=claim.dependency.status;
            assert(Safety::BindExternalWait(lists[sharedList?0:child-1].Get(),producer.Get(),1,100,101,last));
            assert(aggregate->bound==(child==generated));
            assert(!readiness.Poll(identity,device,102));
        }
        for (unsigned i=0;i<listCount;++i) {
            Check(lists[i]->Close());
            ID3D12CommandList* batch[]={lists[i].Get()};
            queue->ExecuteCommandLists(1,batch);
            assert(aggregate->applied==(i+1==listCount));
        }
        assert(!readiness.Poll(identity,device,102)); // all waits applied, producer incomplete
        Check(queue->Signal(finished.Get(),1));
        assert(finished->GetCompletedValue()==0);
        Check(producer->Signal(1));
        HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr); assert(event);
        Check(finished->SetEventOnCompletion(1,event));
        assert(WaitForSingleObject(event,5000)==WAIT_OBJECT_0);
        assert(readiness.Poll(identity,device,102));
        // Replaying a recording reapplies its wait without double-counting children.
        for (unsigned i=0;i<listCount;++i) {
            ID3D12CommandList* batch[]={lists[i].Get()};
            queue->ExecuteCommandLists(1,batch);
        }
        Check(queue->Signal(finished.Get(),2));
        Check(finished->SetEventOnCompletion(2,event));
        assert(WaitForSingleObject(event,5000)==WAIT_OBJECT_0);
        CloseHandle(event);
        assert(!aggregate->failed && readiness.Ready());
        // An ordinary duplicate is still refused and poisons readiness.
        assert(!Safety::BindExternalWait(lists[sharedList?0:generated-1].Get(),producer.Get(),1,100,101,last));
        assert(aggregate->failed && !readiness.Poll(identity,device,103));
        for (unsigned i=0;i<listCount;++i) Check(lists[i]->Reset(allocators[i].Get(),nullptr));
        // Reset of an unsubmitted MFG group must fail every sibling's proof.
        P::CompletionLedger canceled;
        const auto canceledPacket=canceled.Reserve(resource,3,4,5,200,201,P::CompletionKind::Probe,nullptr,true);
        assert(canceled.Commit(canceledPacket,resource,producer.Get(),2));
        for (unsigned child=1;child<=generated;++child) {
            auto claim=canceled.Claim(resource,3,4,5,200,201,generated,child);
            assert(Safety::BindExternalWait(lists[0].Get(),producer.Get(),2,200,201,claim.dependency.status));
        }
        assert(canceled.Status(canceledPacket)->bound && !canceled.Status(canceledPacket)->applied);
        Check(lists[0]->Close()); Check(lists[0]->Reset(allocators[0].Get(),nullptr));
        assert(canceled.Status(canceledPacket)->failed);
        for (unsigned i=0;i<listCount;++i) Check(lists[i]->Close());
        // A later child cannot inherit proof from an already submitted recording.
        Check(lists[0]->Reset(allocators[0].Get(),nullptr));
        P::CompletionLedger late;
        const auto latePacket=late.Reserve(resource,3,4,5,300,301,P::CompletionKind::Probe,nullptr,true);
        assert(late.Commit(latePacket,resource,producer.Get(),1));
        const auto first=late.Claim(resource,3,4,5,300,301,2,1);
        assert(Safety::BindExternalWait(lists[0].Get(),producer.Get(),1,300,301,first.dependency.status));
        Check(lists[0]->Close());
        ID3D12CommandList* submitted[]={lists[0].Get()};
        queue->ExecuteCommandLists(1,submitted);
        const auto lateChild=late.Claim(resource,3,4,5,300,301,2,2);
        assert(!Safety::BindExternalWait(lists[0].Get(),producer.Get(),1,300,301,lateChild.dependency.status));
        assert(late.Status(latePacket)->failed && !late.Status(latePacket)->applied);
        Check(queue->Signal(finished.Get(),3));
        event=CreateEventW(nullptr,FALSE,FALSE,nullptr); assert(event);
        Check(finished->SetEventOnCompletion(3,event));
        assert(WaitForSingleObject(event,5000)==WAIT_OBJECT_0); CloseHandle(event);
    }
    std::puts("PASS: MFG 2x-6x FP16/R10 DIRECT/COMPUTE, shared/separate lists, delayed GPU, aggregate readiness, replay, duplicate and canceled group");
}

int main()
{
    ComPtr<ID3D12Debug> debug;
    const bool debugEnabled = SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));
    if (debugEnabled) debug->EnableDebugLayer();
    std::puts(debugEnabled ? "D3D12 debug layer enabled." : "D3D12 debug layer unavailable; API validation omitted.");
    ComPtr<IDXGIFactory4> factory;
    ComPtr<IDXGIAdapter> warp;
    ComPtr<ID3D12Device> device;
    Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
    Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)));
    Check(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
    // The fence and FG list may expose different wrapper layers for one device.
    // Exercise the production comparison used at both bind and queue execution.
    ComPtr<IdentityProxy> deviceAlias, nestedAlias;
    deviceAlias.Attach(new IdentityProxy(device.Get()));
    nestedAlias.Attach(new IdentityProxy(deviceAlias.Get()));
    const auto aliasRefs = deviceAlias->References();
    assert(Safety::SameDevice(nestedAlias.Get(), device.Get()));
    assert(Safety::SameDevice(device.Get(), nestedAlias.Get()));
    assert(deviceAlias->References() == aliasRefs);
    assert(!Safety::SameDevice(nullptr, device.Get()));
    for (auto mode : {IdentityProxy::NullBase, IdentityProxy::Self, IdentityProxy::Error,
                      IdentityProxy::Unsupported})
    {
        ComPtr<IdentityProxy> invalid;
        invalid.Attach(new IdentityProxy(device.Get(), mode));
        assert(!Safety::SameDevice(invalid.Get(), device.Get()));
        assert(invalid->References() == 1);
    }
    ComPtr<ID3D12CommandQueue> queue, otherQueue;
    D3D12_COMMAND_QUEUE_DESC queueDesc {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    Check(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue)));
    Check(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&otherQueue)));
    ComPtr<ID3D12CommandAllocator> allocator, nextAllocator;
    Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
    Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&nextAllocator)));
    ComPtr<ID3D12GraphicsCommandList> list;
    Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list)));
    auto submit = [&](ID3D12CommandQueue* q) { ID3D12CommandList* lists[] = {list.Get()}; q->ExecuteCommandLists(1, lists); };

    // The provider list must wait on the producer fence without blocking the CPU.
    ComPtr<ID3D12Fence> externalProducer, externalConsumer;
    Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&externalProducer)));
    Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&externalConsumer)));
    ComPtr<IdentityProxy> fenceIdentity;
    fenceIdentity.Attach(new IdentityProxy(externalProducer.Get()));
    ComPtr<FenceAlias> fenceAlias;
    fenceAlias.Attach(new FenceAlias(externalProducer.Get(), fenceIdentity.Get()));
    ComPtr<ID3D12Device> foreignDevice;
    for (UINT i = 0;; ++i)
    {
        ComPtr<IDXGIAdapter1> candidate;
        if (factory->EnumAdapters1(i, &candidate) == DXGI_ERROR_NOT_FOUND) break;
        if (SUCCEEDED(D3D12CreateDevice(candidate.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&foreignDevice))) &&
            !Safety::SameDevice(foreignDevice.Get(), device.Get())) break;
        foreignDevice.Reset();
    }
    if (foreignDevice)
    {
        ComPtr<ID3D12Fence> foreignFence;
        Check(foreignDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&foreignFence)));
        auto foreignStatus = std::make_shared<Safety::ExternalWaitStatus>();
        assert(!Safety::BindExternalWait(list.Get(), foreignFence.Get(), 1, 39, 40, foreignStatus));
        assert(foreignStatus->failed && !foreignStatus->bound && !foreignStatus->applied);
        std::puts("PASS: distinct-device fence rejected before binding");
    }
    else std::puts("SKIP: no distinct D3D12 device available for foreign-fence check");
    for (auto mode : {IdentityProxy::NullBase, IdentityProxy::Self, IdentityProxy::Error,
                      IdentityProxy::Unsupported})
    {
        ComPtr<IdentityProxy> invalidIdentity;
        invalidIdentity.Attach(new IdentityProxy(externalProducer.Get(), mode));
        ComPtr<FenceAlias> invalidFence;
        invalidFence.Attach(new FenceAlias(externalProducer.Get(), invalidIdentity.Get()));
        auto invalidStatus = std::make_shared<Safety::ExternalWaitStatus>();
        assert(!Safety::BindExternalWait(list.Get(), invalidFence.Get(), 1, 39, 40, invalidStatus));
        assert(invalidStatus->failed && !invalidStatus->bound && !invalidStatus->applied);
    }
    assert(!Safety::BindExternalWait(nullptr, externalProducer.Get(), 1, 41, 42));
    assert(!Safety::BindExternalWait(list.Get(), nullptr, 1, 41, 42));
    assert(!Safety::BindExternalWait(list.Get(), externalProducer.Get(), 0, 41, 42));
    assert(!Safety::BindExternalWait(list.Get(), externalProducer.Get(), UINT64_MAX, 41, 42));
    namespace PreFg = DlssNr::PreFg;
    PreFg::CompletionLedger probeLedger;
    auto* probeResource = reinterpret_cast<ID3D12Resource*>(uintptr_t(0x1000));
    const auto probeReservation = probeLedger.Reserve(probeResource, 3, 4, 5, 41, 42, PreFg::CompletionKind::Probe);
    assert(probeReservation && probeLedger.Commit(probeReservation, probeResource,
        externalProducer.Get(), 1, PreFg::CompletionKind::Probe));
    assert(probeLedger.Claim(probeResource, 3, 4, 5, 40, 42).result == PreFg::CompletionClaimResult::Refused);
    assert(probeLedger.Claim(probeResource, 3, 4, 6, 41, 42).result == PreFg::CompletionClaimResult::Refused);
    const auto probe = probeLedger.Claim(probeResource, 3, 4, 5, 41, 42);
    assert(probe.result == PreFg::CompletionClaimResult::Ready &&
        probe.dependency.kind == PreFg::CompletionKind::Probe && probe.dependency.reservation == probeReservation);
    assert(probeLedger.Claim(probeResource, 3, 4, 5, 41, 42).result == PreFg::CompletionClaimResult::Refused);
    auto waitStatus = probe.dependency.status;
    ComPtr<ReShadeDeviceProxy> reshadeDevice;
    reshadeDevice.Attach(new ReShadeDeviceProxy(device.Get()));
    aliasDevice = reshadeDevice.Get(); aliasList = list.Get(); aliasQueue = otherQueue.Get();
    originalDeviceQuery = reinterpret_cast<decltype(originalDeviceQuery)>((*(void***)list.Get())[7]);
    // This fixture requires the runtime's shared child GetDevice implementation.
    assert((*(void***)otherQueue.Get())[7] == reinterpret_cast<void*>(originalDeviceQuery));
    assert(DetourTransactionBegin() == NO_ERROR);
    assert(DetourUpdateThread(GetCurrentThread()) == NO_ERROR);
    assert(DetourAttach(reinterpret_cast<PVOID*>(&originalDeviceQuery), DeviceQuery) == NO_ERROR);
    assert(DetourTransactionCommit() == NO_ERROR);
    PreFg::StartupReadiness certificate;
    PreFg::ReadinessIdentity certificateKey {};
    certificateKey.provider = 3; certificateKey.nativeGeneration = 4; certificateKey.nativeInstance = 5;
    assert(certificate.Submit(certificateKey, probe.dependency.fence.Get(), probe.dependency.value,
        probe.dependency.sequence, waitStatus));
    certificate.Presented(42, true);
    assert(Safety::BindExternalWait(list.Get(), fenceAlias.Get(), 1, 41, 42, waitStatus));
    assert(Safety::Get<Safety::ExternalWait>(list.Get(), Safety::externalWaitGuid)->fence.Get() == externalProducer.Get());
    assert(waitStatus->bound && !waitStatus->applied && !waitStatus->failed);
    assert(!Safety::BindExternalWait(list.Get(), externalProducer.Get(), 1, 43, 44));
    Check(list->Close());
    submit(otherQueue.Get());
    assert(Safety::Get<Safety::ExternalWait>(list.Get(), Safety::externalWaitGuid));
    Check(otherQueue->Signal(externalConsumer.Get(), 1));
    assert(externalConsumer->GetCompletedValue() == 0);
    for (unsigned int i = 0; i < 1000; ++i) assert(!certificate.Poll(certificateKey, device.Get(), 43 + i));
    Check(externalProducer->Signal(1));
    assert(waitStatus->applied && !waitStatus->failed);
    HANDLE externalDone = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    assert(externalDone);
    Check(externalConsumer->SetEventOnCompletion(1, externalDone));
    assert(WaitForSingleObject(externalDone, 5000) == WAIT_OBJECT_0);
    CloseHandle(externalDone);
    assert(certificate.Poll(certificateKey, device.Get(), 43));
    probeLedger.RetireBefore(43);
    assert(probeLedger.Claim(probeResource, 3, 4, 5, 43, 43).result == PreFg::CompletionClaimResult::None);
    assert(certificate.Ready()); // retained proof survives ordinary packet retirement
    assert(aliasListQueries > 0 && aliasQueueQueries > 0);
    assert(DetourTransactionBegin() == NO_ERROR);
    assert(DetourUpdateThread(GetCurrentThread()) == NO_ERROR);
    assert(DetourDetach(reinterpret_cast<PVOID*>(&originalDeviceQuery), DeviceQuery) == NO_ERROR);
    assert(DetourTransactionCommit() == NO_ERROR);
    aliasList = aliasQueue = nullptr; aliasDevice = nullptr;
    assert(reshadeDevice->References() == 1);
    std::puts("PASS: ReShade device identity on real list bind, queue wait and readiness activation");
    // A provider may reuse a list whose completed wait cookie is still attached.
    // The finished producer can retire without Reset, but the new unfinished
    // dependency must survive duplicates and further attempted replacements.
    auto replacementStatus = std::make_shared<Safety::ExternalWaitStatus>();
    assert(Safety::BindExternalWait(list.Get(), externalProducer.Get(), 2, 43, 44, replacementStatus));
    assert(waitStatus->applied && !waitStatus->failed && certificate.Ready());
    assert(!Safety::BindExternalWait(list.Get(), externalProducer.Get(), 2, 43, 44));
    assert(!Safety::BindExternalWait(list.Get(), externalProducer.Get(), 3, 45, 46));
    assert(Safety::Get<Safety::ExternalWait>(list.Get(), Safety::externalWaitGuid)->status == replacementStatus);
    Check(list->Reset(allocator.Get(), nullptr));
    assert(!Safety::Get<Safety::ExternalWait>(list.Get(), Safety::externalWaitGuid));
    assert(replacementStatus->failed && !replacementStatus->applied);

    // Completed production alone must never fabricate a provider queue-wait
    // acknowledgement. Replacing an unexecuted old wait cancels its proof.
    auto unobservedStatus = std::make_shared<Safety::ExternalWaitStatus>();
    auto retainedStatus = std::make_shared<Safety::ExternalWaitStatus>();
    assert(Safety::BindExternalWait(list.Get(), externalProducer.Get(), 1, 50, 51, unobservedStatus));
    assert(Safety::BindExternalWait(list.Get(), externalProducer.Get(), 1, 52, 53, retainedStatus));
    assert(unobservedStatus->failed && !unobservedStatus->applied);
    assert(retainedStatus->bound && !retainedStatus->failed && !retainedStatus->applied);
    Check(list->Close());
    Check(list->Reset(nextAllocator.Get(), nullptr));
    assert(retainedStatus->failed && !retainedStatus->applied);

    // Reset before submission cancels the borrowed dependency.
    ComPtr<ID3D12Fence> canceledProducer;
    Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&canceledProducer)));
    std::atomic<uint64_t> failureEpoch {1};
    auto canceledStatus = std::make_shared<Safety::ExternalWaitStatus>();
    canceledStatus->failureEpoch = &failureEpoch;
    assert(Safety::BindExternalWait(list.Get(), canceledProducer.Get(), 1, 45, 46, canceledStatus));
    Check(list->Close());
    Check(list->Reset(nextAllocator.Get(), nullptr));
    Check(list->Close());
    submit(otherQueue.Get());
    Check(otherQueue->Signal(externalConsumer.Get(), 2));
    externalDone = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    assert(externalDone);
    Check(externalConsumer->SetEventOnCompletion(2, externalDone));
    assert(WaitForSingleObject(externalDone, 5000) == WAIT_OBJECT_0);
    CloseHandle(externalDone);
    Check(list->Reset(allocator.Get(), nullptr));

    auto abandoned = Safety::Record(list.Get());
    assert(!Safety::OrderBefore(abandoned, otherQueue.Get()));
    assert(!Safety::OrderBefore(abandoned, queue.Get()));
    assert(!Safety::OrderedOn(abandoned, queue.Get()));
    assert(abandoned && !Safety::Reusable(abandoned) && !Safety::Drain(0));
    const auto abandonedReport=Safety::InspectPending();
    assert(abandonedReport.slots.unsubmittedUnsealed>0 && !abandonedReport.slots.registryFailed);
    const auto abandonedEntry=std::find_if(abandonedReport.recordings.begin(),abandonedReport.recordings.end(),
        [&](const auto& entry){return entry.incarnation==abandoned->incarnation;});
    assert(abandonedEntry!=abandonedReport.recordings.end() && !abandonedEntry->sealed &&
        abandonedEntry->submissions==0 && abandonedEntry->completion.empty());
    assert(!Safety::Reusable(abandoned) && !Safety::Drain(0)); // diagnostic grants nothing
    const auto boundedReport=Safety::InspectPending(0,0);
    assert(boundedReport.recordings.empty() && boundedReport.omitted>0);
    Check(list->Close());
    Check(list->Reset(allocator.Get(), nullptr));
    assert(Safety::Reusable(abandoned) && !Safety::Readable(abandoned));

    ComPtr<ID3D12Fence> gate;
    Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gate)));
    Check(queue->Wait(gate.Get(), 1));
    auto pending = Safety::Record(list.Get());
    auto retirement = Safety::Pending();
    bool retiredSessionReleased = false;
    bool replacementSessionCreated = false;
    Check(list->Close());
    submit(queue.Get());
    assert(Safety::OrderBefore(pending, queue.Get())); // submitted, GPU blocked, not Reset
    assert(!Safety::Reusable(pending) && !Safety::Readable(pending)); // ordering does not retire work
    assert(!Safety::OrderBefore(pending, otherQueue.Get())); // unsealed lists could replay
    Check(list->Reset(nextAllocator.Get(), nullptr)); // list Reset is legal while old work runs
    assert(Safety::OrderedOn(pending, queue.Get())); // GPU need not be CPU-complete
    assert(!Safety::OrderedOn(pending, otherQueue.Get()));
    ComPtr<ID3D12Fence> consumerPassed;
    Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&consumerPassed)));
    assert(Safety::OrderBefore(pending, otherQueue.Get()));
    Check(otherQueue->Signal(consumerPassed.Get(), 1));
    assert(consumerPassed->GetCompletedValue() == 0); // wait cannot pass the blocked producer
    for (int i = 0; i < 1000; ++i)
    {
        if (Safety::Reusable(retirement)) retiredSessionReleased = true;
        if (retiredSessionReleased) replacementSessionCreated = true;
        assert(!Safety::Reusable(pending) && !Safety::Readable(pending) && !retiredSessionReleased &&
               !replacementSessionCreated);
    }
    assert(!Safety::Drain(1));
    const auto pendingReport=Safety::InspectPending();
    const auto pendingEntry=std::find_if(pendingReport.recordings.begin(),pendingReport.recordings.end(),
        [&](const auto& entry){return entry.incarnation==pending->incarnation;});
    assert(pendingEntry!=pendingReport.recordings.end() && pendingEntry->sealed && pendingEntry->submissions==1);
    assert(pendingEntry->state==Safety::RecordingTerminalState::Reset && pendingEntry->completion.size()==1);
    assert(pendingEntry->completion[0].timeline!=0 &&
        pendingEntry->completion[0].observed<pendingEntry->completion[0].required);
    Check(gate->Signal(1));
    HANDLE handoffDone = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    assert(handoffDone);
    Check(consumerPassed->SetEventOnCompletion(1, handoffDone));
    assert(WaitForSingleObject(handoffDone, 5000) == WAIT_OBJECT_0);
    CloseHandle(handoffDone);
    assert(Safety::Drain(5000));
    if (Safety::Reusable(retirement)) retiredSessionReleased = true;
    if (retiredSessionReleased) replacementSessionCreated = true;
    assert(Safety::Reusable(pending) && Safety::Readable(pending) && retiredSessionReleased &&
           replacementSessionCreated);
    assert(Safety::TimestampFrequency(pending) > 0);

    auto replay = Safety::Record(list.Get());
    Check(list->Close());
    submit(queue.Get());
    assert(Safety::Drain(5000));
    assert(!Safety::Reusable(replay)); // completed but still executable
    const auto replayReport=Safety::InspectPending();
    assert(replayReport.slots.completedUnsealed>0);
    const auto replayEntry=std::find_if(replayReport.recordings.begin(),replayReport.recordings.end(),
        [&](const auto& entry){return entry.incarnation==replay->incarnation;});
    assert(replayEntry!=replayReport.recordings.end() && !replayEntry->sealed && replayEntry->submissions==1);
    assert(replayEntry->completion[0].observed>=replayEntry->completion[0].required);
    std::puts("shutdown diagnostics: unsubmitted, delayed GPU, completed replayable, bounded read-only PASS");
    Check(queue->Wait(gate.Get(), 2));
    submit(queue.Get());
    Check(list->Reset(allocator.Get(), nullptr));
    assert(!Safety::OrderedOn(replay, queue.Get()));
    assert(!Safety::OrderBefore(replay, queue.Get())); // observed replay still fails same-queue admission
    assert(!Safety::Reusable(replay) && !Safety::Drain(0));
    Check(gate->Signal(2));
    assert(Safety::Drain(5000) && Safety::Reusable(replay));

    // Sealing an owned recording never bypasses the actual GPU completion point.
    ComPtr<ID3D12Fence> ownedGate;
    Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&ownedGate)));
    Check(queue->Wait(ownedGate.Get(), 1));
    auto owned = Safety::Record(list.Get());
    auto ownedRetirement = Safety::Pending();
    namespace Receipts=DlssNr::SignalReceipts;
    Receipts::Tracker<Safety::Ticket> receipts;Receipts::Epoch receiptEpoch{1,1,1,1,1,1,2};
    const auto receiptSequence=receipts.Begin(receiptEpoch,100);
    Receipts::Fact motionFact;motionFact.present=true;motionFact.width=2258;motionFact.height=1270;
    receipts.Observe(receiptSequence,Receipts::Signal::Motion,motionFact);
    receipts.Values(receiptSequence,Receipts::Signal::Motion,2256,1264);
    receipts.Admit(receiptSequence,Receipts::Signal::Motion);
    receipts.Record(receiptSequence,owned,Receipts::Bit(Receipts::Signal::Motion));
    const auto inspect=[](const Safety::Ticket& ticket){const auto p=Safety::InspectRecording(ticket);
        return Receipts::Proof{p.valid&&!(p.reusable&&!p.submitted),p.submitted,p.completed};};

    assert(!Safety::SealOwnedRecording(list.Get())); // still unsubmitted
    Check(list->Close());
    submit(queue.Get());
    assert(Safety::SealOwnedRecording(list.Get()));
    assert(!Safety::Reusable(owned) && !Safety::Readable(owned) && !Safety::Reusable(ownedRetirement));
    receipts.Poll(inspect);
    assert(receipts.Read(Receipts::Signal::Motion,receiptEpoch,101,Receipts::Mode::Active).state==Receipts::State::Pending);
    Check(ownedGate->Signal(1));
    assert(Safety::Drain(5000));
    assert(Safety::Reusable(owned) && Safety::Readable(owned) && Safety::Reusable(ownedRetirement));
    receipts.Poll(inspect);
    const auto receipt=receipts.Read(Receipts::Signal::Motion,receiptEpoch,102,Receipts::Mode::Active);
    assert(receipt.state==Receipts::State::Consumed&&receipt.fact.x==2256&&receipt.fact.y==1264);
    ++receiptEpoch.route;
    assert(receipts.Read(Receipts::Signal::Motion,receiptEpoch,103,Receipts::Mode::Active).state==Receipts::State::Pending);
    puts("PASS: Present diagnostic receipt waits for real owned-recording GPU completion and retains exact model values");
    Check(list->Reset(allocator.Get(), nullptr));

    // Track both queues without introducing a cycle into the host's Wait/Signal graph.
    Check(queue->Wait(gate.Get(), 3));
    auto first = Safety::Record(list.Get());
    Check(list->Close());
    submit(queue.Get());
    Check(list->Reset(nextAllocator.Get(), nullptr));
    auto second = Safety::Record(list.Get());
    Check(list->Close());
    submit(otherQueue.Get());
    Check(list->Reset(allocator.Get(), nullptr));
    assert(!Safety::Reusable(first));
    // This queue must be able to release the first queue, even though it submitted NR second.
    Check(otherQueue->Signal(gate.Get(), 3));
    assert(Safety::Drain(5000) && Safety::Reusable(first) && Safety::Reusable(second));

    // A single recording replayed on another queue needs both completion points.
    auto multiQueue = Safety::Record(list.Get());
    Check(list->Close());
    submit(queue.Get());
    assert(Safety::Drain(5000));
    Check(otherQueue->Wait(gate.Get(), 4));
    submit(otherQueue.Get());
    Check(list->Reset(nextAllocator.Get(), nullptr));
    assert(!Safety::Reusable(multiQueue) && !Safety::Readable(multiQueue));
    Check(gate->Signal(4));
    assert(Safety::Drain(5000) && Safety::Reusable(multiQueue));
    assert(Safety::TimestampFrequency(multiQueue) == 0); // no ambiguous timing calculation
    assert(!Safety::OrderBefore(multiQueue, otherQueue.Get()));

    auto destroyedInFlight = Safety::Record(list.Get());
    Check(list->Close());
    Check(queue->Wait(gate.Get(), 5));
    submit(queue.Get());
    list.Reset();
    assert(!Safety::Reusable(destroyedInFlight));
    Check(gate->Signal(5));
    assert(Safety::Drain(5000) && Safety::Reusable(destroyedInFlight));
    Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list)));

    auto destroyed = Safety::Record(list.Get());
    list.Reset();
    assert(Safety::Reusable(destroyed) && !Safety::Readable(destroyed));
    assert(Safety::Drain(0));
    Safety::NewSession();
    assert(Safety::Pending().empty());

    // Capture must not copy a new shape into an old footprint or free an unsubmitted copy.
    Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list)));
    auto makeTexture = [&](UINT width, UINT height = 4,
                           DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM, UINT16 mips = 1)
    {
        ComPtr<ID3D12Resource> texture;
        D3D12_HEAP_PROPERTIES heap {}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC desc {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = width; desc.Height = height; desc.DepthOrArraySize = 1; desc.MipLevels = mips;
        desc.Format = format; desc.SampleDesc.Count = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&texture)));
        return texture;
    };
    auto smallTexture = makeTexture(4), largeTexture = makeTexture(8);
    capture::FrameCapture capture;
    capture.request(2);
    capture.record(list.Get(), device.Get(), smallTexture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                   smallTexture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    assert(capture.progress() == 1);
    capture.record(list.Get(), device.Get(), largeTexture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                   largeTexture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    assert(capture.progress() == 1 && !capture.readyToWrite());
    Check(list->Close());
    Check(list->Reset(nextAllocator.Get(), nullptr)); // discard the old recording
    assert(capture.readyToWrite());
    assert(capture.write("unused-capture-test-path").empty());
    assert(capture.isActive() && capture.progress() == 0); // rearmed, no files written

    // A temporary unsupported replacement must not silently cancel the rearmed request.
    auto mipTexture = makeTexture(8, 4, DXGI_FORMAT_R8G8B8A8_UNORM, 2);
    capture.record(list.Get(), device.Get(), largeTexture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                   mipTexture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    assert(capture.isActive() && capture.progress() == 0 && !capture.readyToWrite());
    capture.record(nullptr, device.Get(), largeTexture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                   largeTexture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    capture.record(list.Get(), nullptr, largeTexture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                   largeTexture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    assert(capture.isActive() && capture.progress() == 0);
    capture.record(list.Get(), device.Get(), largeTexture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                   largeTexture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    assert(capture.progress() == 1);
    Check(list->Close());
    Check(list->Reset(allocator.Get(), nullptr));
    capture.release();

    // Exercise both inputs and each copy-relevant resize independently. The original test
    // canceled its list: these copies are actually submitted behind a GPU gate.
    auto tallTexture = makeTexture(4, 8);
    auto floatTexture = makeTexture(4, 4, DXGI_FORMAT_R16G16B16A16_FLOAT);
    UINT64 captureGate = 6;
    for (auto* replacement : {largeTexture.Get(), tallTexture.Get(), floatTexture.Get(), mipTexture.Get()})
    {
        for (bool changeBefore : {false, true})
        {
            capture.request(2);
            capture.record(list.Get(), device.Get(), smallTexture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                           smallTexture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            assert(capture.progress() == 1);
            Check(list->Close());
            Check(queue->Wait(gate.Get(), captureGate));
            submit(queue.Get());
            Check(list->Reset(nextAllocator.Get(), nullptr));
            capture.record(list.Get(), device.Get(), changeBefore ? replacement : smallTexture.Get(),
                           D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                           changeBefore ? smallTexture.Get() : replacement,
                           D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            for (int poll = 0; poll < 100; ++poll)
                assert(capture.progress() == 1 && !capture.readyToWrite());
            Check(gate->Signal(captureGate++));
            assert(Safety::Drain(5000));
            assert(capture.readyToWrite());
            assert(capture.write("unused-capture-test-path").empty());
            assert(capture.isActive() && capture.progress() == 0);
            capture.release();
            Check(list->Close());
            Check(list->Reset(allocator.Get(), nullptr));
        }
    }

    // A completed but replayable capture still owns all shots. Saturating the requested
    // bound must neither overrun the vectors nor start a new size while waiting.
    capture.request(capture::kMaxFrames + 10);
    for (unsigned int i = 0; i < capture::kMaxFrames + 10; ++i)
        capture.record(list.Get(), device.Get(), smallTexture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                       smallTexture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    assert(capture.progress() == capture::kMaxFrames);
    Check(list->Close());
    submit(queue.Get());
    assert(Safety::Drain(5000) && !capture.readyToWrite());
    Check(queue->Wait(gate.Get(), captureGate));
    submit(queue.Get());
    Check(list->Reset(nextAllocator.Get(), nullptr));
    capture.record(list.Get(), device.Get(), largeTexture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                   largeTexture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    assert(capture.progress() == capture::kMaxFrames && !capture.readyToWrite());
    Check(gate->Signal(captureGate));
    assert(Safety::Drain(5000) && capture.readyToWrite());
    capture.release();
    list.Reset();

    // Exercise actual DIRECT -> COMPUTE -> DIRECT provider queues. CPU admission
    // stays non-blocking while a different GPU queue holds the producer fence.
    for (auto providerType : {D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_TYPE_COMPUTE,
                              D3D12_COMMAND_LIST_TYPE_DIRECT})
    {
        ComPtr<ID3D12CommandQueue> providerQueue;
        D3D12_COMMAND_QUEUE_DESC desc {}; desc.Type = providerType;
        Check(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&providerQueue)));
        ComPtr<ID3D12CommandAllocator> providerAllocator;
        ComPtr<ID3D12GraphicsCommandList> providerList;
        Check(device->CreateCommandAllocator(providerType, IID_PPV_ARGS(&providerAllocator)));
        Check(device->CreateCommandList(0, providerType, providerAllocator.Get(), nullptr, IID_PPV_ARGS(&providerList)));
        auto observation = Safety::ObserveExternalExecution(providerList.Get());
        assert(observation && !observation->Ready());
        observation->evaluated = true;
        assert(!observation->Ready()); // successful evaluate alone is not submission
        ComPtr<ID3D12Fence> producer, blocker, consumer;
        Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&producer)));
        Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&blocker)));
        Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&consumer)));
        Check(queue->Wait(blocker.Get(), 1));
        Check(queue->Signal(producer.Get(), 1));
        auto status = std::make_shared<Safety::ExternalWaitStatus>();
        assert(Safety::BindExternalWait(providerList.Get(), producer.Get(), 1, 100, 101, status));
        Check(providerList->Close());
        ID3D12CommandList* batch[] = {providerList.Get()};
        providerQueue->ExecuteCommandLists(1, batch);
        assert(observation->Ready() && status->applied && !status->failed);
        Check(providerQueue->Signal(consumer.Get(), 1));
        assert(consumer->GetCompletedValue() == 0);
        Check(blocker->Signal(1));
        HANDLE done = CreateEventW(nullptr, FALSE, FALSE, nullptr); assert(done);
        Check(consumer->SetEventOnCompletion(1, done));
        assert(WaitForSingleObject(done, 5000) == WAIT_OBJECT_0);
        // Replay retains the exact dependency on the same compatible queue.
        providerQueue->ExecuteCommandLists(1, batch);
        Check(providerQueue->Signal(consumer.Get(), 2));
        Check(consumer->SetEventOnCompletion(2, done));
        assert(WaitForSingleObject(done, 5000) == WAIT_OBJECT_0);
        CloseHandle(done);
        Check(providerList->Reset(providerAllocator.Get(), nullptr));
        assert(observation->Ready());
        auto canceled = Safety::ObserveExternalExecution(providerList.Get());
        assert(canceled); canceled->evaluated = true;
        Check(providerList->Close());
        Check(providerList->Reset(providerAllocator.Get(), nullptr));
        assert(canceled->failed && !canceled->Ready());
    }
    {
        ComPtr<ID3D12CommandAllocator> copyAllocator;
        ComPtr<ID3D12GraphicsCommandList> copyList;
        Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY, IID_PPV_ARGS(&copyAllocator)));
        Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COPY, copyAllocator.Get(), nullptr,
                                       IID_PPV_ARGS(&copyList)));
        assert(!Safety::ObserveExternalExecution(copyList.Get()));
        assert(!Safety::BindExternalWait(copyList.Get(), externalProducer.Get(), 1, 110, 111));
        Check(copyList->Close());
    }
    std::puts("PASS: DIRECT/COMPUTE/DIRECT actual queue handoffs, cross-queue waits, replay, observation cancellation and COPY refusal");
    TestMfgWaits(device.Get());

    if (debugEnabled)
    {
        ComPtr<ID3D12InfoQueue> messages;
        Check(device.As(&messages));
        for (UINT64 i = 0; i < messages->GetNumStoredMessages(); ++i)
        {
            SIZE_T bytes = 0;
            Check(messages->GetMessage(i, nullptr, &bytes));
            std::vector<unsigned char> storage(bytes);
            auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
            Check(messages->GetMessage(i, message, &bytes));
            if (message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR)
            {
                std::fprintf(stderr, "D3D12 error: %s\n", message->pDescription);
                assert(false);
            }
        }
    }

    // An invalid consumer queue must not execute the dependent batch, or report
    // its recording complete. Exercise the real hook with an intercepted trampoline
    // so the intentionally wrong queue/list pairing never reaches the GPU.
    ComPtr<ID3D12GraphicsCommandList> refusedList;
    Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
                                   IID_PPV_ARGS(&refusedList)));
    auto refusedTicket = Safety::Record(refusedList.Get());
    assert(refusedTicket);
    assert(canceledStatus->failed && !canceledStatus->applied && failureEpoch == 2);
    auto refusedStatus = std::make_shared<Safety::ExternalWaitStatus>();
    refusedStatus->failureEpoch = &failureEpoch;
    assert(Safety::BindExternalWait(refusedList.Get(), externalProducer.Get(), 2, 47, 48, refusedStatus));
    Check(refusedList->Close());
    ComPtr<ID3D12CommandQueue> computeQueue;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
    Check(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&computeQueue)));
    static unsigned int forwarded = 0;
    const auto savedExecute = Safety::originalExecute;
    Safety::originalExecute = [](ID3D12CommandQueue*, UINT, ID3D12CommandList* const*) { ++forwarded; };
    ID3D12CommandList* refusedBatch[] = {refusedList.Get()};
    Safety::Execute(computeQueue.Get(), 1, refusedBatch);
    Safety::originalExecute = savedExecute;
    assert(forwarded == 0 && Safety::State().failed && refusedTicket->failed);
    assert(refusedStatus->bound && !refusedStatus->applied && refusedStatus->failed && failureEpoch == 3);
    assert(Safety::Get<Safety::ExternalWait>(refusedList.Get(), Safety::externalWaitGuid));
    assert(!Safety::Reusable(refusedTicket) && !Safety::Readable(refusedTicket));
    refusedList.Reset();

    const auto failedReport=Safety::InspectPending();
    assert(failedReport.slots.registryFailed && failedReport.slots.failed>0);
    assert(!Safety::Reusable(refusedTicket));

    // Fence failure is a permanent failure, never mistaken for permission to reclaim.
    auto failed = std::make_shared<Safety::Recording>();
    failed->sealed = true;
    failed->failed = true;
    assert(!Safety::Reusable(failed) && !Safety::Readable(failed));
    ComPtr<ID3D12Device5> removable;
    Check(device.As(&removable));
    removable->RemoveDevice();
    assert(!certificate.Poll(certificateKey, device.Get(), 44));
    // The sentinel UINT64_MAX is device loss, not a very large successful fence value.
    assert(!Safety::Reusable(pending) && !Safety::Readable(pending));
    const auto lossReport=Safety::InspectPending();
    assert(lossReport.slots.failed>0 && lossReport.slots.registryFailed);
    std::puts("PASS: unsubmitted cancellation, 1000 premature reuse/read checks, delayed GPU completion,");
    std::puts("      replay, explicit owned-list sealing, independent queues/host dependencies, retirement,");
    std::puts("      capture shape changes, failure and device loss.");
}
