#include <nr/d3d12/NativeRecordingState.h>
#include <d3dcompiler.h>
#include <d3d12sdklayers.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <cassert>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Neurotic::D3D12;
using Microsoft::WRL::ComPtr;

static unsigned checks = 0;
static void Check(bool value, const char* expression, int line)
{
    ++checks;
    if (!value)
    {
        std::cerr << "line=" << line << " failed prerequisite=" << expression << '\n';
        throw std::runtime_error("journal fixture prerequisite failed");
    }
}
#undef assert
#define assert(expression) Check(!!(expression), #expression, __LINE__)

// Real IUnknown ownership behavior without implementing unrelated D3D12 APIs.
// These objects are used only by state-model tests; the WARP tests use real COM.
struct ReferenceProbe final : IUnknown
{
    ULONG references = 1;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override
    {
        if (!result) return E_POINTER;
        *result = nullptr;
        if (iid != __uuidof(IUnknown)) return E_NOINTERFACE;
        *result = this;
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references; }
    ULONG STDMETHODCALLTYPE Release() override { return --references; }
    template<class T> T* As() { return reinterpret_cast<T*>(this); }
};

#include "native_depth_bias_state.h"
#include "native_recording_warp_scene.h"
#include "native_depth_bias_gpu.h"

template<class D> bool TraceHas(const D& d, const char* operation, std::uint64_t a)
{
    if constexpr (requires { d.traceCount; d.trace; })
        for (UINT i=0;i<d.traceCount;++i)
            if (std::string(d.trace[i].operation)==operation && d.trace[i].a==a) return true;
    return false;
}
template<class D> bool TraceWork(const D& d, const char* operation, std::uint64_t ordinal)
{
    if constexpr (requires { d.traceCount; d.trace; })
        for (UINT i=0;i<d.traceCount;++i)
            if (std::string(d.trace[i].operation)==operation && d.trace[i].workOrdinal==ordinal) return true;
    return false;
}
template<class D> bool TraceBounded(const D& d)
{
    if constexpr (requires { d.traceCount; d.traceTotal; d.trace; })
        return d.traceCount==d.trace.size() && d.traceTotal>d.traceCount &&
            d.trace[0].sequence==d.traceTotal-d.traceCount+1 &&
            d.trace[d.traceCount-1].sequence==d.traceTotal;
    return false;
}
static void CaptureTraceRegressions()
{
    ReferenceProbe objects[6];
    auto* list=objects[0].As<ID3D12GraphicsCommandList>();
    auto* pso=objects[1].As<ID3D12PipelineState>();
    auto* root=objects[2].As<ID3D12RootSignature>();
    ID3D12DescriptorHeap* heaps[]={objects[3].As<ID3D12DescriptorHeap>()};
    ID3D12DescriptorHeap* other[]={objects[4].As<ID3D12DescriptorHeap>()};
    NativeRecordingState s; NativeStateCaptureDiagnostic d;
    assert(s.RegisterLayout(root,{{RootKind::Table}}));
    assert(s.Begin(list,1,pso));
    assert(s.ObserveDescriptorHeaps(1,1,heaps));
    assert(s.ObserveComputeRootSignature(1,root));
    assert(!s.Capture(list,1,RestoreMask::Compute,&d));
    assert(TraceHas(d,"compute-signature-changed",reinterpret_cast<std::uint64_t>(root)));
    assert(!TraceHas(d,"compute-table",0));
    assert(s.ObserveComputeTable(1,0,{1234}));
    assert(s.ObserveWork(1));
    assert(s.ObserveDescriptorHeaps(1,1,other));
    assert(s.ObserveDescriptorHeaps(1,1,other));
    assert(!s.Capture(list,1,RestoreMask::Compute,&d));
    assert(TraceHas(d,"compute-table",0));
    assert(TraceHas(d,"heaps-changed",reinterpret_cast<std::uint64_t>(other[0])));
    assert(TraceHas(d,"heaps-same",reinterpret_cast<std::uint64_t>(other[0])));
    assert(TraceWork(d,"heaps-same",1));
    for(unsigned i=0;i<100;++i) s.ObservePipeline(1,pso);
    assert(!s.Capture(list,1,RestoreMask::Compute,&d));
    assert(TraceBounded(d));
    assert(s.Begin(list,2,pso));
    assert(!s.Capture(list,2,RestoreMask::Compute,&d));
    assert(TraceHas(d,"begin",reinterpret_cast<std::uint64_t>(pso)));
    assert(TraceWork(d,"begin",0));
    assert(!TraceHas(d,"compute-table",0));
    assert(!TraceBounded(d));
}

