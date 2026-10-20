#define NR_GPU_SAFETY_TEST
#include "../../../OptiScaler/dlssnr/NrGpuSafety.cpp"
#include "../../../OptiScaler/dlssnr/DlssNr_PresentGuides.h"
#include "../context/ContextFixtureStore.h"
#if __has_include(<nr/lifecycle/NativeLeaseBinding.h>)
#define HAS_GUIDE_LEASE 1
#endif
#include <d3d12sdklayers.h>
using namespace NrTest002;
using Microsoft::WRL::ComPtr;
namespace L=Neurotic::Lifecycle;
namespace G=DlssNr::PresentGuides;
namespace S=DlssNr::GpuSafety;
#ifdef HAS_GUIDE_LEASE
class GuideOwner final:public L::NativeLeaseOwner
{
    NrContextTests::Store store;
    std::vector<std::pair<ID3D12Resource*,L::LeaseBinding>> mappings;
  public:
    bool stale=false,held=false,scopeClosed=false,omitRelease=false,reportedHold=false;
    std::optional<L::LeaseBinding> Describe(ID3D12Resource* resource) override
    {
        for(const auto& value:mappings)if(value.first==resource)return value.second;
        // Fixture-only publication, not production pointer-derived identity. A real host
        // supplies its LIFE-001 resource facade and event evidence at allocation time.
        L::LeaseBinding b;
        b.description.header=NrContextTests::Header(C::ContractId::C03,C::OwnerDomain::Resource,800+mappings.size());
        b.description.context={Sym(C::CanonicalFrameContext::WireName),Key("context"),1};
        b.description.candidate={Sym(C::AcquisitionCandidate::WireName),Key("candidate"),1};
        b.description.resource=NrContextTests::Resource(100+mappings.size(),{}).identity;
        b.description.use=C::UsageKind::OrderedGpuRead;b.description.commandScope.key=Key("private-recording");
        b.description.device=Known(Id<C::ObjectIncarnation>("device",1));b.description.queue=Known(Id<C::ObjectIncarnation>("queue",1));
        b.description.revoked=Known(false);b.description.revocationEpoch=Known(Key("epoch",1));
        b.description.expirationBoundary=Known(Key("end",1));b.description.submissionRecheckRequired=Known(true);
        b.description.generations=store.Publish(C::GenerationVector{},C::OwnerDomain::Resource);
        b.consumer=Key("present-model");b.purpose=Sym("NativeTemporal");
        mappings.push_back({resource,b});return b;
    }
    L::OwnerFacts Refresh(const L::LeaseBinding& b,ID3D12Resource* resource,ID3D12GraphicsCommandList*,ID3D12CommandQueue*) override
    {
        L::OwnerFacts f;f.current=b;
        for(const auto& value:mappings)
            if(value.first==resource&&value.second.description.resource==b.description.resource)f.nativeBindingMatches=L::Fact::Yes;
        f.permission=L::Fact::Yes;f.registryHealthy=L::Fact::Yes;f.apiCompatible=L::Fact::Yes;f.callbackActive=scopeClosed?L::Fact::No:L::Fact::Yes;
        f.resourcesRegistered=L::Fact::Yes;f.descriptorsProtected=L::Fact::Yes;
        f.externalRegistrationsComplete=L::Fact::Yes;f.ownerCanRecycle=L::Fact::Yes;
        if(stale)f.current.description.resource.contentRevision=Known(C::ContentRevision{44});
        reportedHold=reportedHold||held;
        if(reportedHold&&!omitRelease)
        {
            L::ExternalHold h;h.registration=Key("capture-hold");h.consumer=Key("capture");
            h.kind=L::HoldKind::Capture;h.target=b.description.resource;h.released=held?L::Fact::No:L::Fact::Yes;
            if(!held)h.releaseEvidence=Key("capture-owner-release");f.holds.Push(h);
        }
        return f;
    }
};
#endif
static void Api(HRESULT hr){if(FAILED(hr))throw std::runtime_error("D3D12 prerequisite or API failure");}
int main(int argc,char** argv)
{
    return Guard([&]{Report r("guide-consumption-leases",argc==2?std::string_view(argv[1]):std::string_view{});
#ifdef HAS_GUIDE_LEASE
        ComPtr<ID3D12Debug> debug;Api(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));debug->EnableDebugLayer();
        ComPtr<IDXGIFactory4> factory;ComPtr<IDXGIAdapter> warp;ComPtr<ID3D12Device> device;
        Api(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));Api(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)));
        Api(D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
        ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC q{};q.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
        Api(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)));
        ComPtr<ID3D12CommandAllocator> pa,ca;ComPtr<ID3D12GraphicsCommandList> producer,consumer;
        Api(device->CreateCommandAllocator(q.Type,IID_PPV_ARGS(&pa)));Api(device->CreateCommandAllocator(q.Type,IID_PPV_ARGS(&ca)));
        Api(device->CreateCommandList(0,q.Type,pa.Get(),nullptr,IID_PPV_ARGS(&producer)));
        Api(device->CreateCommandList(0,q.Type,ca.Get(),nullptr,IID_PPV_ARGS(&consumer)));
        auto texture=[&](DXGI_FORMAT format){ComPtr<ID3D12Resource> out;D3D12_RESOURCE_DESC d{};
            d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=8;d.Height=4;d.DepthOrArraySize=1;d.MipLevels=1;
            d.SampleDesc.Count=1;d.Format=format;D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
            Api(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                nullptr,IID_PPV_ARGS(&out)));return out;};
        auto depth=texture(DXGI_FORMAT_R32_FLOAT),motion=texture(DXGI_FORMAT_R32G32_FLOAT);
        G::Bridge bridge;bridge.Enable(true);DlssNrFrameInfo frame{};frame.RenderSubrectWidth=8;frame.RenderSubrectHeight=4;
        auto capture=[&]{bridge.Capture(producer.Get(),depth.Get(),motion.Get(),frame,device.Get(),0,16,8,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);};
        auto submit=[&](ID3D12GraphicsCommandList* list){Api(list->Close());ID3D12CommandList* lists[]={list};queue->ExecuteCommandLists(1,lists);};
        capture();auto selection=bridge.BeginPresent();submit(producer.Get());
        auto owner=std::make_shared<GuideOwner>();G::Inputs inputs;
        r.Check("T16.private-guide-lease",bridge.Bind(selection,consumer.Get(),queue.Get(),device.Get(),0,16,8,inputs,0,owner),true,"Bridge::Bind");
        r.Check("T16.guide-submit-fresh",bridge.ValidateLeasedSubmit(selection,consumer.Get(),queue.Get()),true,"Bridge::ValidateLeasedSubmit");
        r.Check("T16.guide-submit-wrong-list",bridge.ValidateLeasedSubmit(selection,producer.Get(),queue.Get()),false,"Bridge::ValidateLeasedSubmit");
        owner->stale=true;
        r.Check("T16.guide-submit-stale",bridge.ValidateLeasedSubmit(selection,consumer.Get(),queue.Get()),false,"Bridge::ValidateLeasedSubmit");
        owner->stale=false;
        bridge.Enable(false);bridge.Enable(true); // owner disable closes admissions, retains outstanding work
        r.Check("T16.guide-admission-closed",bridge.ValidateLeasedSubmit(selection,consumer.Get(),queue.Get()),false,"Bridge::ValidateLeasedSubmit");
        submit(consumer.Get());if(!S::Drain(5000))throw std::runtime_error("fixture drain failed");
        Api(producer->Reset(pa.Get(),nullptr));Api(consumer->Reset(ca.Get(),nullptr));
        // Both command recordings retired, but an owner-reported capture hold still
        // prevents recycling that exact private allocation.
        owner->held=true;capture();auto heldSelection=bridge.BeginPresent();submit(producer.Get());G::Inputs heldInputs;
        r.Check("T19.other-slot-while-held",bridge.Bind(heldSelection,consumer.Get(),queue.Get(),device.Get(),0,16,8,heldInputs),true,"Bridge::Bind");
        r.Check("T19.held-slot-not-recycled",inputs.depth.Get()!=heldInputs.depth.Get(),true,"Bridge::Capture");
        submit(consumer.Get());if(!S::Drain(5000))throw std::runtime_error("second drain failed");
        Api(producer->Reset(pa.Get(),nullptr));Api(consumer->Reset(ca.Get(),nullptr));
        owner->held=false;owner->omitRelease=true;capture();auto missingSelection=bridge.BeginPresent();submit(producer.Get());G::Inputs missing;
        r.Check("T19.missing-release-bind",bridge.Bind(missingSelection,consumer.Get(),queue.Get(),device.Get(),0,16,8,missing),true,"Bridge::Bind");
        r.Check("T19.missing-release-not-recycled",inputs.depth.Get()!=missing.depth.Get(),true,"Bridge::Capture");
        submit(consumer.Get());if(!S::Drain(5000))throw std::runtime_error("missing release drain failed");
        Api(producer->Reset(pa.Get(),nullptr));Api(consumer->Reset(ca.Get(),nullptr));
        owner->omitRelease=false;capture();auto releasedSelection=bridge.BeginPresent();submit(producer.Get());G::Inputs recycled;
        r.Check("T19.released-slot-bind",bridge.Bind(releasedSelection,consumer.Get(),queue.Get(),device.Get(),0,16,8,recycled),true,"Bridge::Bind");
        r.Check("T19.released-slot-recycled",inputs.depth.Get()==recycled.depth.Get(),true,"Bridge::Capture");
        submit(consumer.Get());if(!S::Drain(5000))throw std::runtime_error("final drain failed");
        Api(producer->Reset(pa.Get(),nullptr));Api(consumer->Reset(ca.Get(),nullptr));
#else
        r.Check("T16.guide-adapter-missing",false,true,"NativeLeaseBinding unavailable");
#endif
        return r.Finish();});
}
