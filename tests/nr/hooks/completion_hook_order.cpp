// Real production owners on WARP. No proprietary provider or game.
#define NR_GPU_SAFETY_TEST
#include "../../../OptiScaler/dlssnr/NrGpuSafety.cpp"
#include <nr/d3d12/NativeRecordingHooks.h>
#include <dxgi1_4.h>
#include <d3d12sdklayers.h>
#include <iostream>
#include <stdexcept>
#include <string>
using Microsoft::WRL::ComPtr;
namespace G = DlssNr::GpuSafety;
namespace N = Neurotic::D3D12;
static unsigned checks;
static void Check(bool value, const char* why) { ++checks; if (!value) throw std::runtime_error(why); }
static void Ok(HRESULT result) { Check(SUCCEEDED(result), "WARP API"); }
int main(int argc, char** argv) try
{
    const bool late = argc > 1 && std::string(argv[1]) == "late";
    ComPtr<ID3D12Debug> debug; Ok(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))); debug->EnableDebugLayer();
    ComPtr<IDXGIFactory4> factory; ComPtr<IDXGIAdapter> warp; ComPtr<ID3D12Device> device;
    Ok(CreateDXGIFactory1(IID_PPV_ARGS(&factory))); Ok(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)));
    Ok(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
    ComPtr<ID3D12CommandAllocator> allocator; ComPtr<ID3D12GraphicsCommandList> list;
    Ok(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
    Ok(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list)));
    auto& observer = *new N::NativeRecordingObserver;
    auto& hooks = *new N::NativeRecordingHooks(observer);
    Check(!G::PrepareRecordingHooks(nullptr), "null preparation refuses");
    if (!late) Check(G::PrepareRecordingHooks(list.Get()), "completion hooks prepared before native coverage");
    if (!late)
    {
        auto cold = G::Get<G::RecordingState>(list.Get(), G::recordingStateGuid);
        Check(cold && !cold->open && !cold->active && !G::Get<G::Recording>(list.Get(), G::recordingGuid),
              "preparation has only cold metadata without ticket or Reset authority");
    }
    Check(hooks.Install(list.Get()), "native hooks installed");
    Check(!observer.Observe(list.Get()).completeCoverage, "installation never invents Reset");
    Ok(list->Close()); Ok(list->Reset(allocator.Get(), nullptr));
    const auto before = observer.Observe(list.Get());
    Check(before.completeCoverage, "real Reset grants native recording coverage");
    auto ticket = G::Record(list.Get());
    Check(bool(ticket), "existing owner records ticket");
    const auto after = observer.Observe(list.Get());
    if (late)
    {
        const auto d = hooks.Diagnostic(list.Get());
        Check(!after.completeCoverage && d.firstRefusal == N::NativeRecordingHookRefusal::RouteBytesMismatch && d.failedSlot == 9,
              "late completion install reproduces physical Close route mismatch");
        std::cout << "checks=" << checks << " PASS late-order refusal\n"; return 0;
    }
    Check(after.completeCoverage, "completion ticket must preserve enrolled native route coverage");
    Check(after.incarnation == before.incarnation, "preparation cannot replace recording incarnation");
    D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{}; desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; desc.Width = 256;
    desc.Height = desc.DepthOrArraySize = desc.MipLevels = 1; desc.SampleDesc.Count = 1; desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> resource;
    Ok(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&resource)));
    auto action = G::BeginLocalAction(ticket, list.Get(), resource.Get());
    Check(action && action->CurrentFor(resource.Get()), "completion owner observed actual Reset and grants exact resource borrow");
    Check(G::PrepareRecordingHooks(list.Get()) && action->CurrentFor(resource.Get()) && observer.Observe(list.Get()).completeCoverage,
          "idempotent preparation preserves active owner borrow and native coverage");
    action.reset();
    Check(!G::InspectTerminalRecording(ticket), "unsubmitted open recording is not terminal");
    Ok(list->Close());
    Check(!observer.Observe(list.Get()).completeCoverage, "actual Close retires native coverage");
    Check(!G::InspectTerminalRecording(ticket), "Close alone cannot seal replayable ticket");
    Ok(allocator->Reset()); Ok(list->Reset(allocator.Get(), nullptr));
    Check(bool(G::InspectTerminalRecording(ticket)), "actual Reset cancels unsubmitted old ticket");
    Check(observer.Observe(list.Get()).completeCoverage && observer.Observe(list.Get()).incarnation != before.incarnation,
          "same real Reset enrolls a new native incarnation");
    auto second = G::Record(list.Get());
    Check(second && second != ticket && observer.Observe(list.Get()).completeCoverage, "repeated ticket acquisition preserves routes");
    Check(hooks.Diagnostic(list.Get()).firstRefusal == N::NativeRecordingHookRefusal::None, "no hidden hook rejection");
    Ok(list->Close());
    std::cout << "checks=" << checks << " PASS prepared-order lifecycle\n"; return 0;
}
catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