static void HeapInvalidationRestoreChecks()
{
    ReferenceProbe objects[6];
    auto* list=objects[0].As<ID3D12GraphicsCommandList>();
    auto* pso=objects[1].As<ID3D12PipelineState>();
    auto* root=objects[2].As<ID3D12RootSignature>();
    ID3D12DescriptorHeap* first[]={objects[3].As<ID3D12DescriptorHeap>()};
    ID3D12DescriptorHeap* second[]={objects[4].As<ID3D12DescriptorHeap>()};
    const auto mask=RestoreMask::Compute|RestoreMask::Pipeline|RestoreMask::Heaps|RestoreMask::HeapInvalidatedTables;
    NativeRecordingState s;
    assert(s.RegisterLayout(root,{{RootKind::Table},{RootKind::Constants,2}}));
    assert(s.Begin(list,1,pso));assert(s.ObserveDescriptorHeaps(1,1,first));
    assert(s.ObserveComputeRootSignature(1,root));
    UINT constants[]={17,23};assert(s.ObserveComputeConstants(1,1,2,constants,0));
    // Initial/root-invalidated state is not proof of a heap-invalidated table.
    assert(!s.Capture(list,1,mask));
    assert(s.ObserveDescriptorHeaps(1,1,first)); // redundant, not an invalidating call
    assert(!s.Capture(list,1,mask));
    assert(s.ObserveDescriptorHeaps(1,1,second));
    // A genuine heap change proves undefined state even without a prior handle.
    auto neverSet=s.Capture(list,1,mask);assert(neverSet);
    // Public admission behavior proves undefined state and its heap-only provenance.
    assert(!s.Capture(list,1,RestoreMask::Compute));
    assert(!s.Capture(list,1,RestoreMask::Compute|RestoreMask::Pipeline|RestoreMask::Heaps|RestoreMask::RootInvalidatedTables));
    assert(!s.Capture(list,1,RestoreMask::All)); // default remains strict
    std::vector<std::string> neverSetCalls;
    NativeRecordingState::OriginalSetters neverSetOriginal;
    neverSetOriginal.heaps=[&](auto*,UINT count,auto* const* heaps){
        assert(heaps!=nullptr && (count==0 || (count==1&&heaps[0]==second[0])));
        neverSetCalls.push_back(count ? "heaps" : "invalidate");
    };
    neverSetOriginal.pipeline=[&](auto*,auto* value){assert(value==pso);neverSetCalls.push_back("pipeline");};
    neverSetOriginal.computeSignature=[&](auto*,auto* value){assert(value==root);neverSetCalls.push_back("root");};
    neverSetOriginal.computeConstants=[&](auto*,UINT i,UINT n,const void* p,UINT offset){
        assert(i==1&&n==2&&offset==0&&static_cast<const UINT*>(p)[0]==17&&static_cast<const UINT*>(p)[1]==23);
        neverSetCalls.push_back("constants");
    };
    // No table setter exists: restoration must not invent or replay a handle.
    assert(s.CaptureRestorable(list,1,mask,neverSetOriginal));
    assert(s.ObserveComputeTable(1,0,{456})); // inserted work supplies a new value
    assert(s.ObserveComputeConstants(1,1,1,constants+1,0));
    assert(s.Restore(*neverSet,neverSetOriginal));
    assert((neverSetCalls==std::vector<std::string>{"invalidate","heaps","pipeline","root","constants"}));
    assert(!s.Capture(list,1,RestoreMask::Compute)); // authentic undefined state restored
    assert(s.Capture(list,1,mask));
    assert(s.ObserveComputeTable(1,0,{123}));
    assert(s.ObserveDescriptorHeaps(1,1,first));
    assert(!s.Capture(list,1,RestoreMask::All)); // default remains strict
    auto saved=s.Capture(list,1,mask);assert(saved);
    std::vector<std::string> calls;
    NativeRecordingState::OriginalSetters o;
    o.heaps=[&](auto*,UINT count,auto* const* heaps){
        calls.push_back(count ? "heaps" : "invalidate");
        assert(heaps!=nullptr && (count==0 || (count==1&&heaps[0]==first[0])));
    };
    o.pipeline=[&](auto*,auto* value){assert(value==pso);calls.push_back("pipeline");};
    o.computeSignature=[&](auto*,auto* value){assert(value==root);calls.push_back("root");};
    o.computeConstants=[&](auto*,UINT i,UINT n,const void* p,UINT offset){
        assert(i==1&&n==2&&offset==0&&static_cast<const UINT*>(p)[0]==17&&static_cast<const UINT*>(p)[1]==23);
        calls.push_back("constants");};
    o.computeTable=[&](auto*,UINT,auto){calls.push_back("STALE_TABLE");};
    assert(s.CaptureRestorable(list,1,mask,o));
    auto incomplete=o;incomplete.heaps={};assert(!s.CaptureRestorable(list,1,mask,incomplete));
    auto withoutStaleSetter=o;withoutStaleSetter.computeTable={};
    assert(s.CaptureRestorable(list,1,mask,withoutStaleSetter));
    assert(s.ObserveComputeTable(1,0,{456})); // inserted work fills previously invalid slot
    assert(s.ObserveComputeConstants(1,1,1,constants+1,0));
    assert(s.Restore(*saved,o));
    assert((calls==std::vector<std::string>{"invalidate","heaps","pipeline","root","constants"}));
    assert(!s.Capture(list,1,RestoreMask::Compute)); // restored undefined table stays undefined
    assert(s.Capture(list,1,mask));
    assert(!s.Capture(list,1,RestoreMask::Compute|RestoreMask::HeapInvalidatedTables));
    s.Taint(1);assert(!s.Capture(list,1,mask));
    assert(s.Begin(list,2,pso));assert(!s.Restore(*saved,o));
    // Production-style compute-only restoration must also restore valid graphics
    // tables invalidated by its heap reset, without changing graphics root state.
    assert(s.ObserveDescriptorHeaps(2,1,first));
    assert(s.ObserveComputeRootSignature(2,root));
    assert(s.ObserveGraphicsRootSignature(2,root));
    assert(s.ObserveComputeConstants(2,1,2,constants,0));
    assert(s.ObserveComputeTable(2,0,{111}));
    assert(s.ObserveGraphicsTable(2,0,{222}));
    assert(s.ObserveDescriptorHeaps(2,1,second));
    assert(s.ObserveDescriptorHeaps(2,1,first));
    assert(s.ObserveGraphicsTable(2,0,{333}));
    unsigned graphicsCalls=0;
    o.graphicsTable=[&](auto*,UINT i,auto table){assert(i==0&&table.ptr==333);++graphicsCalls;};
    auto mixed=s.CaptureRestorable(list,2,mask,o);assert(mixed);
    assert(s.ObserveDescriptorHeaps(2,1,second));
    assert(s.Restore(*mixed,o));assert(graphicsCalls==1);
    // Original setter failure after the unbind must taint, never certify return.
    auto failure=s.CaptureRestorable(list,2,mask,o);assert(failure);
    o.pipeline=[](auto*,auto*){throw std::runtime_error("restore failure after unbind");};
    assert(!s.Restore(*failure,o));assert(!s.Capture(list,2,mask));
    assert(s.Begin(list,3,pso));assert(s.ObserveDescriptorHeaps(3,1,first));
    assert(s.ObserveComputeRootSignature(3,root));assert(s.ObserveComputeTable(3,0,{111}));
    assert(s.ObserveComputeConstants(3,1,1,constants,0));
    assert(s.ObserveDescriptorHeaps(3,1,second));
    NativeStateCaptureDiagnostic diagnostic;
    assert(!s.Capture(list,3,mask,&diagnostic));
    assert(std::string(diagnostic.reason)=="Native.State.ComputeConstants" &&
        diagnostic.parameter==1 && diagnostic.known==1 && diagnostic.required==2);
}

