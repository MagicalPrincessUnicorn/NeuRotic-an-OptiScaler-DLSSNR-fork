// Real software D3D12 owner proof; no game or vendor provider.
#define NR_GPU_SAFETY_TEST
#include "../OptiScaler/dlssnr/NrGpuSafety.cpp"
#include <dxgi1_4.h>
#include <cstdio>
#include <cstdlib>
#include <array>
using Microsoft::WRL::ComPtr;
namespace S=DlssNr::GpuSafety;
static unsigned checks=0;
static void Check(bool value,const char* reason) { ++checks; if(!value){std::fprintf(stderr,"FAIL: %s\n",reason);std::exit(1);} }
static void Hr(HRESULT value){Check(SUCCEEDED(value),"fixture operation succeeded");}
static bool Cancel(ID3D12GraphicsCommandList* list,const S::Ticket& ticket,ID3D12CommandAllocator* allocator)
{
#ifdef NR_GPU_CANCEL_OWNED_UNSUBMITTED_V1
    return S::CancelOwnedUnsubmittedRecording(list,ticket,allocator);
#else
    (void)list;(void)ticket;(void)allocator;
    return false; // Baseline ReleaseObjects only releases the retained list.
#endif
}
int main()
{
    ComPtr<IDXGIFactory4> factory;Hr(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
    ComPtr<IDXGIAdapter> adapter;Hr(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
    ComPtr<ID3D12Device> device;Hr(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
    D3D12_COMMAND_QUEUE_DESC qd{};ComPtr<ID3D12CommandQueue> queue;Hr(device->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue)));
    ComPtr<ID3D12CommandAllocator> allocator;Hr(device->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&allocator)));
    ComPtr<ID3D12GraphicsCommandList> list;Hr(device->CreateCommandList(0,qd.Type,allocator.Get(),nullptr,IID_PPV_ARGS(&list)));
    Check(S::PrepareRecordingHooks(list.Get()),"hooks available");
    // PrepareRecordingHooks enrolls cold metadata before the first owned Reset.
    Hr(list->Close());Hr(list->Reset(allocator.Get(),nullptr));
    auto ticket=S::Record(list.Get());
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=256;
    desc.Height=desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> resource;Hr(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&resource)));
    auto action=S::BeginLocalAction(ticket,list.Get(),resource.Get());Check(bool(action),"first reader after observed Reset is available");
    auto pins=std::make_shared<std::array<ComPtr<IUnknown>,2>>();(*pins)[0]=list;(*pins)[1]=allocator;
    std::weak_ptr<void> backing=pins;Check(S::RetainDerivedUse(*action,pins),"reader retains list and allocator");pins.reset();
    Check(!Cancel(list.Get(),ticket,allocator.Get()),"active recording borrow cannot be canceled");action.reset();
    Check(!S::Reusable(ticket),"retained unsubmitted recording is not reusable");
    S::State().failed=true;
    Check(!Cancel(list.Get(),ticket,allocator.Get()),"failed completion registry cannot grant cancellation");
    S::State().failed=false;
    Check(Cancel(list.Get(),ticket,allocator.Get()),"known unsubmitted retained reader is canceled before owner release");
    Check(S::Reusable(ticket),"successful Reset proves cancellation");
    auto terminal=S::InspectTerminalRecording(ticket);Check(terminal&&terminal->submissions==0&&terminal->state==S::RecordingTerminalState::Reset,"cancellation is exact zero-submission Reset evidence");
    Check(!Cancel(list.Get(),ticket,allocator.Get()),"already canceled stale cookie grants no new Reset");
    ticket.reset();S::Pending();Check(backing.expired(),"terminal cancellation breaks retained list cycle");
    auto current=S::Record(list.Get());Check(!Cancel(list.Get(),ticket,allocator.Get()),"null expected cookie refuses");
    auto foreign=std::make_shared<S::Recording>();Check(!Cancel(list.Get(),foreign,allocator.Get()),"foreign ticket refuses");
    current->failed=true;Check(!Cancel(list.Get(),current,allocator.Get()),"failed recording refuses");current->failed=false;
    ComPtr<ID3D12Fence> gate;Hr(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate)));
    Hr(queue->Wait(gate.Get(),1));Hr(list->Close());ID3D12CommandList* batch[]={list.Get()};queue->ExecuteCommandLists(1,batch);
    Check(!Cancel(list.Get(),current,allocator.Get()),"submitted pending recording cannot be canceled");
    Check(!S::Reusable(current),"submitted work remains retained");
    Check(S::SealOwnedRecording(list.Get()),"owned submitted recording is sealed normally");
    Check(!Cancel(list.Get(),current,allocator.Get()),"sealed submitted recording cannot be canceled");
    Hr(gate->Signal(1));Check(S::Drain(5000),"actual software queue completes");Check(S::Reusable(current),"actual completed sealed recording retires");
    std::printf("PASS: %u exact UI reader cancellation checks\n",checks);
}
