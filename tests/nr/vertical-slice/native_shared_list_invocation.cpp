// Regression: custom NR bindings must never reach the original SR callback,
// including when the optional legacy restore preferences are false.
#include <nr/d3d12/NativeRecordingState.h>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

#if __has_include(<dlssnr/NativeSharedListInvocation.h>)
#include <dlssnr/NativeSharedListInvocation.h>
using Result = DlssNr::NativeSharedListResult;
template<class C, class I, class R> Result Run(C capture, I invoke, R restore)
{ return DlssNr::InvokeNativeSharedList(capture, invoke, restore); }
#else
// 05 extracts this exact scope from the baseline product TU before compiling.
// The only substituted dependencies model the documented false-flag no-op.
struct ScopedSkipHeapCapture {};
struct D3D12Hooks {
    static void SetRootSignatureTracking(bool) {}
    static void RestoreRoot(ID3D12GraphicsCommandList*) {} // both legacy flags false
};
#include "baseline_nr_envelope.h"
enum class Result { Skipped, Restored, Failed };
template<class C, class I, class R> Result Run(C, I invoke, R)
{
    try { ScopedNrStateEnvelope actualBaseline(nullptr); invoke(); }
    catch (...) { return Result::Failed; }
    return Result::Restored;
}
#endif

using namespace Neurotic::D3D12;
struct Ref final : IUnknown {
    ULONG refs = 1;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid != __uuidof(IUnknown)) return E_NOINTERFACE;
        *out = this; AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { return --refs; }
    template<class T> T* As() { return reinterpret_cast<T*>(this); }
};

struct Scene {
    Ref objects[7];
    ID3D12GraphicsCommandList* list = objects[0].As<ID3D12GraphicsCommandList>();
    ID3D12PipelineState* gamePso = objects[1].As<ID3D12PipelineState>();
    ID3D12RootSignature* gameRoot = objects[2].As<ID3D12RootSignature>();
    ID3D12DescriptorHeap* gameHeap = objects[3].As<ID3D12DescriptorHeap>();
    ID3D12PipelineState* nrPso = objects[4].As<ID3D12PipelineState>();
    ID3D12RootSignature* nrRoot = objects[5].As<ID3D12RootSignature>();
    ID3D12DescriptorHeap* nrHeap = objects[6].As<ID3D12DescriptorHeap>();
    NativeRecordingState journal;
    NativeRecordingState::OriginalSetters original;
    ID3D12PipelineState* livePso = gamePso;
    ID3D12RootSignature* liveRoot = gameRoot;
    ID3D12DescriptorHeap* liveHeap = gameHeap;
    UINT64 liveTable = 1234;
    UINT liveConstant = 42;
    ID3D12RootSignature* liveGraphicsRoot = gameRoot;
    UINT64 liveGraphicsTable = 4321;
    UINT liveGraphicsConstant = 84;
    bool graphics;
    unsigned nrCalls = 0, srCalls = 0;

    explicit Scene(bool complete = true, bool restoreGraphics = false) : graphics(restoreGraphics) {
        if (!journal.RegisterLayout(gameRoot, {{RootKind::Table}, {RootKind::Constants, 1}}) ||
            !journal.RegisterLayout(nrRoot, {{RootKind::Table}, {RootKind::Constants, 1}}) ||
            !journal.Begin(list, 1, gamePso) ||
            !journal.ObserveDescriptorHeaps(1, 1, &gameHeap) ||
            !journal.ObserveComputeRootSignature(1, gameRoot) ||
            !journal.ObserveComputeTable(1, 0, {liveTable}))
            throw std::runtime_error("fixture setup failed");
        if (complete && !journal.ObserveComputeConstants(1, 1, 1, &liveConstant, 0))
            throw std::runtime_error("fixture constants failed");
        if (graphics && (!journal.ObserveGraphicsRootSignature(1, gameRoot) ||
            !journal.ObserveGraphicsTable(1, 0, {liveGraphicsTable}) ||
            !journal.ObserveGraphicsConstants(1, 1, 1, &liveGraphicsConstant, 0)))
            throw std::runtime_error("fixture graphics failed");
        original.heaps = [&](auto*, UINT count, auto* heaps) { liveHeap = count ? heaps[0] : nullptr; };
        original.pipeline = [&](auto*, auto* pso) { livePso = pso; };
        original.computeSignature = [&](auto*, auto* root) { liveRoot = root; };
        original.computeTable = [&](auto*, UINT, auto table) { liveTable = table.ptr; };
        original.computeConstants = [&](auto*, UINT, UINT, const void* data, UINT) {
            liveConstant = *static_cast<const UINT*>(data);
        };
        original.graphicsSignature = [&](auto*, auto* root) { liveGraphicsRoot = root; };
        original.graphicsTable = [&](auto*, UINT, auto table) { liveGraphicsTable = table.ptr; };
        original.graphicsConstants = [&](auto*, UINT, UINT, const void* data, UINT) {
            liveGraphicsConstant = *static_cast<const UINT*>(data);
        };
    }
    auto Capture() {
        using M = RestoreMask;
        auto mask = M::Compute | M::Pipeline | M::Heaps | M::HeapInvalidatedTables;
        if (graphics) mask = mask | M::Graphics;
        return journal.CaptureRestorable(list, 1, mask, original);
    }
    void CustomNr(bool throwAfter = false) {
        ++nrCalls;
        livePso = nrPso; liveRoot = nrRoot; liveHeap = nrHeap;
        liveTable = 9876; liveConstant = 99;
        if (!journal.ObservePipeline(1, nrPso) || !journal.ObserveDescriptorHeaps(1, 1, &nrHeap) ||
            !journal.ObserveComputeRootSignature(1, nrRoot) ||
            !journal.ObserveComputeTable(1, 0, {liveTable}) ||
            !journal.ObserveComputeConstants(1, 1, 1, &liveConstant, 0))
            throw std::runtime_error("fixture mutation failed");
        if (throwAfter) throw std::runtime_error("NR invocation failed");
    }
    bool CallerBindings() const {
        return livePso == gamePso && liveRoot == gameRoot && liveHeap == gameHeap &&
            liveTable == 1234 && liveConstant == 42 && (!graphics ||
            (liveGraphicsRoot == gameRoot && liveGraphicsTable == 4321 && liveGraphicsConstant == 84));
    }
    bool OriginalSr(Result result) {
        if (result == Result::Failed) return true; // unsafe continuation is refused
        ++srCalls;
        return CallerBindings();
    }
};