static int AuthorityRegressions()
{
    CaptureTraceRegressions();
    HeapInvalidationRestoreChecks();
    ReferenceProbe objects[12];
    int failures = 0;
    const auto check = [&](bool passed, const char* name) {
        ++checks;
        std::cout << (passed ? "PASS " : "FAIL ") << name << '\n';
        if (!passed) ++failures;
    };
    auto* list = objects[0].As<ID3D12GraphicsCommandList>();
    auto* pso = objects[1].As<ID3D12PipelineState>();
    auto* resource = objects[2].As<ID3D12Resource>();
    for (auto flag : {D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY, D3D12_RESOURCE_BARRIER_FLAG_END_ONLY})
    {
        NativeRecordingState state;
        assert(state.Begin(list, 1, pso));
        assert(state.WatchResource(1, resource, 1, 0, D3D12_RESOURCE_STATE_COPY_DEST));
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = flag;
        barrier.Transition = {resource, 0, D3D12_RESOURCE_STATE_COPY_DEST,
                              D3D12_RESOURCE_STATE_UNORDERED_ACCESS};
        check(!state.ObserveBarriers(1, 1, &barrier) && !state.Capture(list, 1, RestoreMask::Pipeline),
              flag == D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY ? "split begin cannot certify final state"
                                                           : "unpaired split end cannot certify final state");
    }
    {
        NativeRecordingState state;
        assert(state.Begin(list, 1, pso));
        assert(state.WatchResource(1, resource, 1, 0, D3D12_RESOURCE_STATE_COPY_DEST));
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
        check(!state.ObserveBarriers(1, 1, &barrier) && !state.Capture(list, 1, RestoreMask::Pipeline),
              "wildcard aliasing invalidates watched resource authority");
    }
    {
        NativeRecordingObserver observer;
        assert(observer.Reset(list, pso));
        observer.SetHookCoverage(true);
        check(!observer.Observe(list).completeCoverage, "late coverage cannot certify an existing epoch");
        assert(observer.Reset(list, pso));
        check(observer.Observe(list).completeCoverage, "new observed Reset captures current coverage");
        observer.SetHookCoverage(false);
        observer.SetHookCoverage(true);
        check(!observer.Observe(list).completeCoverage, "coverage interruption requires another Reset");
    }
    {
        NativeRecordingState state;
        assert(state.Begin(list, 1, pso));
        assert(state.ObserveDescriptorHeaps(1, 0, nullptr));
        auto saved = state.Capture(list, 1, RestoreMask::Heaps);
        assert(saved);
        auto* laterPso = objects[3].As<ID3D12PipelineState>();
        assert(state.ObservePipeline(1, laterPso));
        NativeRecordingState::OriginalSetters setters;
        setters.heaps = [](auto*, UINT, auto* const*) {};
        assert(state.Restore(*saved, setters));
        auto current = state.Capture(list, 1, RestoreMask::Pipeline);
        assert(current);
        ID3D12PipelineState* restored = nullptr;
        setters.pipeline = [&](auto*, auto* value) { restored = value; };
        assert(state.Restore(*current, setters));
        check(restored == laterPso, "partial restore preserves state outside its mask");
    }
    {
        NativeRecordingState state;
        assert(state.Begin(list, 1, pso));
        auto saved = state.Capture(list, 1, RestoreMask::Pipeline);
        assert(saved);
        assert(state.WatchResource(1, resource, 1, 0, D3D12_RESOURCE_STATE_COPY_DEST));
        unsigned calls = 0;
        NativeRecordingState::OriginalSetters setters;
        setters.pipeline = [&](auto*, auto*) { ++calls; };
        check(!state.Restore(*saved, setters) && calls == 0 &&
                  !state.Capture(list, 1, RestoreMask::Pipeline),
              "resource watch set changed after capture refuses before restoration");
    }
    {
        ReferenceProbe listOwner, pipelineOwner;
        std::optional<NativeRecordingState::Snapshot> snapshot;
        {
            NativeRecordingState state;
            assert(state.Begin(listOwner.As<ID3D12GraphicsCommandList>(), 1,
                               pipelineOwner.As<ID3D12PipelineState>()));
            snapshot = state.Capture(listOwner.As<ID3D12GraphicsCommandList>(), 1, RestoreMask::Pipeline);
            assert(snapshot);
        }
        check(listOwner.references == 2 && pipelineOwner.references == 2,
              "snapshot owns native list and pipeline after journal destruction");
        snapshot.reset();
        check(listOwner.references == 1 && pipelineOwner.references == 1,
              "snapshot releases its retained native objects");
    }
    {
        NativeRecordingState state;
        auto* signature = objects[4].As<ID3D12RootSignature>();
        auto* replacement = objects[5].As<ID3D12RootSignature>();
        auto* heap = objects[6].As<ID3D12DescriptorHeap>();
        assert(state.RegisterLayout(signature, {{RootKind::Table, 0}}));
        assert(state.RegisterLayout(replacement, {}));
        assert(state.Begin(list, 1, pso));
        assert(state.ObserveDescriptorHeaps(1, 1, &heap));
        assert(state.ObserveComputeRootSignature(1, signature));
        assert(state.ObserveComputeTable(1, 0, D3D12_GPU_DESCRIPTOR_HANDLE{100}));
        auto saved = state.Capture(list, 1, RestoreMask::Heaps);
        assert(saved);
        assert(state.ObserveComputeRootSignature(1, replacement));
        unsigned calls = 0;
        NativeRecordingState::OriginalSetters setters;
        setters.heaps = [&](auto*, UINT, auto* const*) { ++calls; };
        setters.computeTable = [&](auto*, UINT, auto) { ++calls; };
        check(!state.Restore(*saved, setters) && calls == 0,
              "heap-only restore cannot replay tables against a changed root signature");
    }
    return failures;
}

