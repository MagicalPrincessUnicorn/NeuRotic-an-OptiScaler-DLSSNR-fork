#define NOMINMAX
#include <Windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <cstdio>
#include <vector>
#include "external/FidelityFX-CheckedClosure/CheckedSdkRuntime.h"
static unsigned checks,failures;
#define CHECK(x) do{++checks;if(!(x)){++failures;std::printf("FAIL line %d: %s\n",__LINE__,#x);}}while(0)
int main(){
 ID3D12Debug* debug=nullptr;CHECK(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))));
 if(!debug)return 1;debug->EnableDebugLayer();debug->Release();
 IDXGIFactory4* factory=nullptr;IDXGIAdapter* warp=nullptr;ID3D12Device* device=nullptr;
 ID3D12CommandQueue* queue=nullptr;ID3D12Fence* fence=nullptr;ID3D12InfoQueue* info=nullptr;
 CHECK(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))));
 CHECK(factory&&SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp))));
 CHECK(warp&&SUCCEEDED(D3D12CreateDevice(warp,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device))));
 D3D12_COMMAND_QUEUE_DESC desc{};
 CHECK(device&&SUCCEEDED(device->CreateCommandQueue(&desc,IID_PPV_ARGS(&queue))));
 CHECK(device&&SUCCEEDED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence))));
 CHECK(device&&SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&info))));
 if(failures)return 1;
 info->ClearStoredMessages();
 CHECK(ffx::nr::queueWaitForPreviousUse(queue,fence,0)==S_OK);
 unsigned warning1424=0,errors=0;
 for(UINT64 i=0;i<info->GetNumStoredMessages();++i){SIZE_T n=0;CHECK(SUCCEEDED(info->GetMessage(i,nullptr,&n)));std::vector<unsigned char> bytes(n);auto* m=reinterpret_cast<D3D12_MESSAGE*>(bytes.data());CHECK(SUCCEEDED(info->GetMessage(i,m,&n)));if(unsigned(m->ID)==1424)++warning1424;if(m->Severity<=D3D12_MESSAGE_SEVERITY_WARNING)++errors;}
 std::printf("initial wait warnings1424=%u warning_or_error=%u\n",warning1424,errors);
 CHECK(warning1424==0&&errors==0);
 if(failures)return 1; // Red must fail on the real D3D12 warning before negative pointer tests.
 // Positive target must actually enqueue ordering, not return fabricated success.
 ID3D12Fence* marker=nullptr;CHECK(SUCCEEDED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&marker))));
 CHECK(ffx::nr::queueWaitForPreviousUse(queue,fence,1)==S_OK);
 CHECK(queue->Signal(marker,1)==S_OK);Sleep(20);CHECK(marker->GetCompletedValue()==0);
 CHECK(fence->Signal(1)==S_OK);auto start=GetTickCount64();while(marker->GetCompletedValue()<1&&GetTickCount64()-start<2000)Sleep(1);
 CHECK(marker->GetCompletedValue()==1);marker->Release();
 int exact=0,alias=0;auto context=ffx::nr::createdRegistry().create(&exact,&alias,1);context->enrolled=true;context->closure.sealInventory();
 {ffx::nr::WorkScope scope(&exact);CHECK(bool(scope));CHECK(FAILED(ffx::nr::queueWaitForPreviousUse(nullptr,fence,0)));CHECK(FAILED(ffx::nr::queueWaitForPreviousUse(queue,nullptr,0)));CHECK(context->closure.failed());}
 int removed=0,removedAlias=0;auto removedContext=ffx::nr::createdRegistry().create(&removed,&removedAlias,1);removedContext->enrolled=true;removedContext->closure.sealInventory();
 ID3D12Device5* removable=nullptr;CHECK(SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&removable))));removable->RemoveDevice();CHECK(FAILED(device->GetDeviceRemovedReason()));
 {ffx::nr::WorkScope scope(&removed);CHECK(bool(scope));CHECK(FAILED(ffx::nr::queueWaitForPreviousUse(queue,fence,0)));CHECK(removedContext->closure.failed());}
 removable->Release();info->Release();fence->Release();queue->Release();device->Release();warp->Release();factory->Release();
 std::printf("%u checks, %u failures; WARP synchronization only, no provider/game\n",checks,failures);return failures?1:0;
}
