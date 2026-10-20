#define NR_GPU_SAFETY_TEST
#define NR_SELECTED_SR_LIFETIME_TESTING
#include "../../../OptiScaler/dlssnr/NrGpuSafety.cpp"
#include <dlssnr/NativeFeatureRegistry.h>
#include <dlssnr/NativeSelectedSrLifetimeLedger.h>
#include <nr/lifecycle/NativeOwnerSet.h>
#include <nr/lifecycle/SelectedFsr3NativeLeaseOwner.h>
#include <dxgi1_4.h>
#include <iostream>
#include <stdexcept>
using Microsoft::WRL::ComPtr;
namespace G=DlssNr::GpuSafety;
namespace L=Neurotic::Lifecycle;
namespace C=Neurotic::Contracts;
template<class T>constexpr bool PublicFactory=requires{&T::Create;};
static_assert(!PublicFactory<L::SelectedFsr3NativeLeaseOwner>);
namespace DlssNr
{
struct NativeSelectedSrLifetimeTestAccess
{
    static auto Register(NativeSelectedSrLifetimeLedger& ledger,std::shared_ptr<const NativeFeatureLifetime> feature,
        const L::OpaqueSrEvaluationReceipt::Entry& entry,G::Ticket ticket,std::shared_ptr<const void> capture)
    {return ledger.Register(std::move(feature),entry,std::move(ticket),std::move(capture));}
    static bool Return(NativeSelectedSrLifetimeLedger& ledger,const NativeSelectedSrLifetimeLedger::Registration& registration,
        const L::OpaqueSrEvaluationReceipt::Return& observed){return ledger.ObserveReturn(registration,observed);}
};
}
static void Api(HRESULT result){if(FAILED(result))throw std::runtime_error("WARP prerequisite failed");}
int main()
{
    unsigned checks=0;auto check=[&](bool condition){++checks;if(!condition)throw std::runtime_error("check "+std::to_string(checks));};
    ComPtr<IDXGIFactory4> factory;ComPtr<IDXGIAdapter> adapter;ComPtr<ID3D12Device> device;
    Api(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));Api(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
    Api(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
    D3D12_COMMAND_QUEUE_DESC q{};ComPtr<ID3D12CommandQueue> queue;Api(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)));
    for(unsigned mode=0;mode<9;++mode)
    {
        ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;
        Api(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
        Api(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)));
        check(G::PrepareRecordingHooks(list.Get()));Api(list->Close());Api(allocator->Reset());Api(list->Reset(allocator.Get(),nullptr));
        ComPtr<ID3D12Resource> output;D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=8;desc.Height=8;
        desc.DepthOrArraySize=1;desc.MipLevels=1;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;
        desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        Api(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&output)));
        D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.NumDescriptors=1;hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        ComPtr<ID3D12DescriptorHeap> rtv;Api(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&rtv)));
        device->CreateRenderTargetView(output.Get(),nullptr,rtv->GetCPUDescriptorHandleForHeapStart());
        C::Symbol symbol;check(symbol.Assign("sr.ledger."+std::to_string(mode)));
        auto owners=std::make_shared<L::NativeOwnerSet>(symbol);const auto* view=owners->Resources().Observe(output.Get());check(view!=nullptr);
        auto queueIdentity=owners->Resources().ObserveQueue(queue.Get());check(queueIdentity.IsKnown());
        check(owners->Resources().ObserveQueue(queue.Get())==queueIdentity);
        std::weak_ptr<L::NativeOwnerSet> retained=owners;
        DlssNr::NativeFeatureRegistry<void*> features;features.Set(1,reinterpret_cast<void*>(1));auto feature=features.Read(1);
        auto pin=features.Pin(1,feature);check(bool(pin));auto lifetime=pin->Lifetime();
        auto ledger=std::make_shared<DlssNr::NativeSelectedSrLifetimeLedger>();auto ticket=G::Record(list.Get());check(bool(ticket));
        L::OpaqueSrEvaluationReceipt::Entry entry;entry.featureHandle=feature.feature;entry.featureGeneration=feature.generation;
        entry.nativeList=list.Get();entry.recordingIncarnation=1;entry.parameters=&features;
        entry.output={output.Get(),view->identity};entry.outputRegion={0,0,8,8};entry.entrySequence=1;entry.completeCoverage=true;
        auto registration=DlssNr::NativeSelectedSrLifetimeTestAccess::Register(*ledger,lifetime,entry,mode==7?G::Ticket{}:ticket,owners);
        check(bool(registration)==(mode!=7));check(pin->MarkOpaqueEntry());
        if(registration)check(!ledger->RetainsTerminal(*registration));
        if(mode==5)check(!DlssNr::NativeSelectedSrLifetimeTestAccess::Register(*ledger,lifetime,entry,ticket,owners));
        const float color[]={0.2f,0.3f,0.4f,1};list->ClearRenderTargetView(rtv->GetCPUDescriptorHandleForHeapStart(),color,0,nullptr);
        L::OpaqueSrEvaluationReceipt::Return returned;returned.featureHandle=entry.featureHandle;returned.featureGeneration=entry.featureGeneration;
        returned.nativeList=entry.nativeList;returned.recordingIncarnation=entry.recordingIncarnation;returned.parameters=entry.parameters;
        returned.output=entry.output;returned.outputRegion=entry.outputRegion;returned.returnSequence=2;returned.workOrdinal=1;
        returned.completeCoverage=true;returned.ngxResult=mode==1?1:0;
        if(mode==2)returned.recordingIncarnation=99;
        if(registration&&mode!=6)check(DlssNr::NativeSelectedSrLifetimeTestAccess::Return(*ledger,*registration,returned)==(mode!=2));
        pin.reset();
        if(mode==4){auto bypass=features.Pin(1,feature);check(bypass&&bypass->MarkOpaqueEntry());}
        ledger->CloseAdmission();check(!ledger->Retirement());
        auto release=features.BeginRelease(1,feature);check(bool(release));
        if(mode!=8)check(release->Complete(mode!=3)==(mode!=3));
        check(!ledger->Retirement()); // actual successful Release alone never drains the recording
        Api(list->Close());ID3D12CommandList* submitted[]={list.Get()};queue->ExecuteCommandLists(1,submitted);
        check(G::Drain(5000));check(!ledger->Retirement()); // completed recording can replay
        Api(allocator->Reset());Api(list->Reset(allocator.Get(),nullptr));
        if(mode==8){ledger->Poll();G::NewSession();check(release->Complete(true));}
        auto receipt=ledger->Retirement();check(bool(receipt)==(mode<2||mode==8));
        if(receipt)
        {
            check(registration&&ledger->RetainsTerminal(*registration));
            check(ledger->CoversOutput(*receipt,output.Get(),ticket,entry.output.identity));
            check(!ledger->CoversOutput(*receipt,nullptr,ticket,entry.output.identity));
            check(!ledger->CoversOutput(*receipt,output.Get(),G::Record(list.Get()),entry.output.identity));
            DlssNr::NativeSelectedSrLifetimeLedger foreign;check(receipt->Matches(*ledger));check(!receipt->Matches(foreign));
            owners.reset();check(!retained.expired());check(!foreign.ReleaseRetiredCaptures(*receipt));
            check(ledger->ReleaseRetiredCaptures(*receipt));check(retained.expired());check(bool(ledger->Retirement()));
            check(!ledger->RetainsTerminal(*registration));
            check(features.StopCallbacks());check(features.ReopenAfterRetirement(*receipt));
            check(!features.ReopenAfterRetirement(*receipt));
            features.Set(1,reinterpret_cast<void*>(1));auto renewed=features.Read(1);
            check(renewed.generation>feature.generation);auto newPin=features.Pin(1,renewed);check(bool(newPin));
            newPin.reset();auto newRelease=features.BeginRelease(1,renewed);check(newRelease&&newRelease->Complete(true));
            check(features.StopCallbacks());check(!features.ReopenAfterRetirement(*receipt));
            ledger->MarkUnknown();check(!receipt->Matches(*ledger));check(!ledger->Retirement());
        }
        Api(list->Close());
    }
    std::cout<<"selected_sr_ledger: "<<checks<<" checks passed (real WARP recording owners; no provider)\n";
}