static void NativeIdentityChecks()
{
    ComPtr<IDXGIFactory4> factory;
    Ok(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
    ComPtr<IDXGIAdapter> warp;
    Ok(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)));
    ComPtr<ID3D12Device> device;
    Ok(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
    ComPtr<ID3D12CommandAllocator> allocator;
    Ok(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
    ComPtr<ID3D12GraphicsCommandList> list;
    Ok(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list)));
    NativeRecordingObserver observer;
    assert(!observer.Observe(list.Get()).trackingBeganBeforeRecording);
    Ok(list->Close());
    Ok(list->Reset(allocator.Get(), nullptr));
    // Exercise the exact adapter invoked by the production successful-Reset
    // detour. This does not claim that an actual detour was installed here.
    assert(observer.ResetNative(list.Get(), nullptr));
    const auto observed = observer.Observe(list.Get());
    assert(observed.identity && observed.identity->nativeList.Get() == list.Get());
    assert(observed.identity->interfaceVersion == 0);
    assert(observed.identity->interfaceIid == __uuidof(ID3D12GraphicsCommandList));
    assert(observed.identity->type == D3D12_COMMAND_LIST_TYPE_DIRECT);
    ComPtr<IUnknown> expectedList, expectedDevice;
    Ok(list.As(&expectedList));
    Ok(device.As(&expectedDevice));
    assert(observed.identity->comIdentity.Get() == expectedList.Get());
    assert(observed.identity->deviceIdentity.Get() == expectedDevice.Get());
    assert(observed.trackingBeganBeforeRecording && observed.missingRequiredHistory);
    assert(observed.hookGeneration != 0 && !observed.completeCoverage);
    Ok(list->Close());
    observer.Close(list.Get());
    assert(!observer.Observe(list.Get()).active);
    list.Reset();
    // Retaining a recording observation also retains its exact COM identities.
    assert(observed.identity->nativeList->GetType() == D3D12_COMMAND_LIST_TYPE_DIRECT);
}

