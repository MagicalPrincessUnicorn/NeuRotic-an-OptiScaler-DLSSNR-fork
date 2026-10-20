#include <nr/d3d12/NativeRecordingState.h>
#if __has_include(<nr/d3d12/NativeRecordingHooks.h>)
#define NATIVE_RECORDING_HOOK_TEST_ACCESS
#include <nr/d3d12/NativeRecordingHooks.h>
#include <nr/d3d12/NativeRecordingReplay.h>
#define HAS_PRODUCTION_HOOKS 1
#endif
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include <d3d12sdklayers.h>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <functional>
using namespace Neurotic::D3D12;
using Microsoft::WRL::ComPtr;
namespace Neurotic::D3D12
{
struct NativeRecordingHooksTestAccess
{
    static unsigned SlotCount(const NativeRecordingHooks& hooks) { return hooks.count_; }
    static bool FailedAttachWithSuspendedDiagnosticReader(NativeRecordingHooks& hooks, ID3D12GraphicsCommandList* list)
    {
        HANDLE start = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        HANDLE held = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        HANDLE release = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        std::thread reader([&] {
            WaitForSingleObject(start, INFINITE);
            std::lock_guard lock(hooks.diagnosticsMutex_);
            SetEvent(held);
            WaitForSingleObject(release, INFINITE);
        });
        hooks.beforeTransactionForTest_ = [&] { SetEvent(start); WaitForSingleObject(held, INFINITE); };
        // This reader was enumerated and suspended while owning the diagnostics
        // mutex. It can release that mutex only after transaction abort resumes it.
        hooks.afterSuspendForTest_ = [&] { SetEvent(release); };
        hooks.failAttachForTest_ = true;
        const auto result = hooks.Install(list);
        SetEvent(release); reader.join();
        hooks.beforeTransactionForTest_ = {}; hooks.afterSuspendForTest_ = {}; hooks.failAttachForTest_ = false;
        CloseHandle(start); CloseHandle(held); CloseHandle(release);
        return result;
    }
    static void SaturateDiagnostics(NativeRecordingHooks& hooks)
    {
        // Synthetic addresses are diagnostic keys only, never COM objects.
        for (std::uintptr_t i = 1; i <= 80; ++i)
            hooks.Record(reinterpret_cast<ID3D12GraphicsCommandList*>(i), [](auto& d, auto) { ++d.closeCalls; });
    }
    static void ObserveUnsupportedOnly(NativeRecordingHooks& hooks, ID3D12GraphicsCommandList* list)
    {
        // Exercise the real observer helper without issuing an invalid command
        // to a closed native command list.
        hooks.Before<65>(list, UINT{0});
    }
    static void ObserveDepthBiasOnly(NativeRecordingHooks& hooks, ID3D12GraphicsCommandList* list)
    {
        // Product observation only, not a v9 API/hardware execution claim.
        hooks.Before<82>(list,1.5f,0.75f,-2.25f);
    }
    static void ObserveStateObjectOnly(NativeRecordingHooks& hooks,ID3D12GraphicsCommandList* list,ID3D12StateObject* object)
    {hooks.Before<75>(list,object);}
    static void ObservePipelineOnly(NativeRecordingHooks& hooks,ID3D12GraphicsCommandList* list,ID3D12PipelineState* object)
    {hooks.Before<25>(list,object);}
    using BiasFn=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList9*,float,float,float);
    static BiasFn ExchangeBiasOriginal(BiasFn replacement)
    {
        auto& original=NativeRecordingHooks::Hook<82,decltype(&ID3D12GraphicsCommandList10::RSSetDepthBias)>::original;
        auto prior=original;original=replacement;return prior;
    }
    static bool KnownBiasForwarding(NativeRecordingHooks& hooks,ID3D12GraphicsCommandList* caller,ID3D12GraphicsCommandList* callee)
    {
        std::lock_guard lock(hooks.forwardingMutex_);
        return std::any_of(hooks.forwarding_.begin(),hooks.forwarding_.end(),[&](const auto& route){
            return route->caller.Get()==caller&&route->callee.Get()==callee&&route->slot==82&&
                route->generation==hooks.forwardingGeneration_.load();
        });
    }
    static void ReplayObservedStateObject(NativeRecordingHooks& hooks,ID3D12GraphicsCommandList* list,ID3D12StateObject* object)
    {
        NativeRecordingHooks::CallbackLease lease(hooks);
        NativeRecordingHooks::Replay<75>([&](auto* target,auto* value){hooks.Before<75>(target,value);},list,object);
    }
    static void ObserveNullPipelineOnly(NativeRecordingHooks& hooks, ID3D12GraphicsCommandList* list)
    {
        // Exercise an independent observer rejection without sending an invalid
        // SetPipelineState(nullptr) command to the native command list.
        hooks.Before<25>(list, static_cast<ID3D12PipelineState*>(nullptr));
    }
    static void ObserveTimestampOnly(NativeRecordingHooks& hooks, ID3D12GraphicsCommandList* list,
        ID3D12QueryHeap* heap, D3D12_QUERY_TYPE type, UINT index)
    { hooks.Before<53>(list, heap, type, index); }
    static void ObserveQueryBeginOnly(NativeRecordingHooks& hooks, ID3D12GraphicsCommandList* list,
        ID3D12QueryHeap* heap, D3D12_QUERY_TYPE type, UINT index)
    { hooks.Before<52>(list, heap, type, index); }
    static void ObserveQueryResolveOnly(NativeRecordingHooks& hooks, ID3D12GraphicsCommandList* list,
        ID3D12QueryHeap* heap, D3D12_QUERY_TYPE type, UINT first, UINT count)
    { hooks.Before<54>(list,heap,type,first,count,static_cast<ID3D12Resource*>(nullptr),UINT64{0}); }
    template<class F> static void Replay(NativeRecordingHooks& hooks, ID3D12GraphicsCommandList* list, F action)
    {
        NativeRecordingHooks::CallbackLease lease(hooks);
        NativeRecordingHooks::Replay<28>(action, list);
    }
};
}
static NativeRecordingObserver& TestObserver() { static auto* value = new NativeRecordingObserver; return *value; }
static NativeRecordingHooks& TestHooks() { static auto* value = new NativeRecordingHooks(TestObserver()); return *value; }
using LegacyPipelineFn = void (STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12PipelineState*);
static LegacyPipelineFn legacyPipelineOriginal;
static unsigned legacyPipelineCalls;
__declspec(noinline) static void STDMETHODCALLTYPE LegacyPipeline(ID3D12GraphicsCommandList* list, ID3D12PipelineState* pipeline)
{
    ++legacyPipelineCalls;
    legacyPipelineOriginal(list,pipeline);
}
static unsigned checks;
using QueryFn = HRESULT (STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,REFIID,void**);
static QueryFn nativeQuery;
static HRESULT STDMETHODCALLTYPE BaseOnlyQuery(ID3D12GraphicsCommandList* list,REFIID iid,void** object)
{
    if(iid == __uuidof(IUnknown) || iid == __uuidof(ID3D12Object) || iid == __uuidof(ID3D12DeviceChild) ||
       iid == __uuidof(ID3D12CommandList) || iid == __uuidof(ID3D12GraphicsCommandList)) return nativeQuery(list,iid,object);
    *object=nullptr;return E_NOINTERFACE;
}
static void Check(bool b, const char* text) { ++checks; if (!b) throw std::runtime_error(text); }
#define assert(value) Check(!!(value), #value)
#define NATIVE_INSTALLED_HOOK_TEST
#include "../vertical-slice/native_recording_warp_scene.h"