int main() {
    unsigned checks = 0, failures = 0;
    const auto check = [&](bool ok, const char* label) {
        ++checks;
        if (!ok) { ++failures; std::cerr << "FAIL " << label << '\n'; }
    };
    {
        Scene s;
        auto result = Run([&] { return s.Capture(); }, [&] { s.CustomNr(); },
            [&](const auto& saved) { return s.journal.Restore(saved, s.original); });
        check(result == Result::Restored && s.OriginalSr(result) && s.nrCalls == 1 && s.srCalls == 1,
            "flags false: restore full caller bindings before original SR exactly once");
    }
    {
        Scene s(false);
        auto result = Run([&] { return s.Capture(); }, [&] { s.CustomNr(); },
            [&](const auto& saved) { return s.journal.Restore(saved, s.original); });
        check(result == Result::Skipped && s.nrCalls == 0 && s.OriginalSr(result) && s.srCalls == 1,
            "incomplete state: bypass NR without touching caller bindings, original SR once");
    }
    {
        Scene s;
        auto result = Run([&] { return s.Capture(); }, [&] {
            s.CustomNr();
            s.original.pipeline = [](auto*, auto*) { throw std::runtime_error("restore failed"); };
        }, [&](const auto& saved) { return s.journal.Restore(saved, s.original); });
        s.OriginalSr(result);
        check(result == Result::Failed && s.nrCalls == 1 && s.srCalls == 0 && !s.Capture(),
            "restore failure: no original SR continuation and recording remains tainted");
    }
    {
        Scene s;
        auto result = Run([&] { return s.Capture(); }, [&] { s.CustomNr(true); },
            [&](const auto& saved) { return s.journal.Restore(saved, s.original); });
        s.OriginalSr(result);
        check(result == Result::Failed && s.CallerBindings() && s.srCalls == 0,
            "invocation exception: restore bindings and refuse uncertain continuation");
    }
    {
        Scene s;
        auto result = Run([&] { return s.Capture(); }, [&] {
            s.livePso = s.nrPso;
            if (!s.journal.ObservePipeline(1, s.nrPso))
                throw std::runtime_error("preparation binding failed");
            return; // no simulated inference or copyback (nrCalls remains zero)
        },
            [&](const auto& saved) { return s.journal.Restore(saved, s.original); });
        check(result == Result::Restored && s.OriginalSr(result) && s.srCalls == 1 && s.nrCalls == 0,
            "creation/preparation return: restore caller state before original SR");
    }
    {
        Scene s;
        auto result = Run([&]() -> std::optional<NativeRecordingState::Snapshot> {
            throw std::runtime_error("capture unavailable");
        }, [&] { s.CustomNr(); }, [&](const auto& saved) { return s.journal.Restore(saved, s.original); });
        check(result == Result::Skipped && s.nrCalls == 0 && s.OriginalSr(result) && s.srCalls == 1,
            "capture failure: no NR mutation and original SR remains available");
    }
    {
        Scene s(true, true);
        auto result = Run([&] { return s.Capture(); }, [&] {
            s.CustomNr();
            // Legacy graphics replay may set away and back inside the NR pass.
            // Authentic full-stage restore must cover even that mutation.
            s.liveGraphicsRoot = s.gameRoot;
            s.liveGraphicsTable = 9876; s.liveGraphicsConstant = 99;
            if (!s.journal.ObserveGraphicsRootSignature(1, s.nrRoot) ||
                !s.journal.ObserveGraphicsRootSignature(1, s.gameRoot) ||
                !s.journal.ObserveGraphicsTable(1, 0, {s.liveGraphicsTable}) ||
                !s.journal.ObserveGraphicsConstants(1, 1, 1, &s.liveGraphicsConstant, 0))
                throw std::runtime_error("nested graphics replay failed");
        }, [&](const auto& saved) { return s.journal.Restore(saved, s.original); });
        check(result == Result::Restored && s.OriginalSr(result) && s.srCalls == 1,
            "graphics preference: cover nested graphics replay and both heap-invalidated tables");
    }
    std::cout << "native shared-list invocation checks=" << checks << " failed=" << failures << '\n';
    return failures ? 1 : 0;
}