int main(int argc, char** argv)
{
    DepthBiasStateRegressions();
    DepthBiasGpuRegressions();
    const int authorityFailures = AuthorityRegressions();
    if (authorityFailures || (argc > 1 && std::string(argv[1]) == "--authority-regressions"))
        return authorityFailures;
    NativeIdentityChecks();
    ReferenceProbe objects[12];
    NativeRecordingObserver observer;
    auto* observedList = objects[7].As<ID3D12GraphicsCommandList>();
    auto* observedPso = objects[8].As<ID3D12PipelineState>();
    assert(!observer.Observe(observedList).completeCoverage);
    assert(observer.Reset(observedList, observedPso));
    auto firstRecording = observer.Observe(observedList);
    assert(firstRecording.nativeList == observedList && firstRecording.incarnation != 0);
    assert(firstRecording.workOrdinal == 0 && !firstRecording.completeCoverage);
    assert(observer.Work(observedList));
    assert(observer.Observe(observedList).workOrdinal == 1);
    assert(observer.Reset(observedList, observedPso));
    auto nextRecording = observer.Observe(observedList);
    assert(nextRecording.incarnation > firstRecording.incarnation && nextRecording.workOrdinal == 0);
    assert(!observer.WithCurrent(observedList, [](auto&, auto) -> bool {
        throw std::runtime_error("observation failed");
    }));
    assert(!observer.Observe(observedList).active);
    assert(observer.Reset(observedList, observedPso));
    observer.Close(observedList);
    assert(!observer.Observe(observedList).active);

    NativeRecordingState journal;
    auto* list = objects[0].As<ID3D12GraphicsCommandList>();
    auto* compute = objects[1].As<ID3D12RootSignature>();
    auto* graphics = objects[2].As<ID3D12RootSignature>();
    auto* pso = objects[3].As<ID3D12PipelineState>();
    auto* heap = objects[4].As<ID3D12DescriptorHeap>();
    assert(journal.RegisterLayout(compute, {{RootKind::Table, 0}, {RootKind::Constants, 3}, {RootKind::CBV, 0}}));
    assert(journal.RegisterLayout(graphics, {{RootKind::Table, 0}, {RootKind::Constants, 2}, {RootKind::SRV, 0}}));
    assert(journal.Begin(list, 7, pso));
    // A receipt must see actual recorded work on this incarnation, and Reset must retire it.
    assert(journal.Activity(list, 7) == 0);
    assert(journal.ObserveWork(7));
    assert(journal.Activity(list, 7) == 1);
    assert(journal.Activity(list, 8) == std::nullopt);
    assert(journal.ObserveDescriptorHeaps(7, 1, &heap));
    assert(journal.ObserveComputeRootSignature(7, compute));
    assert(journal.ObserveGraphicsRootSignature(7, graphics));
    assert(journal.ObserveComputeTable(7, 0, D3D12_GPU_DESCRIPTOR_HANDLE{100}));
    assert(journal.ObserveGraphicsTable(7, 0, D3D12_GPU_DESCRIPTOR_HANDLE{200}));
    const std::uint32_t c[] = {11, 22, 33};
    assert(journal.ObserveComputeConstants(7, 1, 1, c, 0));
    const std::uint32_t g[] = {44, 55};
    assert(journal.ObserveGraphicsConstants(7, 1, 2, g, 0));
    assert(journal.ObserveComputeDescriptor(7, 2, RootKind::CBV, 1234));
    assert(journal.ObserveGraphicsDescriptor(7, 2, RootKind::SRV, 5678));
    auto* resource = objects[6].As<ID3D12Resource>();
    assert(journal.WatchResource(7, resource, 23, 0, D3D12_RESOURCE_STATE_COPY_DEST));
    assert(!journal.Capture(list, 7, RestoreMask::All)); // partial constant write is unknown
    NativeStateCaptureDiagnostic diagnostic;
    assert(!journal.Capture(list, 7, RestoreMask::All, &diagnostic));
    assert(std::string(diagnostic.reason) == "Native.State.ComputeConstants" && diagnostic.parameter == 1 &&
           diagnostic.known == 1 && diagnostic.required == 3);
    assert(journal.ObserveComputeRootSignature(7, compute));
    assert(!journal.Capture(list, 7, RestoreMask::All, &diagnostic));
    assert(diagnostic.known == 1 && diagnostic.required == 3); // redundant set never fills unknown values
    assert(journal.ObserveComputeConstants(7, 1, 2, c + 1, 1));
    auto snap = journal.Capture(list, 7, RestoreMask::All);
    assert(snap);
    assert(journal.ObserveComputeRootSignature(7, compute));
    assert(journal.ObserveGraphicsRootSignature(7, graphics));
    assert(journal.Capture(list, 7, RestoreMask::All)); // redundant root setters retain arguments
    assert(journal.ObserveDescriptorHeaps(7, 1, &heap));
    assert(journal.Capture(list, 7, RestoreMask::All)); // redundant heap setter retains tables
    assert(!journal.Capture(list, 8, RestoreMask::All));
    auto noSetter = NativeRecordingState::OriginalSetters{};
    auto* otherHeap = objects[5].As<ID3D12DescriptorHeap>();
    assert(journal.ObserveDescriptorHeaps(7, 1, &otherHeap));
    assert(journal.ObserveTransition(7, resource, 23, 0, D3D12_RESOURCE_STATE_COPY_DEST,
                                     D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
    assert(!journal.Capture(list, 7, RestoreMask::All)); // both tables now invalid
    assert(!journal.Capture(list, 7, RestoreMask::All, &diagnostic));
    assert(std::string(diagnostic.reason) == "Native.State.ComputeTable" && diagnostic.parameter == 0);
    assert(journal.ObserveDescriptorHeaps(7, 1, &otherHeap));
    assert(!journal.Capture(list, 7, RestoreMask::All)); // same heap cannot revive invalidated tables
    std::vector<std::string> calls;
    NativeRecordingState::OriginalSetters o;
    o.heaps = [&](auto*, UINT n, ID3D12DescriptorHeap* const* h) {
        assert(n == 1 && h[0] == heap); calls.push_back("heaps");
    };
    o.pipeline = [&](auto*, auto* p) { assert(p == pso); calls.push_back("pipeline"); };
    o.computeSignature = [&](auto*, auto* p) { assert(p == compute); calls.push_back("compute-signature"); };
    o.graphicsSignature = [&](auto*, auto* p) { assert(p == graphics); calls.push_back("graphics-signature"); };
    o.computeTable = [&](auto*, UINT i, D3D12_GPU_DESCRIPTOR_HANDLE h) {
        assert(i == 0 && h.ptr == 100); calls.push_back("compute-table");
    };
    o.graphicsTable = [&](auto*, UINT i, D3D12_GPU_DESCRIPTOR_HANDLE h) {
        assert(i == 0 && h.ptr == 200); calls.push_back("graphics-table");
    };
    o.computeConstants = [&](auto*, UINT i, UINT n, const void* data, UINT offset) {
        auto* values = static_cast<const UINT*>(data);
        assert(i == 1 && n == 3 && offset == 0 && values[0] == 11 && values[1] == 22 && values[2] == 33);
        calls.push_back("compute-constants");
    };
    o.graphicsConstants = [&](auto*, UINT i, UINT n, const void* data, UINT offset) {
        auto* values = static_cast<const UINT*>(data);
        assert(i == 1 && n == 2 && offset == 0 && values[0] == 44 && values[1] == 55);
        calls.push_back("graphics-constants");
    };
    o.computeCbv = [&](auto*, UINT i, D3D12_GPU_VIRTUAL_ADDRESS a) {
        assert(i == 2 && a == 1234); calls.push_back("compute-cbv");
    };
    o.graphicsSrv = [&](auto*, UINT i, D3D12_GPU_VIRTUAL_ADDRESS a) {
        assert(i == 2 && a == 5678); calls.push_back("graphics-srv");
    };
    o.barriers = [&](auto*, UINT n, const D3D12_RESOURCE_BARRIER* b) {
        assert(n == 1 && b->Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION);
        assert(b->Transition.pResource == resource && b->Transition.Subresource == 0);
        assert(b->Transition.StateBefore == D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        assert(b->Transition.StateAfter == D3D12_RESOURCE_STATE_COPY_DEST);
        calls.push_back("barrier");
    };
    o.computeSrv = [](auto*, UINT, D3D12_GPU_VIRTUAL_ADDRESS) {};
    o.computeUav = [](auto*, UINT, D3D12_GPU_VIRTUAL_ADDRESS) {};
    o.graphicsCbv = [](auto*, UINT, D3D12_GPU_VIRTUAL_ADDRESS) {};
    o.graphicsUav = [](auto*, UINT, D3D12_GPU_VIRTUAL_ADDRESS) {};
    // Missing originals must be refused at the post-SR capture boundary.
    assert(!journal.CaptureRestorable(list, 7, RestoreMask::All, noSetter));
    assert(journal.CaptureRestorable(list, 7, RestoreMask::Pipeline, o));
    assert(journal.ObserveWork(7));
    assert(journal.Restore(*snap, o));
    assert(journal.Activity(list, 7) == 2); // restore must not rewind recorded activity
    assert(calls.size() == 11);
    assert(calls.front() == "heaps" && calls.back() == "barrier");
    assert(journal.Capture(list, 7, RestoreMask::All));
    calls.clear();
    o.graphicsSrv = {}; // preflight refuses before any setter, then taints
    assert(!journal.Restore(*snap, o));
    assert(calls.empty());
    assert(!journal.Capture(list, 7, RestoreMask::All)); // taint survives a failed restore
    assert(journal.Begin(list, 8, pso));
    assert(journal.Activity(list, 8) == 0);
    assert(!journal.Restore(*snap, noSetter));
    assert(!journal.Capture(list, 8, RestoreMask::All));
    ReferenceProbe rejectedListOwner;
    assert(!journal.Begin(rejectedListOwner.As<ID3D12GraphicsCommandList>(), 9, pso));
    auto exceptionSnap = journal.Capture(list, 8, RestoreMask::Pipeline);
    assert(exceptionSnap);
    NativeRecordingState::OriginalSetters throwing;
    throwing.pipeline = [&](auto*, auto*) { throw std::runtime_error("failed setter"); };
    assert(!journal.Restore(*exceptionSnap, throwing));
    assert(!journal.Capture(list, 8, RestoreMask::Pipeline));
    NativeRecordingState resourceJournal;
    assert(resourceJournal.Begin(list, 1, pso));
    assert(resourceJournal.WatchResource(1, resource, 23, 0, D3D12_RESOURCE_STATE_COPY_DEST));
    auto resourceSnap = resourceJournal.Capture(list, 1, RestoreMask::Pipeline); assert(resourceSnap);
    D3D12_RESOURCE_BARRIER observedBarrier{};
    observedBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    observedBarrier.Transition = {resource, 0, D3D12_RESOURCE_STATE_COPY_DEST,
                                  D3D12_RESOURCE_STATE_UNORDERED_ACCESS};
    assert(resourceJournal.ObserveBarriers(1, 1, &observedBarrier));
    NativeRecordingState::OriginalSetters pipelineOnly;
    pipelineOnly.pipeline = [](auto*, auto*) {};
    assert(!resourceJournal.Restore(*resourceSnap, pipelineOnly));
    assert(!resourceJournal.Capture(list, 1, RestoreMask::Pipeline));
    const auto control = WarpOracle(false);
    const auto restored = WarpOracle(true);
    const auto restoredThroughAdapter = WarpOracle(true, true);
    assert(control == WarpOracle(true, false, true));
    assert(control == WarpOracle(true, true, true));
    assert(control == restored);
    assert(control == restoredThroughAdapter);
    assert(control[0] == 65 && control[1] == 128 && control[2] == 0 && control[3] == 255);
    std::cout << "native recording state checks=" << checks << " failed=0\n";
}