using RouteTypeFn = D3D12_COMMAND_LIST_TYPE (STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*);
static RouteTypeFn routeOriginalGetType;
static std::function<bool()> routeTypeCallback;
static D3D12_COMMAND_LIST_TYPE STDMETHODCALLTYPE RouteType(ID3D12GraphicsCommandList* list)
{
 auto callback=std::move(routeTypeCallback);
 if(callback && !callback()) routeTypeCallback=std::move(callback);
 return routeOriginalGetType(list);
}
#include "native_depth_bias_hooks.h"
int main() {
try {
 ComPtr<ID3D12Debug> debug; Ok(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))); debug->EnableDebugLayer();
 ComPtr<IDXGIFactory4> factory; ComPtr<IDXGIAdapter> warp; ComPtr<ID3D12Device> device;
 Ok(CreateDXGIFactory1(IID_PPV_ARGS(&factory))); Ok(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)));
 Ok(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
 ComPtr<ID3D12CommandAllocator> allocator; ComPtr<ID3D12GraphicsCommandList> list;
 Ok(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
 Ok(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list)));
 auto& observer = TestObserver();
#ifdef HAS_PRODUCTION_HOOKS
 auto& hooks = TestHooks();
 const auto unseen = hooks.Diagnostic(list.Get());
 Check(unseen.queriedList == reinterpret_cast<std::uint64_t>(list.Get()) && unseen.queriedVtable != 0 &&
       unseen.installAttempts == 0 && unseen.resetCalls == 0 && !(unseen.flags & 4), "unseen diagnostic has requested identity without creating authority");
 legacyPipelineOriginal = reinterpret_cast<LegacyPipelineFn>((*reinterpret_cast<void***>(list.Get()))[25]);
 Check(DetourTransactionBegin() == NO_ERROR && DetourUpdateThread(GetCurrentThread()) == NO_ERROR, "legacy chain begin");
 Check(DetourAttach(reinterpret_cast<PVOID*>(&legacyPipelineOriginal),reinterpret_cast<PVOID>(LegacyPipeline)) == NO_ERROR, "legacy chain attach");
 Check(DetourTransactionCommit() == NO_ERROR, "legacy chain commit");
 Check(DetourTransactionBegin() == NO_ERROR, "controlled busy transaction");
 Check(!hooks.Install(list.Get()), "failed installation transaction cannot grant hooks");
 const auto failedInstall = hooks.Diagnostic(list.Get());
 Check(failedInstall.firstRefusal == NativeRecordingHookRefusal::TransactionBegin && failedInstall.firstResult != NO_ERROR,
       "installation preserves raw transaction failure");
 Check(DetourTransactionAbort() == NO_ERROR, "abort controlled busy transaction");
 Check(!observer.Observe(list.Get()).completeCoverage, "failed installation remains refused");
 ComPtr<ID3D12CommandAllocator> installFailureAllocator; ComPtr<ID3D12GraphicsCommandList> installFailureList;
 Ok(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&installFailureAllocator)));
 Ok(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,installFailureAllocator.Get(),nullptr,IID_PPV_ARGS(&installFailureList)));
 Check(!NativeRecordingHooksTestAccess::FailedAttachWithSuspendedDiagnosticReader(hooks, installFailureList.Get()),
       "attach failure resumes diagnostic mutex holder before publishing refusal");
 Check(hooks.Diagnostic(installFailureList.Get()).firstRefusal == NativeRecordingHookRefusal::Attach &&
       hooks.Diagnostic(installFailureList.Get()).firstResult != NO_ERROR, "actual attach failure preserves raw result after abort");
 Ok(installFailureList->Close());
 Check(hooks.Install(list.Get()), "checked hook installation");
