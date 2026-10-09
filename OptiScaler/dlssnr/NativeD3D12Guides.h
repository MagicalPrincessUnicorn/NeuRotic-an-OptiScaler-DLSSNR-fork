#pragma once
#include <d3d12.h>
#include <dxgi1_4.h>
#include <cstdint>
#include <string>
namespace DlssNr::NativeD3D12Guides {
void Install(ID3D12Device*);
void Present(IDXGISwapChain*,ID3D12CommandQueue*,bool enabled);
// Requires an external gate against new NR work; never forces retirement.
bool CanYieldOutput(std::string& reason);
#ifdef NR_NATIVE12_TESTING
struct Snapshot {std::uint64_t retainedBytes=0,draws=0;unsigned descriptors=0,recordings=0,candidates=0,unsupported=0;bool installed=false,resourcePressure=false,recordingPressure=false,restartRequired=false;};
Snapshot Inspect(ID3D12CommandQueue* selectedQueue=nullptr);
void ClearObservations();
void EndObservationFrame();
bool SetTestBudget(std::uint64_t);
#endif
}
