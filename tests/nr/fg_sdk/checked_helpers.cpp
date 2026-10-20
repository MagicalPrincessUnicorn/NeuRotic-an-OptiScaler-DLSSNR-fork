#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <cmath>
#include <limits>
#include <cstdio>
#include <type_traits>
#ifdef SDK_V2
#include "external/FidelityFX-SDK-v2/Kits/FidelityFX/framegeneration/fsr3/dx12/FrameInterpolationSwapchainDX12_Helpers.h"
#else
#include "external/FidelityFX-SDK/sdk/src/backends/dx12/FrameInterpolationSwapchain/FrameInterpolationSwapchainDX12_Helpers.h"
#endif
static unsigned checks,failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::printf("FAIL line %d: %s\n",__LINE__,#x); } } while(0)
template<class T> void exercise(ID3D12CommandQueue* queue) {
    if constexpr(requires(T& c) { c.executeChecked(true); c.resetChecked(); c.availableChecked(); c.snapshotChecked(); }) {
        T commands; CHECK(commands.verify(queue)); commands.occupy(queue,L"Closure WARP test");
        CHECK(commands.resetChecked()!=nullptr);
        auto submitted=commands.executeChecked(true); CHECK(SUCCEEDED(submitted.result)); CHECK(submitted.executeInvoked);
        CHECK(submitted.signalSucceeded); auto snap=commands.snapshotChecked(); CHECK(snap.queue==queue); CHECK(snap.target==1);
        DWORD start=GetTickCount(); while(commands.availableChecked()==S_FALSE && GetTickCount()-start<2000) Sleep(1);
        CHECK(commands.availableChecked()==S_OK);
        commands.occupy(queue,L"Closure negative");
        // This list is already closed: a second Close must not execute or signal.
        auto rejected=commands.executeChecked(true); CHECK(FAILED(rejected.result)); CHECK(!rejected.executeInvoked); CHECK(!rejected.signalSucceeded);
        // Callback acceptance is not submission success, and count=1 cannot erase rejection.
        CHECK(!submitted.mayPublish(false,1)); CHECK(submitted.mayPublish(true,1)); CHECK(!rejected.mayPublish(true,1));
        CHECK(!submitted.mayPublish(true,0));
    } else { CHECK(false && "checked command methods absent"); }
}
int main() {
    IDXGIFactory4* factory=nullptr; IDXGIAdapter* warp=nullptr; ID3D12Device* device=nullptr; ID3D12CommandQueue* queue=nullptr;
    CHECK(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))));
    if(factory) CHECK(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp))));
    if(warp) CHECK(SUCCEEDED(D3D12CreateDevice(warp,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device))));
    D3D12_COMMAND_QUEUE_DESC desc{};
    if(device) CHECK(SUCCEEDED(device->CreateCommandQueue(&desc,IID_PPV_ARGS(&queue))));
    if(queue) exercise<Dx12Commands>(queue);
#ifndef SDK_V2
    if(queue) {
        int exact=0,alias=0; auto state=ffx::nr::createdRegistry().create(&exact,&alias,1); state->enrolled=true;
        auto pool=new Dx12CommandPool<1>; CHECK(pool->registerCheckedSlots(*state)); state->closure.sealInventory();
        { ffx::nr::WorkScope root(&exact); CHECK(bool(root)); CHECK(pool->get(queue,L"reserved")!=nullptr); CHECK(pool->get(queue,L"exhausted")==nullptr); CHECK(state->closure.failed()); }
        auto drain=state->closure.begin(1); CHECK(state->closure.poll(drain,1,100)==FFX_NR_API_FAILURE_RETAINED);
        // Retain the uncertain pool until this isolated process exits.
    }
    if(queue) {
        ID3D12Fence* fence=nullptr; CHECK(SUCCEEDED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence))));
        ID3D12Device5* removable=nullptr; CHECK(SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&removable))));
        Dx12Commands commands; CHECK(SUCCEEDED(commands.verifyChecked(queue))); commands.occupy(queue,L"removed"); CHECK(commands.resetChecked()!=nullptr);
        removable->RemoveDevice(); CHECK(FAILED(device->GetDeviceRemovedReason()));
        const auto signalResult=queue->Signal(fence,1),waitResult=queue->Wait(fence,1);
        std::printf("Removed WARP Signal=%08x Wait=%08x completed=%llu\n",unsigned(signalResult),unsigned(waitResult),static_cast<unsigned long long>(fence->GetCompletedValue()));
        CHECK(FAILED(commands.availableChecked()));
        auto observation=ffx::nr::observeFence(fence); CHECK(observation.value==UINT64_MAX || observation.removedReason<0);
        int exact=0,alias=0; auto state=ffx::nr::createdRegistry().create(&exact,&alias,1); state->enrolled=true; state->closure.sealInventory();
        { ffx::nr::WorkScope root(&exact); CHECK(bool(root)); ffx::nr::signal(queue,fence,2); ffx::nr::queueWait(queue,fence,2); CHECK(!ffx::nr::waitFence(fence,2,30)); CHECK(state->closure.failed()); }
        auto drain=state->closure.begin(1); CHECK(state->closure.poll(drain,1,100)!=FFX_NR_COMPLETE);
        SafeRelease(removable); SafeRelease(fence);
    }
#endif
    SafeRelease(queue); SafeRelease(device); SafeRelease(warp); SafeRelease(factory);
    std::printf("%u checks, %u failures\n",checks,failures); return failures?1:0;
}