#endif
 Check(!observer.Observe(list.Get()).completeCoverage, "installation cannot certify missed Reset");
 Ok(list->Close()); Ok(list->Reset(allocator.Get(), nullptr));
 Check(observer.Observe(list.Get()).completeCoverage, "production Reset must grant installed coverage");
 std::vector<ComPtr<ID3D12GraphicsCommandList>> rejectedTables;
 {
     ComPtr<ID3D12CommandAllocator> cloneAllocator; ComPtr<ID3D12GraphicsCommandList> clone;
     Ok(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&cloneAllocator)));
     Ok(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,cloneAllocator.Get(),nullptr,IID_PPV_ARGS(&clone)));
     rejectedTables.push_back(clone);
     auto** table=*reinterpret_cast<void***>(clone.Get());
     std::vector<void*> copiedTable(table,table+NativeRecordingHooksTestAccess::SlotCount(hooks));
     struct RestoreTable { ID3D12GraphicsCommandList* list; void** table;
         ~RestoreTable(){*reinterpret_cast<void***>(list)=table;} } restoreTable{clone.Get(),table};
     *reinterpret_cast<void***>(clone.Get())=copiedTable.data();
     Check(hooks.Install(clone.Get()),"equivalent per-list vtable can use existing verified detours");
     Check(!observer.Observe(clone.Get()).completeCoverage,"equivalent table alone cannot certify missed Reset");
     Ok(clone->Close());Ok(clone->Reset(cloneAllocator.Get(),nullptr));
     Check(observer.Observe(clone.Get()).completeCoverage,"real Reset enrolls verified equivalent table");
     ID3D12DescriptorHeap* noHeaps[] = {nullptr};
     clone->SetDescriptorHeaps(0,noHeaps);
     auto snapshot=observer.CaptureRestorable(clone.Get(),RestoreMask::Heaps,hooks.Originals());
     Check(snapshot&&observer.Restore(clone.Get(),observer.Observe(clone.Get()).incarnation,*snapshot,hooks.Originals()),
         "equivalent table preserves actual setter restoration");
     const auto qualifiedVersion=observer.Observe(clone.Get()).identity->interfaceVersion;
     observer.ResetNative(clone.Get(),nullptr,qualifiedVersion);
     Check(!observer.Observe(clone.Get()).completeCoverage,"correct interface version without explicit table proof cannot manufacture coverage");
     Ok(clone->Close());
 }
 for(unsigned variant=0;variant<2;++variant)
 {
     ComPtr<ID3D12CommandAllocator> candidateAllocator; ComPtr<ID3D12GraphicsCommandList> candidate;
     Ok(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&candidateAllocator)));
     Ok(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,candidateAllocator.Get(),nullptr,IID_PPV_ARGS(&candidate)));
     rejectedTables.push_back(candidate);
     auto** table=*reinterpret_cast<void***>(candidate.Get());
     std::vector<void*> copied(table,table+NativeRecordingHooksTestAccess::SlotCount(hooks));
     struct RestoreTable { ID3D12GraphicsCommandList* list; void** table;
         ~RestoreTable(){*reinterpret_cast<void***>(list)=table;} } restoreTable{candidate.Get(),table};
     if(variant==0)copied[25]=copied[28];
     else { nativeQuery=reinterpret_cast<QueryFn>(copied[0]);copied.resize(60);copied[0]=reinterpret_cast<void*>(BaseOnlyQuery); }
     *reinterpret_cast<void***>(candidate.Get())=copied.data();
     Check(!hooks.Install(candidate.Get()),"different target or shorter shape cannot inherit installed coverage");
     const auto refused=hooks.Diagnostic(candidate.Get());
     Check(refused.firstRefusal==(variant==0?NativeRecordingHookRefusal::SlotMismatch:NativeRecordingHookRefusal::InterfaceShape),
         "shape rejection precedes extended reads and function rejection identifies changed slot");
     Ok(candidate->Close());
 }
 const auto resetDiagnostic = hooks.Diagnostic(list.Get());
 Check(resetDiagnostic.installedResetTarget == resetDiagnostic.queriedResetTarget && resetDiagnostic.resetPatchTarget != 0,
       "query reports current and installed Reset entry and actual patch target");
 Check(resetDiagnostic.resetCalls == 1 && resetDiagnostic.resetSuccesses == 1 && resetDiagnostic.enrollmentSuccesses == 1 &&
       resetDiagnostic.lastResetResult == S_OK && resetDiagnostic.firstRefusal == failedInstall.firstRefusal &&
       resetDiagnostic.firstRefusalSequence == failedInstall.firstRefusalSequence, "observed Reset enrolls without overwriting earlier refusal");
 Check(observer.WithCurrent(list.Get(), [&](auto&, auto) {
     NativeRecordingHooksTestAccess::Replay(hooks, list.Get(), [&](auto* value) {
         Check(hooks.Diagnostic(value).resetCalls == 1, "diagnostic query under observer lock and replay is read only");
     });
     return true;
 }), "diagnostic query does not deadlock observer");
 // These are valid native calls; the production observer lacks the first
 // signature's layout; only the later versioned effect loses coverage.
 ComPtr<ID3D12CommandAllocator> diagnosticAllocator, unsupportedAllocator, unobservedAllocator;
 ComPtr<ID3D12GraphicsCommandList> rejectedStateList, unsupportedList, unobservedList;
 Ok(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&diagnosticAllocator)));
 Ok(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&unsupportedAllocator)));
 Ok(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&unobservedAllocator)));
 Ok(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,diagnosticAllocator.Get(),nullptr,IID_PPV_ARGS(&rejectedStateList)));
 Ok(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,unsupportedAllocator.Get(),nullptr,IID_PPV_ARGS(&unsupportedList)));
 Ok(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,unobservedAllocator.Get(),nullptr,IID_PPV_ARGS(&unobservedList)));
 D3D12_ROOT_SIGNATURE_DESC unknownDesc{};
 ComPtr<ID3DBlob> unknownBlob, unknownErrors; ComPtr<ID3D12RootSignature> unknownRoot;
 Ok(D3D12SerializeRootSignature(&unknownDesc,D3D_ROOT_SIGNATURE_VERSION_1,&unknownBlob,&unknownErrors));
 Ok(device->CreateRootSignature(0,unknownBlob->GetBufferPointer(),unknownBlob->GetBufferSize(),IID_PPV_ARGS(&unknownRoot)));
 unobservedList->SetComputeRootSignature(unknownRoot.Get());
 ComPtr<ID3D12GraphicsCommandList1> unobservedVersion1; Ok(unobservedList.As(&unobservedVersion1));
 unobservedVersion1->SetViewInstanceMask(0);
 Check(hooks.Diagnostic(unobservedList.Get()).firstRefusal == NativeRecordingHookRefusal::None &&
       !observer.Observe(unobservedList.Get()).incarnation,
       "unseen list does not invent a state observation rejection or unsupported effect");
 Ok(unobservedList->Close());
 Ok(rejectedStateList->Close()); Ok(rejectedStateList->Reset(diagnosticAllocator.Get(),nullptr));
 Check(observer.Observe(rejectedStateList.Get()).completeCoverage, "state diagnostic control has observed Reset");
 rejectedStateList->SetDescriptorHeaps(0,nullptr);
 Check(hooks.Diagnostic(rejectedStateList.Get()).firstRefusal == NativeRecordingHookRefusal::None &&
       observer.Observe(rejectedStateList.Get()).completeCoverage, "accepted observation does not publish refusal");
 rejectedStateList->SetComputeRootSignature(unknownRoot.Get());
 NativeStateCaptureDiagnostic unknownCapture;
 const auto unknownCaptured = observer.CaptureRestorable(rejectedStateList.Get(), RestoreMask::Compute, {}, &unknownCapture);
 Check(hooks.Diagnostic(rejectedStateList.Get()).firstRefusal == NativeRecordingHookRefusal::None &&
       observer.Observe(rejectedStateList.Get()).completeCoverage &&
       !unknownCaptured && std::strcmp(unknownCapture.reason, "Native.State.ComputeSignatureUnknown") == 0,
       "unregistered native root remains observed but cannot authenticate a compute snapshot");
 NativeRecordingHooksTestAccess::ObserveNullPipelineOnly(hooks, rejectedStateList.Get());
 const auto stateRefusal = hooks.Diagnostic(rejectedStateList.Get());
 Check(static_cast<std::uint32_t>(stateRefusal.firstRefusal) == 30 && stateRefusal.failedSlot == 25 &&
       stateRefusal.firstRefusalSequence != 0 && stateRefusal.expectedIdentity == 0 &&
       stateRefusal.observedIdentity == 0 && !observer.Observe(rejectedStateList.Get()).completeCoverage,
       "independent null pipeline observer rejection records first cause and exact setter slot");
 ComPtr<ID3D12GraphicsCommandList1> rejectedVersion1; Ok(rejectedStateList.As(&rejectedVersion1));
 rejectedVersion1->SetViewInstanceMask(0);
 const auto retainedStateRefusal = hooks.Diagnostic(rejectedStateList.Get());
 Check(retainedStateRefusal.firstRefusal == stateRefusal.firstRefusal && retainedStateRefusal.failedSlot == 25 &&
       retainedStateRefusal.firstRefusalSequence == stateRefusal.firstRefusalSequence,
       "later unsupported effect cannot replace first state observation rejection");
 Ok(rejectedStateList->Close());
 Ok(unsupportedList->Close()); Ok(unsupportedList->Reset(unsupportedAllocator.Get(),nullptr));
 ComPtr<ID3D12GraphicsCommandList1> unsupportedVersion1; Ok(unsupportedList.As(&unsupportedVersion1));
 Check(observer.Observe(unsupportedList.Get()).completeCoverage, "unsupported diagnostic control has observed Reset");
 unsupportedVersion1->SetViewInstanceMask(0);
 const auto unsupportedRefusal = hooks.Diagnostic(unsupportedList.Get());
 Check(static_cast<std::uint32_t>(unsupportedRefusal.firstRefusal) == 29 && unsupportedRefusal.failedSlot == 65 &&
       unsupportedRefusal.firstRefusalSequence != 0 && !observer.Observe(unsupportedList.Get()).completeCoverage,
       "actual versioned effect records first unsupported slot while coverage remains refused");
 Ok(unsupportedList->Close()); Ok(unsupportedList->Reset(unsupportedAllocator.Get(),nullptr));
 Check(observer.Observe(unsupportedList.Get()).completeCoverage &&
       hooks.Diagnostic(unsupportedList.Get()).firstRefusalSequence == unsupportedRefusal.firstRefusalSequence,
       "diagnostic history does not deny fresh recording authority or erase the first refusal");
 Ok(unsupportedList->Close());
 // No method may claim to be the first coverage loss after the recording has
 // already become inactive or tainted through another owner operation.
 Ok(unobservedList->Reset(unobservedAllocator.Get(),nullptr));
 Check(observer.WithCurrent(unobservedList.Get(), [](auto& state, auto incarnation) {
     state.Close(incarnation); return true;
 }), "inactive observation control retains its entry");
 NativeRecordingHooksTestAccess::ObserveUnsupportedOnly(hooks, unobservedList.Get());
 Check(hooks.Diagnostic(unobservedList.Get()).firstRefusal == NativeRecordingHookRefusal::None &&
       !observer.Observe(unobservedList.Get()).completeCoverage,
       "inactive recording does not attribute existing coverage loss to a later unsupported slot");
 Ok(unobservedList->Close());
 NativeRecordingHooksTestAccess::ObserveUnsupportedOnly(hooks, unobservedList.Get());
 Check(hooks.Diagnostic(unobservedList.Get()).firstRefusal == NativeRecordingHookRefusal::None,
       "closed native list with no observer entry does not invent an unsupported effect");
 Ok(unobservedList->Reset(unobservedAllocator.Get(),nullptr));
 Check(observer.WithCurrent(unobservedList.Get(), [](auto& state, auto incarnation) {
     state.Taint(incarnation); return true;
 }), "tainted observation control retains its entry");
 unobservedList->SetComputeRootSignature(unknownRoot.Get());
 Check(hooks.Diagnostic(unobservedList.Get()).firstRefusal == NativeRecordingHookRefusal::None &&
       !observer.Observe(unobservedList.Get()).completeCoverage,
       "already tainted recording does not attribute existing coverage loss to a later unknown-root setter");
 Ok(unobservedList->Close());
 ComPtr<ID3D12GraphicsCommandList> qiList;
 ComPtr<ID3D12CommandAllocator> qiAllocator;
 Ok(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&qiAllocator)));
 Ok(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,qiAllocator.Get(),nullptr,IID_PPV_ARGS(&qiList)));
 const auto beforeQuery = hooks.Diagnostic(qiList.Get());
 Check(beforeQuery.resetCalls == 0 && !observer.Observe(qiList.Get()).incarnation, "fresh list is not enrolled by diagnostics");
 ComPtr<ID3D12DebugCommandList> qiDebug; const auto qiResult = qiList.As(&qiDebug); Ok(qiResult);
 const auto qiDiagnostic = hooks.Diagnostic(qiList.Get());
 Check(qiDiagnostic.firstRefusal == NativeRecordingHookRefusal::UnknownInterface && qiDiagnostic.firstResult == qiResult &&
       qiDiagnostic.failedIid == __uuidof(ID3D12DebugCommandList) && qiDiagnostic.failedSlot == 0 &&
       qiDiagnostic.expectedIdentity == reinterpret_cast<std::uint64_t>(qiList.Get()) &&
       qiDiagnostic.observedIdentity == reinterpret_cast<std::uint64_t>(qiDebug.Get()), "unknown successful QI retains raw IID result and identities before any entry exists");
 Ok(qiList->Close()); Ok(qiList->Reset(qiAllocator.Get(), nullptr));
 const auto rejectedReset = hooks.Diagnostic(qiList.Get());
 Check(rejectedReset.resetCalls == 1 && rejectedReset.resetSuccesses == 1 && rejectedReset.enrollmentSuccesses == 0 &&
       rejectedReset.firstRefusalSequence == qiDiagnostic.firstRefusalSequence && !observer.Observe(qiList.Get()).incarnation,
       "successful Reset on rejected interface remains absent with exact first reason");
 Ok(qiList->Close());
 ComPtr<ID3D12GraphicsCommandList> failedResetList;
 Ok(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,qiAllocator.Get(),nullptr,IID_PPV_ARGS(&failedResetList)));
 const auto realFailedReset = failedResetList->Reset(qiAllocator.Get(), nullptr);
 const auto failedResetDiagnostic = hooks.Diagnostic(failedResetList.Get());
 Check(FAILED(realFailedReset) && failedResetDiagnostic.firstRefusal == NativeRecordingHookRefusal::ResetFailed &&
       failedResetDiagnostic.firstResult == realFailedReset && failedResetDiagnostic.lastResetResult == realFailedReset &&
       failedResetDiagnostic.resetCalls == 1 && failedResetDiagnostic.resetSuccesses == 0 && failedResetDiagnostic.enrollmentSuccesses == 0,
       "real failed Reset records HRESULT and never grants enrollment");
 Ok(failedResetList->Close());
 // This negative API call intentionally emits one debug-layer error. Do not
 // attribute that expected message to the later valid GPU oracle recordings.
 ComPtr<ID3D12InfoQueue> expectedErrors; Ok(device.As(&expectedErrors)); expectedErrors->ClearStoredMessages();
