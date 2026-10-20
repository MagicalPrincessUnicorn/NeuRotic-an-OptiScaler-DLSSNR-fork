#include "../addons/prepared-guides/flow/PreparedFlowD3D12.h"
#include "nr_reshade_device_fixture.h"
#include <dxgi1_4.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace Neurotic::PreparedFlow;
static void Check(bool ok,const char* message){if(!ok){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
static void HR(HRESULT value){Check(SUCCEEDED(value),"D3D12 fixture operation");}
// These typed facades are used only for negative admission. A refused child
// must never reach a real queue. All other methods preserve the underlying API.
template<class T> class WrongOwnerBase : public T {
 std::atomic<ULONG> refs{1};
protected:
 ComPtr<T> base;ComPtr<ID3D12Device> owner;
public:
 WrongOwnerBase(T* value,ID3D12Device* parent):base(value),owner(parent){}
 HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override {
  if(!out)return E_POINTER;*out=nullptr;
  if(id!=__uuidof(IUnknown)&&id!=__uuidof(T)&&id!=__uuidof(ID3D12DeviceChild))return E_NOINTERFACE;
  *out=static_cast<T*>(this);AddRef();return S_OK;
 }
 ULONG STDMETHODCALLTYPE AddRef() override{return ++refs;}
 ULONG STDMETHODCALLTYPE Release() override{const auto n=--refs;if(!n)delete this;return n;}
 virtual ~WrongOwnerBase()=default;
 HRESULT STDMETHODCALLTYPE GetDevice(REFIID id,void** out) override{return owner->QueryInterface(id,out);}
 HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID id,UINT* size,void* data) override{return base->GetPrivateData(id,size,data);}
 HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID id,UINT size,const void* data) override{return base->SetPrivateData(id,size,data);}
 HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID id,const IUnknown* value) override{return base->SetPrivateDataInterface(id,value);}
 HRESULT STDMETHODCALLTYPE SetName(LPCWSTR name) override{return base->SetName(name);}
};
class WrongFence final : public WrongOwnerBase<ID3D12Fence> {
public:
 using WrongOwnerBase::WrongOwnerBase;
 UINT64 STDMETHODCALLTYPE GetCompletedValue() override{return base->GetCompletedValue();}
 HRESULT STDMETHODCALLTYPE SetEventOnCompletion(UINT64 value,HANDLE event) override{return base->SetEventOnCompletion(value,event);}
 HRESULT STDMETHODCALLTYPE Signal(UINT64 value) override{return base->Signal(value);}
};
class WrongColor final : public WrongOwnerBase<ID3D12Resource> {
public:
 using WrongOwnerBase::WrongOwnerBase;
 HRESULT STDMETHODCALLTYPE Map(UINT sub,const D3D12_RANGE* range,void** out) override{return base->Map(sub,range,out);}
 void STDMETHODCALLTYPE Unmap(UINT sub,const D3D12_RANGE* range) override{base->Unmap(sub,range);}
 D3D12_RESOURCE_DESC STDMETHODCALLTYPE GetDesc() override{return base->GetDesc();}
 D3D12_GPU_VIRTUAL_ADDRESS STDMETHODCALLTYPE GetGPUVirtualAddress() override{return base->GetGPUVirtualAddress();}
 HRESULT STDMETHODCALLTYPE WriteToSubresource(UINT sub,const D3D12_BOX* box,const void* data,UINT row,UINT depth) override{return base->WriteToSubresource(sub,box,data,row,depth);}
 HRESULT STDMETHODCALLTYPE ReadFromSubresource(void* data,UINT row,UINT depth,UINT sub,const D3D12_BOX* box) override{return base->ReadFromSubresource(data,row,depth,sub,box);}
 HRESULT STDMETHODCALLTYPE GetHeapProperties(D3D12_HEAP_PROPERTIES* heap,D3D12_HEAP_FLAGS* flags) override{return base->GetHeapProperties(heap,flags);}
};
int main(){
 ComPtr<IDXGIFactory4> factory;ComPtr<IDXGIAdapter> adapter;ComPtr<ID3D12Device> native;
 HR(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));HR(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
 HR(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&native)));
 ComPtr<ReShadeDeviceProxy> proxy;proxy.Attach(new ReShadeDeviceProxy(native.Get()));
 Session session;Options options;options.width=256;options.height=64;std::string reason;
 Check(session.Initialize(proxy.Get(),options,reason),reason.c_str());
 D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
 D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=256;
 desc.Height=64;desc.DepthOrArraySize=desc.MipLevels=1;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;
 Input input;HR(proxy->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&input.color)));
 D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 size=0;proxy->GetCopyableFootprints(&desc,0,1,0,&footprint,nullptr,nullptr,&size);
 heap.Type=D3D12_HEAP_TYPE_UPLOAD;desc={};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=size;
 desc.Height=desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
 ComPtr<ID3D12Resource> upload;HR(proxy->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&upload)));
 void* mapped=nullptr;HR(upload->Map(0,nullptr,&mapped));std::memset(mapped,127,static_cast<size_t>(size));upload->Unmap(0,nullptr);
 D3D12_COMMAND_QUEUE_DESC q{};q.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;ComPtr<ID3D12CommandQueue> queue;
 ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;
 HR(proxy->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)));HR(proxy->CreateCommandAllocator(q.Type,IID_PPV_ARGS(&allocator)));
 HR(proxy->CreateCommandList(0,q.Type,allocator.Get(),nullptr,IID_PPV_ARGS(&list)));
 D3D12_TEXTURE_COPY_LOCATION dst{},src{};dst.pResource=input.color.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
 src.pResource=upload.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint=footprint;
 list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
 b.Transition={input.color.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};list->ResourceBarrier(1,&b);
 HR(list->Close());ID3D12CommandList* commands=list.Get();queue->ExecuteCommandLists(1,&commands);
 HR(proxy->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&input.producer.fence)));input.producer.value=1;HR(queue->Signal(input.producer.fence.Get(),1));
 HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);Check(event!=nullptr,"completion event");
 HR(input.producer.fence->SetEventOnCompletion(1,event));Check(WaitForSingleObject(event,5000)==WAIT_OBJECT_0,"upload completed");
 input.ownership=std::make_shared<ComPtr<ID3D12Resource>>(input.color);input.pair={1,0,10,20};input.reset=true;
 ComPtr<ReShadeDeviceProxy> unrelated;unrelated.Attach(new ReShadeDeviceProxy(native.Get(),ReShadeDeviceProxy::Unsupported));
 ComPtr<WrongFence> wrongFence;wrongFence.Attach(new WrongFence(input.producer.fence.Get(),unrelated.Get()));
 ComPtr<WrongColor> wrongColor;wrongColor.Attach(new WrongColor(input.color.Get(),unrelated.Get()));
 const auto rejects=[&](const Input& candidate,const char* expected){
  Check(session.SubmitChecked(candidate,reason)==SubmitDisposition::Rejected,"admission refusal issues no new work");
  Check(reason.find(expected)!=std::string::npos,expected);
 };
 auto bad=input;bad.producer.fence=wrongFence;rejects(bad,"Flow producer fence device identity mismatch");
 bad=input;bad.color=wrongColor;rejects(bad,"Flow color device identity mismatch");
 bad=input;bad.pair.previousCapture=1;rejects(bad,"Flow capture pair rejected");
 bad=input;bad.ownership.reset();rejects(bad,"owning lease missing");
 bad=input;bad.producer.value=0;rejects(bad,"valid timeline");
 bad={};
 const bool accepted=session.Submit(input,reason);if(!accepted)std::fprintf(stderr,"flow: %s\n",reason.c_str());
 Check(accepted,"wrapped execution device accepts its actual color and producer fence");
 Output output;const auto deadline=GetTickCount64()+5000;
 while(!session.Poll(output,reason)){Check(GetTickCount64()<deadline,"flow completion");Sleep(1);}
 Check(!session.AddReader({wrongFence,1},reason)&&reason.find("Flow reader fence device identity mismatch")!=std::string::npos,"reader rejects unrelated owner independently");
 Check(session.AddReader(input.producer,reason),"reader uses the same reconciled device identity");
 output={};Check(session.ReturnOutput(reason),"return output");Check(session.Close(reason),"completed flow closes");
 Check(proxy->commandListsCreated>1&&proxy->fencesCreated>1,"execution retains proxy callbacks");
 Session lost;Check(lost.Initialize(proxy.Get(),options,reason),"device-loss fixture initializes");
 proxy->mode=ReShadeDeviceProxy::DeviceLost; // injected status; real WARP device stays healthy
 Check(lost.SubmitChecked(input,reason)==SubmitDisposition::Unsafe,"reported device removal requires quarantine");
 proxy->mode=ReShadeDeviceProxy::Normal;
 Check(lost.SubmitChecked(input,reason)==SubmitDisposition::Unsafe,"poisoned flow owner cannot retry");
 Check(!lost.Close(reason),"quarantined owner remains retained");
 CloseHandle(event);std::puts("PASS: wrapped flow, independent owner refusal, clean retry, unsafe quarantine and proxy callbacks on WARP");
}
