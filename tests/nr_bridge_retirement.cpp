// Real WARP queue/recording and cross-API fence observations; no NR provider.
#define NR_GPU_SAFETY_TEST
#include "../OptiScaler/dlssnr/NrGpuSafety.cpp"
#include "dlssnr/NrBridgeRetirement.h"
#include <d3d11_4.h>
#include <dxgi1_4.h>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <chrono>

using Microsoft::WRL::ComPtr;
namespace B=DlssNr::Bridge;
namespace S=DlssNr::GpuSafety;
static unsigned checks=0;
static void Check(bool value,const char* reason)
{ ++checks; if (!value) { std::fprintf(stderr,"FAIL: %s\n",reason); std::exit(1); } }
static void Hr(HRESULT result) { Check(SUCCEEDED(result),"native fixture operation succeeded"); }
template<class Use> bool WaitReuse(Use& use,ID3D11Fence* consumer,ID3D12Fence* producer,
    UINT64 value,ID3D12CommandQueue* queue,DWORD timeout)
{
    if constexpr(requires {use.WaitForReuse(consumer,producer,value,queue,timeout);})
        return use.WaitForReuse(consumer,producer,value,queue,timeout);
    else return use.Reusable(consumer->GetCompletedValue());
}
int main()
{
    ComPtr<IDXGIFactory4> factory; Hr(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
    ComPtr<IDXGIAdapter> adapter; Hr(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
    ComPtr<ID3D12Device> device; Hr(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
    ComPtr<ID3D11Device> device11; ComPtr<ID3D11DeviceContext> context;
    Hr(D3D11CreateDevice(adapter.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,0,nullptr,0,D3D11_SDK_VERSION,
        &device11,nullptr,&context));
    ComPtr<ID3D11Device5> device5; Hr(device11.As(&device5));
    ComPtr<ID3D11DeviceContext4> context4; Hr(context.As(&context4));
    ComPtr<ID3D11Fence> consumer; Hr(device5->CreateFence(0,D3D11_FENCE_FLAG_SHARED,IID_PPV_ARGS(&consumer)));
    HANDLE shared=nullptr; Hr(consumer->CreateSharedHandle(nullptr,GENERIC_ALL,nullptr,&shared));
    ComPtr<ID3D12Fence> consumer12; Hr(device->OpenSharedHandle(shared,IID_PPV_ARGS(&consumer12))); CloseHandle(shared);
    D3D12_COMMAND_QUEUE_DESC desc {}; desc.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> queue; Hr(device->CreateCommandQueue(&desc,IID_PPV_ARGS(&queue)));
    ComPtr<ID3D12Fence> gate, finished;
    Hr(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate)));
    Hr(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&finished)));
    ComPtr<ID3D12CommandAllocator> allocator; Hr(device->CreateCommandAllocator(desc.Type,IID_PPV_ARGS(&allocator)));
    ComPtr<ID3D12GraphicsCommandList> list;
    Hr(device->CreateCommandList(0,desc.Type,allocator.Get(),nullptr,IID_PPV_ARGS(&list)));
    Check(S::PrepareRecordingHooks(list.Get()),"existing owner observes recording hooks before Reset");
    Hr(list->Close()); Hr(list->Reset(allocator.Get(),nullptr));
    auto writerUse=std::make_shared<B::Use>();
    auto& use=*writerUse;
    B::CacheWriter cacheWriter;
    use.recording=S::Record(list.Get()); Check(bool(use.recording),"recording registered before GPU work");
    use.consumer=1;
    Check(!WaitReuse(use,consumer.Get(),finished.Get(),1,queue.Get(),1),"unsubmitted recording cannot be waited into reuse");
    Check(cacheWriter.Bind(writerUse,consumer.Get()),"cache representation binds exact first writer use");
    Hr(list->Close()); Hr(queue->Wait(gate.Get(),1));
    ID3D12CommandList* batch[]={list.Get()}; queue->ExecuteCommandLists(1,batch);
    Check(S::OrderedOn(use.recording,queue.Get()),"exact recording observed on actual producer queue");
    Check(S::SealOwnedRecording(list.Get()),"owned recording sealed against replay");
    Hr(queue->Signal(consumer12.Get(),1));
    Check(!use.Reusable(consumer->GetCompletedValue()),"blocked producer withholds context/queue generation retirement");
    Check(!cacheWriter.Reusable(),"second live bridge handle cannot overwrite pending first writer");
    Check(!cacheWriter.Bind(std::make_shared<B::Use>(),consumer.Get()),"replacement writer cannot steal pending cache representation");
    Check(!S::Drain(1),"shutdown drain cannot invent completion for blocked submitted work");
    Hr(queue->Signal(finished.Get(),1));
    Check(!WaitReuse(use,consumer.Get(),finished.Get(),1,queue.Get(),1),"bounded timeout retains blocked producer");
    std::thread releaseProducer([&]{std::this_thread::sleep_for(std::chrono::milliseconds(20));gate->Signal(1);});
    const bool waitedProducer=WaitReuse(use,consumer.Get(),finished.Get(),1,queue.Get(),5000);
    releaseProducer.join();
    Check(waitedProducer,"pending known submission completes without skipping next frame");
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    Check(event!=nullptr,"completion event created"); Hr(finished->SetEventOnCompletion(1,event));
    Check(WaitForSingleObject(event,5000)==WAIT_OBJECT_0,"known producer submission finished");
    Check(use.Reusable(consumer->GetCompletedValue()),"completed producer and cross-API consumer permit retirement");
    Check(cacheWriter.Reusable(),"representation owner observes completed first writer");
    // A second real submission completes while its D3D11 consumer remains behind
    // a separate externally controlled fence wait. D3D12 completion alone fails.
    Hr(allocator->Reset()); Hr(list->Reset(allocator.Get(),nullptr));
    use.recording=S::Record(list.Get()); Check(bool(use.recording),"second exact recording enrolled");
    Hr(list->Close()); queue->ExecuteCommandLists(1,batch);
    Check(S::SealOwnedRecording(list.Get()),"second recording sealed");
    Hr(queue->Signal(finished.Get(),2)); Hr(finished->SetEventOnCompletion(2,event));
    Check(WaitForSingleObject(event,5000)==WAIT_OBJECT_0,"second producer finished");
    Hr(context4->Wait(consumer.Get(),2)); Hr(context4->Signal(consumer.Get(),3)); context4->Flush();
    use.consumer=3;
    Check(!cacheWriter.Reusable(),"cache owner observes later consumer target on same live use");
    Check(S::Reusable(use.recording),"D3D12 producer retirement independently proven");
    Check(!use.Reusable(consumer->GetCompletedValue()),"pending D3D11 consumer independently withholds reuse");
    Check(!WaitReuse(use,consumer.Get(),finished.Get(),2,queue.Get(),1),"producer completion alone cannot end consumer wait");
    std::thread releaseConsumer([&]{std::this_thread::sleep_for(std::chrono::milliseconds(20));consumer12->Signal(2);});
    const bool waitedConsumer=WaitReuse(use,consumer.Get(),finished.Get(),2,queue.Get(),5000);
    releaseConsumer.join();
    Check(waitedConsumer,"known pending D3D11 consumer completes before reuse");
    Hr(consumer->SetEventOnCompletion(3,event));
    Check(WaitForSingleObject(event,5000)==WAIT_OBJECT_0,"D3D11 consumer finished after actual wait release");
    Check(use.Reusable(consumer->GetCompletedValue()),"both consumers prove terminal retirement");
    use.unknown=true;
    Check(!WaitReuse(use,consumer.Get(),finished.Get(),2,queue.Get(),5000),"unknown submission never gains reuse through waiting");
    Check(!cacheWriter.Reusable(),"cache owner observes late signal failure instead of a copied old status");
    Check(!use.Reusable(consumer->GetCompletedValue()),"injected post-execute signal failure retains even completed generation");
    Check(!use.Reusable(UINT64_MAX),"device removal never grants retirement");
    CloseHandle(event);
    std::printf("PASS: %u bridge retirement checks (real D3D12/D3D11 WARP plus unknown-completion injection)\n",checks);
}