#ifdef HAS_PRODUCTION_HOOKS
 // Repeated installation must not revoke an already observed recording.
 ID3D12DescriptorHeap* emptyHeaps[] = {nullptr};
 const auto generation = observer.Observe(list.Get()).hookGeneration;
 Check(hooks.Install(list.Get()) && observer.Observe(list.Get()).hookGeneration == generation, "idempotent install");
 list->SetDescriptorHeaps(0, emptyHeaps);
 const auto originals = hooks.Originals();
 Check(!observer.CaptureRestorable(list.Get(), RestoreMask::Pipeline, originals), "null initial PSO is unknown");
 auto savedHeaps = observer.CaptureRestorable(list.Get(), RestoreMask::Heaps, originals);
 Check(savedHeaps.has_value(), "fresh compute-domain heaps snapshot");
 D3D12_VIEWPORT viewport{0,0,1,1,0,1}; list->RSSetViewports(1, &viewport);
 Check(!observer.Restore(list.Get(), observer.Observe(list.Get()).incarnation, *savedHeaps, originals), "unrestorable viewport mutation refuses");
 Ok(list->Close()); Ok(list->Reset(allocator.Get(), nullptr));
 list->SetDescriptorHeaps(0, emptyHeaps);
 D3D12_ROOT_PARAMETER parameter{}; parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
 parameter.Constants.Num32BitValues = 1;
 D3D12_ROOT_SIGNATURE_DESC desc{}; desc.NumParameters = 1; desc.pParameters = &parameter;
 ComPtr<ID3DBlob> blob, errors; ComPtr<ID3D12RootSignature> root;
 Ok(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors));
 Ok(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root)));
 Check(hooks.RegisterRootSignature(root.Get(), blob->GetBufferPointer(), blob->GetBufferSize(), D3D12CreateVersionedRootSignatureDeserializer), "actual root blob registration");
 list->SetComputeRootSignature(root.Get()); list->SetComputeRoot32BitConstant(0, 7, 0);
 list->SetGraphicsRootSignature(root.Get()); list->SetGraphicsRoot32BitConstant(0, 7, 0);
 list->SetComputeRootSignature(root.Get()); list->SetGraphicsRootSignature(root.Get());
 Check(observer.CaptureRestorable(list.Get(), RestoreMask::Compute | RestoreMask::Graphics, originals).has_value(),
       "real redundant signature calls preserve observed root constants at both bind points");
 // The renderer's existing GPU timer records timestamps inside the restore
 // envelope. These are work, with no open query scope or persistent binding.
 D3D12_QUERY_HEAP_DESC timestampDesc{};timestampDesc.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;timestampDesc.Count=2;
 ComPtr<ID3D12QueryHeap> timestamps;Ok(device->CreateQueryHeap(&timestampDesc,IID_PPV_ARGS(&timestamps)));
 D3D12_HEAP_PROPERTIES readbackHeap{};readbackHeap.Type=D3D12_HEAP_TYPE_READBACK;
 auto readbackDesc=Buffer(16);ComPtr<ID3D12Resource> timestampReadback;
 Ok(device->CreateCommittedResource(&readbackHeap,D3D12_HEAP_FLAG_NONE,&readbackDesc,
     D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&timestampReadback)));
 auto timestampState=observer.CaptureRestorable(list.Get(),RestoreMask::Compute|RestoreMask::Graphics,originals);
 const auto beforeTimestamp=observer.Observe(list.Get());
 list->EndQuery(timestamps.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0);
 Check(observer.Observe(list.Get()).completeCoverage&&observer.Observe(list.Get()).workOrdinal>beforeTimestamp.workOrdinal,
       "actual timestamp EndQuery retains coverage and records work");
 list->SetComputeRoot32BitConstant(0,19,0);
 list->EndQuery(timestamps.Get(),D3D12_QUERY_TYPE_TIMESTAMP,1);
 list->ResolveQueryData(timestamps.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0,2,timestampReadback.Get(),0);
 Check(timestampState&&observer.Restore(list.Get(),beforeTimestamp.incarnation,*timestampState,originals),
       "timestamp pair and real resolve preserve caller state restoration");
 for(unsigned invalid=0;invalid<2;++invalid)
 {
     // Invalid inputs exercise the actual hook classifier without forwarding
     // an invalid native command to the debug layer.
     NativeRecordingHooksTestAccess::ObserveTimestampOnly(hooks,list.Get(),invalid==0?nullptr:timestamps.Get(),
         invalid==1?D3D12_QUERY_TYPE_OCCLUSION:D3D12_QUERY_TYPE_TIMESTAMP,0u);
     Check(!observer.Observe(list.Get()).completeCoverage,"invalid or scoped query cannot inherit timestamp coverage");
     Ok(list->Close());Ok(list->Reset(allocator.Get(),nullptr));
 }
 D3D12_QUERY_HEAP_DESC occlusionDesc{};occlusionDesc.Type=D3D12_QUERY_HEAP_TYPE_OCCLUSION;occlusionDesc.Count=1;
 ComPtr<ID3D12QueryHeap> occlusion;Ok(device->CreateQueryHeap(&occlusionDesc,IID_PPV_ARGS(&occlusion)));
 list->SetDescriptorHeaps(0,emptyHeaps);
 auto beforeScope=observer.CaptureRestorable(list.Get(),RestoreMask::Heaps,originals);
 const auto scopeIncarnation=observer.Observe(list.Get()).incarnation;
 list->BeginQuery(occlusion.Get(),D3D12_QUERY_TYPE_OCCLUSION,0);
 Check(observer.Observe(list.Get()).completeCoverage,"open query preserves observed binding history");
 NativeStateCaptureDiagnostic openQuery;
 Check(!observer.CaptureRestorable(list.Get(),RestoreMask::Heaps,originals,&openQuery)&&
       std::string(openQuery.reason)=="Native.State.QueryOpen","open query blocks capture with exact reason");
 Check(beforeScope&&!observer.Restore(list.Get(),scopeIncarnation,*beforeScope,originals),"open query blocks restore");
 list->EndQuery(occlusion.Get(),D3D12_QUERY_TYPE_OCCLUSION,0);
 Check(observer.Observe(list.Get()).completeCoverage&&
       observer.CaptureRestorable(list.Get(),RestoreMask::Heaps,originals).has_value(),
       "completed query permits later capture without resetting binding history");
 Check(observer.Restore(list.Get(),scopeIncarnation,*beforeScope,originals),"balanced query does not prevent later binding restoration");
 for(unsigned invalid=0;invalid<4;++invalid)
 {
     Ok(list->Close());Ok(list->Reset(allocator.Get(),nullptr));
     NativeRecordingHooksTestAccess::ObserveQueryBeginOnly(hooks,list.Get(),occlusion.Get(),
         invalid==0?D3D12_QUERY_TYPE_TIMESTAMP:invalid==1?static_cast<D3D12_QUERY_TYPE>(99):D3D12_QUERY_TYPE_OCCLUSION,0);
     if(invalid==2)NativeRecordingHooksTestAccess::ObserveQueryBeginOnly(hooks,list.Get(),occlusion.Get(),D3D12_QUERY_TYPE_BINARY_OCCLUSION,0);
     if(invalid==3)NativeRecordingHooksTestAccess::ObserveTimestampOnly(hooks,list.Get(),occlusion.Get(),D3D12_QUERY_TYPE_BINARY_OCCLUSION,0);
     Check(!observer.Observe(list.Get()).completeCoverage,"invalid begin, duplicate element and mismatched end fail closed");
 }
 for(unsigned variant=0;variant<4;++variant)
 {
     Ok(list->Close());Ok(list->Reset(allocator.Get(),nullptr));
     NativeRecordingHooksTestAccess::ObserveQueryBeginOnly(hooks,list.Get(),occlusion.Get(),D3D12_QUERY_TYPE_OCCLUSION,0);
     NativeRecordingHooksTestAccess::ObserveQueryBeginOnly(hooks,list.Get(),occlusion.Get(),D3D12_QUERY_TYPE_OCCLUSION,1);
     NativeRecordingHooksTestAccess::ObserveTimestampOnly(hooks,list.Get(),occlusion.Get(),D3D12_QUERY_TYPE_OCCLUSION,0);
     NativeRecordingHooksTestAccess::ObserveQueryResolveOnly(hooks,list.Get(),occlusion.Get(),
         variant==1?static_cast<D3D12_QUERY_TYPE>(99):D3D12_QUERY_TYPE_OCCLUSION,
         variant==2?UINT_MAX:variant==3?0:1,variant==2?2:1);
     if(variant==3) {
         Check(observer.Observe(list.Get()).completeCoverage,"resolve of closed nonoverlapping element preserves open scope");
         Check(!observer.CaptureRestorable(list.Get(),RestoreMask::Heaps,originals),"ending one query does not clear another scope");
         NativeRecordingHooksTestAccess::ObserveTimestampOnly(hooks,list.Get(),occlusion.Get(),D3D12_QUERY_TYPE_OCCLUSION,1);
         list->SetDescriptorHeaps(0,emptyHeaps);
         Check(observer.CaptureRestorable(list.Get(),RestoreMask::Heaps,originals).has_value(),"all matched ends recover capture");
     } else Check(!observer.Observe(list.Get()).completeCoverage,"open resolve, unknown type and overflow fail closed");
 }
 Ok(list->Close());Ok(list->Reset(allocator.Get(),nullptr));
 list->SetDescriptorHeaps(0,emptyHeaps);list->SetComputeRootSignature(root.Get());list->SetComputeRoot32BitConstant(0,7,0);
 list->SetGraphicsRootSignature(root.Get());list->SetGraphicsRoot32BitConstant(0,7,0);
 auto partial = observer.CaptureRestorable(list.Get(), RestoreMask::Compute | RestoreMask::Heaps, originals);
 Check(partial.has_value(), "partial snapshot with known unchanged graphics");
 list->SetGraphicsRootSignature(root.Get()); list->SetGraphicsRoot32BitConstant(0, 1, 0);
 list->SetGraphicsRoot32BitConstant(0, 7, 0);
 Check(!observer.Restore(list.Get(), observer.Observe(list.Get()).incarnation, *partial, originals), "unmasked graphics mutation refuses even set back");
 Ok(list->Close()); Ok(list->Reset(allocator.Get(), nullptr));
 // Versioned operations remain observable but explicitly unsupported for restoration.
 ComPtr<ID3D12GraphicsCommandList1> version1; Ok(list.As(&version1));
 version1->SetViewInstanceMask(0);
 Check(!observer.Observe(list.Get()).completeCoverage, "versioned effect taints recording");
 Ok(list->Close()); Ok(list->Reset(allocator.Get(), nullptr));
 ComPtr<ID3D12CommandAllocator> bundleAllocator; ComPtr<ID3D12GraphicsCommandList> bundle;
 Ok(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_BUNDLE, IID_PPV_ARGS(&bundleAllocator)));
 Ok(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_BUNDLE,bundleAllocator.Get(),nullptr,IID_PPV_ARGS(&bundle)));
 Ok(bundle->Close()); list->ExecuteBundle(bundle.Get());
 Check(!observer.Observe(list.Get()).completeCoverage, "ExecuteBundle taints recording");
 Ok(list->Close()); Ok(list->Reset(allocator.Get(), nullptr));
 ComPtr<ID3D12CommandAllocator> computeAllocator; ComPtr<ID3D12GraphicsCommandList> compute;
 Ok(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&computeAllocator)));
 Ok(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_COMPUTE,computeAllocator.Get(),nullptr,IID_PPV_ARGS(&compute)));
 Ok(compute->Close()); Ok(compute->Reset(computeAllocator.Get(),nullptr));
 Check(!observer.Observe(compute.Get()).completeCoverage && !hooks.Install(compute.Get()), "other command-list type cannot inherit coverage");
 Ok(compute->Close());
 // Missed signature creation conservatively invalidates root arguments, even at zero count.
 auto shader = Compile("[numthreads(1,1,1)] void main(){}", "cs_5_0");
 D3D12_COMPUTE_PIPELINE_STATE_DESC pipelineDesc{}; pipelineDesc.pRootSignature = root.Get();
 pipelineDesc.CS = {shader->GetBufferPointer(),shader->GetBufferSize()};
 ComPtr<ID3D12PipelineState> pipeline; Ok(device->CreateComputePipelineState(&pipelineDesc,IID_PPV_ARGS(&pipeline)));
 list->SetPipelineState(pipeline.Get()); list->SetComputeRootSignature(root.Get()); list->SetComputeRoot32BitConstant(0,7,0);
 {
     ComPtr<ID3D12CommandAllocator> biasAllocator;
     ComPtr<ID3D12GraphicsCommandList> biasList;
     Ok(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&biasAllocator)));
     Ok(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,biasAllocator.Get(),pipeline.Get(),IID_PPV_ARGS(&biasList)));
     Ok(biasList->Close());Ok(biasList->Reset(biasAllocator.Get(),pipeline.Get()));
     Check(observer.Observe(biasList.Get()).completeCoverage,"fresh bias observer recording begins covered");
     NativeRecordingHooksTestAccess::ObserveDepthBiasOnly(hooks,biasList.Get());
     const bool covered=observer.Observe(biasList.Get()).completeCoverage;
     std::cout<<"DEPTH_BIAS_OBSERVER slot=82 covered="<<covered<<" actualAPIInvoked=0\n";
     Check(covered,"observed slot82 must retain coverage instead of unconditional unsupported-effect taint");
     // Do not attempt replay/GPU use of this observer-only input. A fresh actual
     // Reset clears its recording before the old full fixture continues.
     Ok(biasList->Close());Ok(biasList->Reset(biasAllocator.Get(),pipeline.Get()));Ok(biasList->Close());
 }

 // Keep these closed fixture identities live through the older diagnostic
 // tests, whose intentionally retained first-refusal records use raw addresses.
 const auto depthFixtureOwners=DepthBiasInstalledRegressions(device.Get(),hooks,observer);
 (void)depthFixtureOwners;

 D3D12_INDIRECT_ARGUMENT_DESC argument{}; argument.Type=D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
 D3D12_COMMAND_SIGNATURE_DESC command{}; command.ByteStride=12; command.NumArgumentDescs=1; command.pArgumentDescs=&argument;
 ComPtr<ID3D12CommandSignature> signature; Ok(device->CreateCommandSignature(&command,nullptr,IID_PPV_ARGS(&signature)));
 D3D12_HEAP_PROPERTIES upload{}; upload.Type=D3D12_HEAP_TYPE_UPLOAD; auto argumentDesc=Buffer(12);
 ComPtr<ID3D12Resource> argumentBuffer; Ok(device->CreateCommittedResource(&upload,D3D12_HEAP_FLAG_NONE,&argumentDesc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&argumentBuffer)));
 Check(!NativeIndirectSignatures::Read(signature.Get(),list.Get()),"direct creation is not observed signature provenance");
 const auto beforeIndirect=observer.Observe(list.Get());
 auto savedIndirect=observer.CaptureRestorable(list.Get(),RestoreMask::Compute|RestoreMask::Pipeline,originals);
 Check(savedIndirect.has_value(),"known compute arguments before unobserved indirect signature");
 list->ExecuteIndirect(signature.Get(),0,argumentBuffer.Get(),0,nullptr,0);
 const auto afterIndirect=observer.Observe(list.Get());NativeStateCaptureDiagnostic indirectDiagnostic;
 Check(afterIndirect.completeCoverage&&afterIndirect.workOrdinal>beforeIndirect.workOrdinal&&
       !observer.CaptureRestorable(list.Get(),RestoreMask::Compute,originals,&indirectDiagnostic)&&
       std::string(indirectDiagnostic.reason)=="Native.State.ComputeConstants"&&
       indirectDiagnostic.parameter==0&&indirectDiagnostic.known==0&&indirectDiagnostic.required==1,
       "unobserved indirect retains coverage but refuses unestablished root constants");
 list->SetComputeRoot32BitConstant(0,7,0);
 Check(observer.CaptureRestorable(list.Get(),RestoreMask::Compute|RestoreMask::Pipeline,originals).has_value(),
       "actual explicit setter reestablishes current constants after unknown indirect effects");
 NativeRecordingState::RestoreDiagnostic indirectRestore;
 Check(!observer.Restore(list.Get(),beforeIndirect.incarnation,*savedIndirect,originals,&indirectRestore)&&
       std::string(indirectRestore.reason)=="Native.Restore.MutationBoundary",
       "snapshot cannot cross indirect work even after apparent values are set back");
 Ok(list->Close()); Ok(list->Reset(allocator.Get(),nullptr));
 // An actual successful unrecognized interface invalidates the route persistently.
 ComPtr<ID3D12DebugCommandList> debugList; Ok(list.As(&debugList));
 Check(!observer.Observe(list.Get()).completeCoverage, "successful unknown QI refuses");
 const auto priorRefusal = hooks.Diagnostic(list.Get());
 Check(priorRefusal.firstRefusal == failedInstall.firstRefusal && priorRefusal.queryInterfaceCalls > 0,
       "later QI rejection retains first installation rejection");
 Ok(list->Close()); Ok(list->Reset(allocator.Get(),nullptr));
 Check(!observer.Observe(list.Get()).completeCoverage, "unknown interface cannot be repaired by Reset");
 hooks.Revoke(); Check(!observer.Observe(list.Get()).completeCoverage, "authority revocation: checked hook removal");
 Check(!observer.Observe(list.Get()).completeCoverage, "detach revokes prior recording");
 Ok(list->Close());
 ComPtr<ID3D12GraphicsCommandList> replacement;
 Ok(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&replacement)));
 Check(hooks.Install(replacement.Get()), "checked retained installation regrant");
 Ok(replacement->Close()); Ok(replacement->Reset(allocator.Get(), nullptr));
 Check(observer.Observe(replacement.Get()).completeCoverage && observer.Observe(replacement.Get()).hookGeneration != generation, "new observed Reset under changed generation");
 const auto routeEpoch = observer.Observe(replacement.Get()).incarnation;
 // Swap only this controlled object's vtable pointer, without issuing commands.
 auto** originalTable = *reinterpret_cast<void***>(replacement.Get());
 std::array<void*,86> otherTable{}; std::copy_n(originalTable,60,otherTable.data());
 *reinterpret_cast<void***>(replacement.Get()) = otherTable.data();
 const bool unknownRefused = !observer.Observe(replacement.Get()).completeCoverage;
 *reinterpret_cast<void***>(replacement.Get()) = originalTable;
 Check(unknownRefused && !observer.Observe(replacement.Get()).completeCoverage, "changed implementation revokes this recording until a genuine Reset");
 const auto routeDiagnostic = hooks.Diagnostic(replacement.Get());
 std::cout<<"ROUTE_DIAGNOSTIC list="<<std::hex<<reinterpret_cast<std::uintptr_t>(replacement.Get())
          <<" firstRefusal="<<static_cast<unsigned>(routeDiagnostic.firstRefusal)
          <<" expected="<<routeDiagnostic.expectedIdentity<<" observed="<<routeDiagnostic.observedIdentity<<std::dec<<'\n';
 Check(routeDiagnostic.firstRefusal == NativeRecordingHookRefusal::VtableMismatch &&
       routeDiagnostic.expectedIdentity == reinterpret_cast<std::uint64_t>(originalTable) &&
       routeDiagnostic.observedIdentity == reinterpret_cast<std::uint64_t>(otherTable.data()), "changed route records exact expected and observed identities");
 Ok(replacement->Close()); Ok(replacement->Reset(allocator.Get(),nullptr));
 const auto recoveredRoute = observer.Observe(replacement.Get());
 Check(recoveredRoute.completeCoverage && recoveredRoute.incarnation > routeEpoch,
       "genuine Reset with the restored route reenrolls a fresh recording");
 Ok(replacement->Close());
 // Admit an equivalent table on an unenrolled list, then retain its exact
 // identity for that recording. Mutate one uncalled slot in the SAME table.
 ComPtr<ID3D12GraphicsCommandList> installList;
 Ok(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&installList)));
 auto** installOriginal=*reinterpret_cast<void***>(installList.Get());
 std::array<void*,86> installTable{};
 std::copy_n(installOriginal,NativeRecordingHooksTestAccess::SlotCount(hooks),installTable.data());
 *reinterpret_cast<void***>(installList.Get())=installTable.data();
 if(!hooks.Install(installList.Get())) throw std::runtime_error("direct Install control prerequisite");
 Ok(installList->Close()); Ok(installList->Reset(allocator.Get(),nullptr));
 const auto installBefore=observer.Observe(installList.Get());
 Check(installBefore.completeCoverage && installBefore.identity && installBefore.identity->commandTable==installTable.data(),
       "unenrolled equivalent table is admitted by a genuine Reset");
 const auto installEpoch=installBefore.incarnation;
 installTable[28]=installOriginal[25]; // Never call the modified setter.
 const bool installRefused=!hooks.Install(installList.Get());
 installTable[28]=installOriginal[28];
 const auto installDiagnostic=hooks.Diagnostic(installList.Get());
 Check(installRefused && installDiagnostic.firstRefusal==NativeRecordingHookRefusal::SlotMismatch &&
       installDiagnostic.failedSlot==28 && !observer.Observe(installList.Get()).completeCoverage,
       "direct Install route loss survives restoration before Observe");
 Ok(installList->Close()); Ok(installList->Reset(allocator.Get(),nullptr));
 const auto installRecovered=observer.Observe(installList.Get());
 Check(installRecovered.completeCoverage && installRecovered.incarnation>installEpoch,
       "genuine Reset recovers direct Install route loss");
 Ok(installList->Close());*reinterpret_cast<void***>(installList.Get())=installOriginal;
 // A different but byte-equivalent table is still an observed change from
 // this enrolled epoch's identity, even when no Observe occurs until restored.
 ComPtr<ID3D12GraphicsCommandList> identityList;
 Ok(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&identityList)));
 if(!hooks.Install(identityList.Get())) throw std::runtime_error("table identity control prerequisite");
 Ok(identityList->Close());Ok(identityList->Reset(allocator.Get(),nullptr));
 const auto identityEpoch=observer.Observe(identityList.Get()).incarnation;
 auto** identityOriginal=*reinterpret_cast<void***>(identityList.Get());
 std::array<void*,86> identityTable{};
 std::copy_n(identityOriginal,NativeRecordingHooksTestAccess::SlotCount(hooks),identityTable.data());
 *reinterpret_cast<void***>(identityList.Get())=identityTable.data();
 const bool identityRefused=!hooks.Install(identityList.Get());
 *reinterpret_cast<void***>(identityList.Get())=identityOriginal;
 const auto identityDiagnostic=hooks.Diagnostic(identityList.Get());
 Check(identityRefused && identityDiagnostic.firstRefusal==NativeRecordingHookRefusal::VtableMismatch &&
       identityDiagnostic.expectedIdentity==reinterpret_cast<std::uint64_t>(identityOriginal) &&
       identityDiagnostic.observedIdentity==reinterpret_cast<std::uint64_t>(identityTable.data()) &&
       !observer.Observe(identityList.Get()).completeCoverage,
       "direct Install rejects witnessed equivalent-table replacement before Observe");
 Ok(identityList->Close());Ok(identityList->Reset(allocator.Get(),nullptr));
 const auto identityRecovered=observer.Observe(identityList.Get());
 Check(identityRecovered.completeCoverage && identityRecovered.incarnation>identityEpoch,
       "genuine Reset recovers witnessed equivalent-table replacement");
 Ok(identityList->Close());
 // Sample a changed enrolled route before its GetType callback can conceal
 // that observation by restoring the old table without a genuine Reset.
 ComPtr<ID3D12GraphicsCommandList> concealedList;
 Ok(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&concealedList)));
 if(!hooks.Install(concealedList.Get())) throw std::runtime_error("concealed route control prerequisite");
 Ok(concealedList->Close());Ok(concealedList->Reset(allocator.Get(),nullptr));
 const auto concealedEpoch=observer.Observe(concealedList.Get()).incarnation;
 auto** concealedOriginal=*reinterpret_cast<void***>(concealedList.Get());
 std::array<void*,86> concealedTable{};
 std::copy_n(concealedOriginal,NativeRecordingHooksTestAccess::SlotCount(hooks),concealedTable.data());
 routeOriginalGetType=reinterpret_cast<RouteTypeFn>(concealedOriginal[8]);
 concealedTable[8]=reinterpret_cast<void*>(&RouteType);
 routeTypeCallback=[&]{*reinterpret_cast<void***>(concealedList.Get())=concealedOriginal;return true;};
 *reinterpret_cast<void***>(concealedList.Get())=concealedTable.data();
 const bool concealedRefused=!hooks.Install(concealedList.Get());
 *reinterpret_cast<void***>(concealedList.Get())=concealedOriginal;routeTypeCallback={};
 const auto concealedObserved=observer.Observe(concealedList.Get());
 const auto concealedDiagnostic=hooks.Diagnostic(concealedList.Get());
 Check(concealedRefused && !concealedObserved.completeCoverage && concealedObserved.incarnation==concealedEpoch &&
       concealedDiagnostic.firstRefusal==NativeRecordingHookRefusal::VtableMismatch &&
       concealedDiagnostic.expectedIdentity==reinterpret_cast<std::uint64_t>(concealedOriginal) &&
       concealedDiagnostic.observedIdentity==reinterpret_cast<std::uint64_t>(concealedTable.data()),
       "callback restoration cannot hide sampled enrolled-table change");
 Ok(concealedList->Close());Ok(concealedList->Reset(allocator.Get(),nullptr));
 const auto concealedRecovered=observer.Observe(concealedList.Get());
 Check(concealedRecovered.completeCoverage && concealedRecovered.incarnation>concealedEpoch,
       "genuine Reset recovers callback-concealed route loss");
 Ok(concealedList->Close());
 // Foreign shape discovery can establish a newer genuine Reset. An older
 // validation result must not invalidate the newly qualified incarnation.
 ComPtr<ID3D12GraphicsCommandList> newerList;
 Ok(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&newerList)));
 auto** newerOriginal=*reinterpret_cast<void***>(newerList.Get());
 std::array<void*,86> newerTable{};
 std::copy_n(newerOriginal,NativeRecordingHooksTestAccess::SlotCount(hooks),newerTable.data());
 *reinterpret_cast<void***>(newerList.Get())=newerTable.data();
 if(!hooks.Install(newerList.Get())) throw std::runtime_error("newer Reset control prerequisite");
 Ok(newerList->Close()); Ok(newerList->Reset(allocator.Get(),nullptr));
 const auto newerEpoch=observer.Observe(newerList.Get()).incarnation;
 routeOriginalGetType=reinterpret_cast<RouteTypeFn>(newerOriginal[8]);
 newerTable[8]=reinterpret_cast<void*>(&RouteType);
 routeTypeCallback=[&]{*reinterpret_cast<void***>(newerList.Get())=newerOriginal;
     Ok(newerList->Close());Ok(newerList->Reset(allocator.Get(),nullptr));return true;};
 *reinterpret_cast<void***>(newerList.Get())=newerTable.data();
 const bool oldInstallRefused=!hooks.Install(newerList.Get());
 *reinterpret_cast<void***>(newerList.Get())=newerOriginal;routeTypeCallback={};
 const auto newerObserved=observer.Observe(newerList.Get());
 Check(oldInstallRefused && newerObserved.completeCoverage && newerObserved.incarnation>newerEpoch,
       "older direct Install failure preserves a newer genuine Reset");
 Ok(newerList->Close());
 // During real post-Reset discovery, a witnessed bad route must prevent that
 // pending enrollment from publishing complete history after route restoration.
 ComPtr<ID3D12GraphicsCommandList> pendingList;
 Ok(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&pendingList)));
 if(!hooks.Install(pendingList.Get())) throw std::runtime_error("pending Reset control prerequisite");
 Ok(pendingList->Close());Ok(pendingList->Reset(allocator.Get(),nullptr));
 const auto pendingEpoch=observer.Observe(pendingList.Get()).incarnation;
 Ok(pendingList->Close());
 auto** pendingOriginal=*reinterpret_cast<void***>(pendingList.Get());
 std::array<void*,86> pendingTable{};
 std::copy_n(pendingOriginal,NativeRecordingHooksTestAccess::SlotCount(hooks),pendingTable.data());
 routeOriginalGetType=reinterpret_cast<RouteTypeFn>(pendingOriginal[8]);
 pendingTable[8]=reinterpret_cast<void*>(&RouteType);bool pendingWitnessed=false,pendingRefused=false;
 routeTypeCallback=[&]{if(!observer.CaptureRouteHistory(pendingList.Get()).enrollment)return false;
     pendingWitnessed=true;pendingTable[28]=pendingOriginal[25];
     pendingRefused=!hooks.Install(pendingList.Get());
     pendingTable[28]=pendingOriginal[28];return true;};
 *reinterpret_cast<void***>(pendingList.Get())=pendingTable.data();
 Ok(pendingList->Reset(allocator.Get(),nullptr));
 routeTypeCallback={};
 Check(pendingWitnessed && pendingRefused && !observer.Observe(pendingList.Get()).completeCoverage,
       "direct Install route failure cancels matching pending Reset enrollment");
 *reinterpret_cast<void***>(pendingList.Get())=pendingOriginal;
 Ok(pendingList->Close());Ok(pendingList->Reset(allocator.Get(),nullptr));
 const auto pendingRecovered=observer.Observe(pendingList.Get());
 Check(pendingRecovered.completeCoverage && pendingRecovered.incarnation>pendingEpoch,
       "fresh genuine Reset recovers canceled route discovery");
 Ok(pendingList->Close());
 hooks.Revoke(); Check(!observer.Observe(list.Get()).completeCoverage, "authority revocation: final checked detach");
 ComPtr<ID3D12GraphicsCommandList> lifecycle;
 Ok(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&lifecycle)));
 Check(hooks.Install(lifecycle.Get()), "lifecycle control install");
 Ok(lifecycle->Close()); Ok(lifecycle->Reset(allocator.Get(),nullptr));
 bool replayRejected = false;
 try { NativeRecordingHooksTestAccess::Replay(hooks,lifecycle.Get(),[](auto* value) { Ok(value->Close()); }); }
 catch (const std::runtime_error&) { replayRejected = true; }
 Check(replayRejected && !observer.Observe(lifecycle.Get()).completeCoverage, "Close during original replay refuses without lock reentry");
 Check(hooks.Diagnostic(lifecycle.Get()).firstRefusal == NativeRecordingHookRefusal::ReplayLifecycle,
       "reentrant refusal diagnostics do not reenter observer lock");
 Ok(lifecycle->Reset(allocator.Get(),nullptr));
 Check(observer.Observe(lifecycle.Get()).completeCoverage, "fresh Reset recovers original replay Close failure");
 Ok(lifecycle->Close());
 ComPtr<ID3D12GraphicsCommandList> resetDuringReplay;
 Ok(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&resetDuringReplay)));
 Ok(resetDuringReplay->Close());
 replayRejected = false;
 try { NativeRecordingHooksTestAccess::Replay(hooks,resetDuringReplay.Get(),[&](auto* value) { Ok(value->Reset(allocator.Get(),nullptr)); }); }
 catch (const std::runtime_error&) { replayRejected = true; }
 Check(replayRejected && !observer.Observe(resetDuringReplay.Get()).completeCoverage, "Reset during original replay refuses without lock reentry");
 Ok(resetDuringReplay->Close());Ok(resetDuringReplay->Reset(allocator.Get(),nullptr));
 Check(observer.Observe(resetDuringReplay.Get()).completeCoverage, "fresh Reset recovers original replay Reset failure");
 Ok(resetDuringReplay->Close());
 hooks.Revoke(); Check(!observer.Observe(list.Get()).completeCoverage, "authority revocation: detach after replay rejection");
 ComPtr<ID3D12GraphicsCommandList> flight;
 Ok(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&flight)));
 Check(hooks.Install(flight.Get()), "in-flight control install");
 bool refusedDetach = false;
 NativeRecordingHooksTestAccess::Replay(hooks,flight.Get(),[&](auto*) { hooks.Revoke(); refusedDetach = !observer.Observe(flight.Get()).completeCoverage; });
 Check(refusedDetach && !observer.Observe(flight.Get()).completeCoverage, "in-flight replay revokes coverage while retaining executable paths");
 hooks.Revoke(); Check(!observer.Observe(list.Get()).completeCoverage, "authority revocation: detach after replay lease retires");
 Ok(flight->Close());
#endif
 const auto baseline = WarpOracle(false, false, false);
 const auto restored = WarpOracle(true, true, true);
 const auto computeRestored = WarpOracle(true, true, true, true);
 assert(baseline == WarpOracle(true, true, true, false, true));
 assert(baseline == WarpOracle(true, true, true, true, true));
 Check(computeRestored == baseline, "compute-only fresh Reset restoration GPU pixel oracle");
 Check(baseline == restored && baseline[0] == 65 && baseline[3] == 255, "installed compute and graphics restoration GPU pixel oracle");
 Check(legacyPipelineCalls > 0, "pre-existing legacy detour forwards through native observation and replay");
 NativeRecordingResetDiagnostic failedIdentity;
 Check(!observer.ResetNative(nullptr, nullptr, 0, &failedIdentity) && failedIdentity.stage == 1 && failedIdentity.result == E_POINTER,
       "enrollment preserves native resolution result and stage");
 const auto firstBeforeSaturation = hooks.Diagnostic(list.Get());
 NativeRecordingHooksTestAccess::SaturateDiagnostics(hooks);
 Check(hooks.Diagnostic(nullptr).droppedEvents > 0 && hooks.Diagnostic(list.Get()).firstRefusalSequence == firstBeforeSaturation.firstRefusalSequence,
       "bounded diagnostic saturation is explicit and never evicts first rejection");
 std::cout << "checks=" << checks << " PASS\n"; return 0;
} catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}

