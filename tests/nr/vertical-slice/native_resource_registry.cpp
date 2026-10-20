#define NR_GPU_SAFETY_TEST
#include "../../../OptiScaler/dlssnr/NrGpuSafety.cpp"
#define NR_SPECTRE_SOURCE_TESTING
#define NR_SELECTED_SR_TESTING
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <array>
#include <atomic>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <algorithm>
#if __has_include(<nr/lifecycle/NativeResourceRegistry.h>)
#include <nr/lifecycle/NativeOwnerSet.h>
#include <nr/lifecycle/OpaqueSrEvaluationReceipt.h>
#define NR_RESOURCE_REGISTRY_AVAILABLE
#endif
using Microsoft::WRL::ComPtr;
static void Api(HRESULT hr){if(FAILED(hr))throw std::runtime_error("controlled WARP prerequisite failed");}
#ifdef NR_RESOURCE_REGISTRY_AVAILABLE
namespace C=Neurotic::Contracts;namespace L=Neurotic::Lifecycle;namespace S=Neurotic::Context;
namespace Neurotic::Lifecycle
{
class SelectedSrTestAccess
{
  public:
    static bool Enroll(OpaqueSrEvaluationReceipt& receipt){return receipt.EnrollNgxSuperSampling();}
};
class NativeAllocationRecordTestAccess
{
  public:
    static void Representation(NativeAllocationRecord& record,const RepresentationIssuer& issuer){record.representation_=issuer;}
};
class NativeResourceRegistryTestAccess
{
  public:
    static void FailRepresentationAfterResourceIssue(NativeResourceRegistry& value)
    {value.slots_[0].owner.representation=value.slots_[1].owner.representation;}
    static std::uint64_t NextRecord(NativeResourceRegistry& value)
    {return value.slots_[0].owner.journal.Event().evidence.record.value;}
    static void ProjectionRepresentation(NativeResourceRegistry& value,bool valid)
    {NativeAllocationRecordTestAccess::Representation(*value.slots_[0].record,*value.slots_[valid?0:1].owner.representation);}
    static std::size_t ProjectionCount(const NativeResourceRegistry& value){return value.slots_[0].projectionCount;}
    // Synthetic release observer replaces a lookup ref only in this isolated
    // teardown fixture. Actual WARP native/resource ownership is retained.
    static void RetainObserver(NativeResourceRegistry& value,IUnknown* observer)
    {value.slots_[0].identity=observer;}
    static bool Closed(const NativeResourceRegistry& value){return value.closed_;}
    static auto SelectedReserve(NativeResourceRegistry& value,ID3D12Resource* output,
        ID3D12GraphicsCommandList* list,const DlssNr::GpuSafety::LocalRecordingAction& action)
    {return value.ReserveSelectedSrWrite(output,list,action);}
    static bool SelectedCommit(NativeResourceRegistry& value,const NativeResourceRegistry::OpaqueWriteReservation& reservation,
        const DlssNr::GpuSafety::LocalRecordingAction& action,const OpaqueSrEvaluationReceipt& receipt)
    {return value.CommitSelectedSrWrite(reservation,reservation.native,reservation.list,action,receipt);}
    template<class Registry>static constexpr bool HasReadBorrow()
    {return requires(Registry& value,ID3D12Resource* native,const C::ResourceView& view){value.BorrowCurrentRead(native,view);};}
    template<class Registry>static auto Borrow(Registry& value,ID3D12Resource* native,const C::ResourceView& view)
    {
        if constexpr(HasReadBorrow<Registry>())return value.BorrowCurrentRead(native,view);
        else return std::optional<int>{};
    }
    template<class Registry>static constexpr bool HasExplicitTeardown()
    {return requires(Registry& value){value.ReleaseRetained();};}
};
}
static C::Symbol Symbol(const char* text){C::Symbol result;if(!result.Assign(text))throw std::runtime_error("test symbol");return result;}
// Synthetic wrapper with actual retained WARP resource; this is not Streamline evidence.
class Alias final:public IUnknown
{
    std::atomic<ULONG> refs_{1};ComPtr<IUnknown> base_;
  public:
    std::function<void()> query;
    explicit Alias(IUnknown* base):base_(base){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out)override
    {
        if(!out)return E_POINTER;*out=nullptr;
        if(query){auto action=std::move(query);action();}
        if(id==__uuidof(IUnknown)){*out=this;AddRef();return S_OK;}
        if(id==DlssNr::NativeIdentity::streamlineBase)return base_->QueryInterface(__uuidof(IUnknown),out);
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs_;}
    ULONG STDMETHODCALLTYPE Release()override{auto count=--refs_;if(!count)delete this;return count;}
};
class ReleaseObserver final:public IUnknown
{
    ULONG refs_=1;std::function<void()> callback_;
    ~ReleaseObserver(){callback_();}
  public:
    explicit ReleaseObserver(std::function<void()> callback):callback_(std::move(callback)){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out)override
    {if(!out)return E_POINTER;*out=nullptr;if(id!=__uuidof(IUnknown))return E_NOINTERFACE;*out=this;AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs_;}
    ULONG STDMETHODCALLTYPE Release()override{auto count=--refs_;if(!count)delete this;return count;}
};
static ComPtr<ID3D12Resource> Texture(ID3D12Device* device,UINT16 mips=1,UINT16 layers=1)
{
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=16;desc.Height=8;
    desc.DepthOrArraySize=layers;desc.MipLevels=mips;desc.SampleDesc.Count=1;desc.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;ComPtr<ID3D12Resource> result;
    Api(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&result)));
    return result;
}
// Catch stale publication across conflicting writes/closure, lost COM retention,
// and unbounded pin admission using actual WARP allocations and recording actions.
template<class Registry,class Check>void ReadBorrows(ID3D12Device* device,ID3D12CommandAllocator* allocator,
    ID3D12GraphicsCommandList* writer,Check& check)
{
    using Access=L::NativeResourceRegistryTestAccess;
    if constexpr(Access::HasReadBorrow<Registry>())
    {
        auto owners=std::make_unique<L::NativeOwnerSet>(Symbol("native.resource.read"));Registry& table=owners->Resources();
        auto texture=Texture(device),input=Texture(device);const auto* view=table.Observe(texture.Get());
        check(view&&!Access::Borrow(table,texture.Get(),*view),"read pin refuses unknown content");
        Api(writer->Reset(allocator,nullptr));auto ticket=DlssNr::GpuSafety::Record(writer);
        auto action=DlssNr::GpuSafety::BeginLocalAction(ticket,writer,texture.Get());writer->CopyResource(texture.Get(),input.Get());
        check(action&&table.RecordWritten(texture.Get(),writer,*action),"read fixture publishes actual owned copy");
        if(!action||!view)return;
        const auto exact=*view;auto wrong=exact;wrong.raster.active={};
        check(!Access::Borrow(table,texture.Get(),wrong)&&!Access::Borrow(table,input.Get(),exact),"read pin refuses mismatched view and allocation");
        auto pin=Access::Borrow(table,texture.Get(),exact);
        check(pin&&pin->Current()&&pin->Native()==texture.Get()&&pin->View()==exact,"read pin retains exact current full view");
        if(!pin)return;
        check(!table.ReserveOpaqueWrite(texture.Get(),writer,*action)&&pin->Current()&&*view==exact,"generic pre-command write refuses without invalidating read");
        std::vector<decltype(pin)> pins;
        for(std::size_t i=1;i<Registry::ReadBorrowsPerAllocation;++i)pins.push_back(Access::Borrow(table,texture.Get(),exact));
        check(std::all_of(pins.begin(),pins.end(),[](const auto& p){return p&&p->Current();})&&!Access::Borrow(table,texture.Get(),exact),"bounded read capacity refuses excess without evicting live pins");
        auto moved=std::move(*pin);pin.reset();
        check(moved.Current()&&!Access::Borrow(table,texture.Get(),exact),"move transfers one pin without freeing capacity");
        pins.clear();auto replacement=Access::Borrow(table,texture.Get(),exact);
        check(replacement&&replacement->Current(),"released pins return bounded capacity");replacement.reset();
        writer->CopyResource(texture.Get(),input.Get());
        check(!table.RecordWritten(texture.Get(),writer,*action)&&!moved.Current()&&!S::CompleteContent(*view)&&!table.CurrentRegisteredView(texture.Get()),"unexpected recorded write revokes pinned freshness and quarantines allocation");
        action.reset();Api(writer->Close());
        // Each remaining case starts from a real current owned-copy publication.
        for(unsigned scenario=0;scenario<3;++scenario)
        {
            auto nextOwners=std::make_unique<L::NativeOwnerSet>(Symbol("native.resource.read.next"));Registry& next=nextOwners->Resources();
            auto target=Texture(device);const auto* targetView=next.Observe(target.Get());
            Api(writer->Reset(allocator,nullptr));ticket=DlssNr::GpuSafety::Record(writer);
            action=DlssNr::GpuSafety::BeginLocalAction(ticket,writer,target.Get());writer->CopyResource(target.Get(),input.Get());
            check(action&&next.RecordWritten(target.Get(),writer,*action),"read control publishes current content");
            if(!action||!targetView)return;
            const auto snapshot=*targetView;auto retained=Access::Borrow(next,target.Get(),snapshot);
            check(retained&&retained->Current(),"read control acquires current pin");
            if(!retained)return;
            if(scenario==0)
            {
                retained.reset();auto reserved=next.ReserveOpaqueWrite(target.Get(),writer,*action);
                check(reserved.has_value()&&!Access::Borrow(next,target.Get(),snapshot),"RAII release permits reservation and pending write rejects old content");
            }
            else if(scenario==1)
                check(!Access::SelectedReserve(next,target.Get(),writer,*action)&&!retained->Current()&&!S::CompleteContent(*targetView),"selected SR refusal quarantines old pinned content before original call");
            else
            {
                next.CloseAdmissions();
                check(!retained->Current()&&retained->View()==snapshot&&!Access::Borrow(next,target.Get(),snapshot),"close revokes existing pins immediately and blocks admission");
            }
            action.reset();Api(writer->Close());
            if(scenario==2)
            {
                target.Reset();nextOwners.reset();ComPtr<ID3D12Device> retainedDevice;
                check(!retained->Current()&&SUCCEEDED(retained->Native()->GetDevice(IID_PPV_ARGS(&retainedDevice))),"pin safely outlives registry retaining object and historical snapshot");
            }
        }
    }
    else check(false,"canonical Resource owner supplies bounded publication read pin");
}
template<class Registry,class Check>void Projections(Registry& registry,const C::ResourceView* base,Check& check)
{
    if constexpr(requires{registry.ProjectRectangle(base,C::Rectangle{});})
    {
        const auto original=*base;const C::Rectangle active{2,1,8,4};
        const auto* view=registry.ProjectRectangle(base,active);
        check(view&&S::CompleteResourceStructure(*view)&&S::ValidRaster(view->raster),"resource owner establishes bounded active view");
        if(!view)return;
        check(view->raster.active.KnownPart()->value==active&&view->descriptor==base->descriptor&&
              view->device==base->device&&view->mip==base->mip,"projection preserves physical descriptor and exact requested rect");
        check(view->identity.objectIncarnation==base->identity.objectIncarnation&&
              view->identity.resourceIncarnation==base->identity.resourceIncarnation&&
              view->identity.resourceGeneration==base->identity.resourceGeneration&&
              view->identity.resourceViewIncarnation!=base->identity.resourceViewIncarnation&&
              view->identity.representationGeneration!=base->identity.representationGeneration,
              "new active structure gets distinct view and representation identity");
        check(!view->identity.contentRevision.IsKnown()&&*base==original,"projection invents no write or mutation of full view");
        check(registry.ProjectRectangle(base,active)==view,"same active rectangle retains exact identity");
        check(registry.ProjectRectangle(base,{0,0,16,8})==base,"whole rectangle reuses genuine physical view");
        auto copy=*base;
        check(!registry.ProjectRectangle(&copy,active)&&!registry.ProjectRectangle(nullptr,active),"unregistered or null view refuses projection");
        check(!registry.ProjectRectangle(view,{0,0,4,2}),"projection cannot masquerade as registered base");
        check(!registry.ProjectRectangle(base,{0,0,0,1})&&!registry.ProjectRectangle(base,{0,0,1,0}),"empty active rectangle refuses");
        check(!registry.ProjectRectangle(base,{16,0,1,1})&&!registry.ProjectRectangle(base,{0,8,1,1})&&
              !registry.ProjectRectangle(base,{0xffffffffu,0,2,1})&&!registry.ProjectRectangle(base,{0,0xffffffffu,1,2}),
              "out of bounds and overflowing active rectangle refuses");
        for(unsigned i=1;i<Registry::ProjectionsPerAllocation;++i)
            check(registry.ProjectRectangle(base,{i,0,1,1})!=nullptr,"bounded projection capacity accepts exact remaining views");
        check(!registry.ProjectRectangle(base,{0,0,2,2})&&registry.ProjectRectangle(base,active)==view,
              "projection exhaustion cannot recycle live identities");
        check(*base==original&&!view->identity.contentRevision.IsKnown(),"projection exhaustion preserves physical and content truth");
    }
    else check(false,"resource owner establishes bounded active view");
}
template<class Registry,class Check>void ClosedProjection(Registry& registry,const C::ResourceView* view,Check& check)
{
    if constexpr(requires{registry.ProjectRectangle(view,C::Rectangle{});})
        check(!registry.ProjectRectangle(view,{0,0,16,8})&&!registry.ProjectRectangle(view,{2,1,8,4}),
              "closed owner refuses existing whole and active projections");
}
template<class Registry>bool RecordedWrite(Registry& registry,ID3D12Resource* resource,
    ID3D12GraphicsCommandList* list,const DlssNr::GpuSafety::LocalRecordingAction& action)
{
    if constexpr(requires{registry.RecordWritten(resource,list,action);})
        return registry.RecordWritten(resource,list,action);
    else return false;
}
#endif
int main()
{
    int count=0,failed=0;const auto check=[&](bool value,const char* text){++count;if(!value){++failed;std::cerr<<"FAIL "<<text<<'\n';}};
#ifdef NR_RESOURCE_REGISTRY_AVAILABLE
    ComPtr<IDXGIFactory4> factory;ComPtr<IDXGIAdapter> warp;ComPtr<ID3D12Device> device,foreign;
    Api(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));Api(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)));
    Api(D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
    Api(D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&foreign)));
    check(DlssNr::NativeIdentity::CompareDevices(device.Get(),foreign.Get()).equal,
        "repeated D3D12 device creation on same adapter is the same device");
    foreign.Reset();
#ifndef NR_LAB_WARP_ONLY
    for(UINT index=0;;++index)
    {
        ComPtr<IDXGIAdapter1> candidate;
        if(factory->EnumAdapters1(index,&candidate)==DXGI_ERROR_NOT_FOUND)break;
        if(!candidate)continue;
        if(SUCCEEDED(D3D12CreateDevice(candidate.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&foreign)))&&
            !DlssNr::NativeIdentity::CompareDevices(device.Get(),foreign.Get()).equal)break;
        foreign.Reset();
    }
#endif
    auto owners=std::make_unique<L::NativeOwnerSet>(Symbol("native.resource.test"));auto& registry=owners->Resources();
    check(owners->Ready()&&registry.Used()==0,"one cold empty registry");
    check(!registry.Observe(nullptr)&&registry.Used()==0,"null refuses before slot use");
    auto texture=Texture(device.Get());const auto* view=registry.Observe(texture.Get());
    check(view&&registry.Used()==1,"actual retained resource registration");if(!view)return 1;
    check(S::CompleteResourceStructure(*view)&&!S::CompleteContent(*view),"complete structure does not invent current content");
    check(view->descriptor.allocation.KnownPart()->value==C::Extent{16,8}&&
        view->descriptor.api.KnownPart()->value==C::GraphicsApi::D3D12&&view->descriptor.format.KnownPart()->value==Symbol("DXGI.Format.10"),
        "exact descriptor from actual resource");
    check(view->mip.KnownPart()->value==0&&view->plane.KnownPart()->value==0&&view->arrayLayer.KnownPart()->value==0&&
        view->raster.active.KnownPart()->value==C::Rectangle{0,0,16,8},"exact supported whole-resource view");
    Projections(registry,view,check);
    const auto identity=view->identity;
    check(registry.Observe(texture.Get())==view&&registry.Used()==1&&view->identity==identity,"same lifetime never rekeys across observations");
    ComPtr<Alias> alias;alias.Attach(new Alias(texture.Get()));
    check(registry.Observe(alias.Get())==view&&registry.Used()==1,"normalized wrapped alias deduplicates same lifetime");
    const auto cpuSnapshot=registry.CurrentRegisteredView(texture.Get());
    check(cpuSnapshot&&*cpuSnapshot==*view,"return snapshot reads exact enrolled CPU metadata");
    bool snapshotQueried=false;alias->query=[&]{snapshotQueried=true;};
    check(!registry.CurrentRegisteredView(reinterpret_cast<ID3D12Resource*>(alias.Get()))&&!snapshotQueried,
        "return snapshot performs no COM discovery for unregistered pointer");
    alias->query={};
    auto other=Texture(device.Get());const auto* second=registry.Observe(other.Get());
    check(second&&second->identity!=identity&&second->device==view->device&&registry.Used()==2,"different physical allocation has distinct identity same device");
    ComPtr<ID3D12CommandAllocator> writerAllocator;ComPtr<ID3D12GraphicsCommandList> writer;
    Api(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&writerAllocator)));
    Api(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,writerAllocator.Get(),nullptr,IID_PPV_ARGS(&writer)));
    const auto firstSeen=DlssNr::GpuSafety::Record(writer.Get());
    check(firstSeen&&!DlssNr::GpuSafety::BeginLocalAction(firstSeen,writer.Get(),other.Get()),
        "first-seen recording does not imply observed open state");
    Api(writer->Close());Api(writer->Reset(writerAllocator.Get(),nullptr));
    auto recording=DlssNr::GpuSafety::Record(writer.Get());
    auto action=DlssNr::GpuSafety::BeginLocalAction(recording,writer.Get(),other.Get());
    check(action&&action->Current()&&!S::CompleteContent(*second),"recorded-write fixture has live D3D12 owner and no invented content");
    writer->CopyResource(other.Get(),texture.Get());
    check(action&&!RecordedWrite(registry,texture.Get(),writer.Get(),*action)&&!S::CompleteContent(*view),
        "target recording action cannot stamp a different same-device resource");
    const bool written=action&&RecordedWrite(registry,other.Get(),writer.Get(),*action);
    check(written&&S::CompleteContent(*second),"actual owned copy advances resource content version");
    action.reset();Api(writer->Close());
    auto opaqueOwners=std::make_unique<L::NativeOwnerSet>(Symbol("native.resource.opaque"));
    auto& opaqueRegistry=opaqueOwners->Resources();auto opaqueOutput=Texture(device.Get());
    const auto* opaqueView=opaqueRegistry.Observe(opaqueOutput.Get());
    check(opaqueView&&!S::CompleteContent(*opaqueView),"opaque Output starts with unknown content");
    Api(writer->Reset(writerAllocator.Get(),nullptr));
    auto opaqueTicket=DlssNr::GpuSafety::Record(writer.Get());
    auto opaqueAction=DlssNr::GpuSafety::BeginLocalAction(opaqueTicket,writer.Get(),opaqueOutput.Get());
    check(opaqueAction&&opaqueAction->CurrentFor(opaqueOutput.Get()),"opaque call reserves on exact Output recording");
    const bool opaqueReservation=[&]<class R>(R& owner)
    {
        if constexpr(requires{owner.ReserveOpaqueWrite(opaqueOutput.Get(),writer.Get(),*opaqueAction);})
        {
            auto reserved=owner.ReserveOpaqueWrite(opaqueOutput.Get(),writer.Get(),*opaqueAction);
            check(reserved.has_value()&&!S::CompleteContent(*opaqueView),
                "reserved possible effect does not publish Resource content");
            check(!owner.ReserveOpaqueWrite(opaqueOutput.Get(),writer.Get(),*opaqueAction),
                "duplicate reservation cannot overwrite an uncertain operation");
            if(!reserved)return false;
            using Receipt=L::OpaqueSrEvaluationReceipt;
            Receipt::Entry entry;entry.featureHandle=reinterpret_cast<void*>(0x1000);
            entry.featureGeneration=7;entry.nativeList=writer.Get();entry.recordingIncarnation=8;
            entry.parameters=reinterpret_cast<void*>(0x2000);entry.entrySequence=20;
            entry.workOrdinal=4;entry.completeCoverage=true;
            for(std::size_t i=0;i<3;++i)entry.inputs[i]={other.Get(),second->identity};
            entry.output={opaqueOutput.Get(),opaqueView->identity};entry.outputRegion={0,0,16,8};
            entry.reservedRevision=reserved->revision;
            Receipt receipt(entry);check(receipt.MarkEntered(),"reserved Resource write enters one opaque operation");
            writer->CopyResource(opaqueOutput.Get(),other.Get());
            Receipt::Return returned;returned.featureHandle=entry.featureHandle;
            returned.featureGeneration=entry.featureGeneration;returned.nativeList=entry.nativeList;
            returned.recordingIncarnation=entry.recordingIncarnation;returned.parameters=entry.parameters;
            returned.inputs=entry.inputs;returned.output=entry.output;returned.outputRegion=entry.outputRegion;
            returned.outputSubresource=entry.outputSubresource;returned.returnSequence=21;
            returned.workOrdinal=5;returned.completeCoverage=true;returned.ngxResult=0;returned.ngxSuccess=0;
            check(receipt.Seal(returned)==Receipt::Class::RecordedPossibleEffect,
                "generic receipt cannot authenticate the fixture's specific copy");
            check(!owner.CommitOpaqueWrite(*reserved,other.Get(),writer.Get(),*opaqueAction,receipt),
                "foreign Output cannot commit reserved revision");
            const bool committed=owner.CommitOpaqueWrite(*reserved,opaqueOutput.Get(),writer.Get(),*opaqueAction,receipt);
            check(!committed&&!S::CompleteContent(*opaqueView),
                "work and success cannot commit a caller Output revision");
            check(!owner.CommitOpaqueWrite(*reserved,opaqueOutput.Get(),writer.Get(),*opaqueAction,receipt),
                "duplicate opaque commit refused");
            check(!owner.RecordWritten(opaqueOutput.Get(),writer.Get(),*opaqueAction),
                "uncertain reservation prevents a retry from erasing possible effects");
            return !committed;
        }
        else return false;
    }(opaqueRegistry);
    check(opaqueReservation,"Resource owner retains uncertainty without opaque write publication");
    opaqueAction.reset();Api(writer->Close());
    {
        // Native Before already owns the Color recording action. The selected
        // SR Output join must reuse it; a second local action is forbidden.
        auto output=Texture(device.Get());const auto* outputView=opaqueRegistry.Observe(output.Get());
        Api(writer->Reset(writerAllocator.Get(),nullptr));auto ticket=DlssNr::GpuSafety::Record(writer.Get());
        auto held=DlssNr::GpuSafety::BeginLocalAction(ticket,writer.Get(),other.Get());
        check(held&&!DlssNr::GpuSafety::BeginLocalAction(ticket,writer.Get(),output.Get()),"held Before action excludes nested recording actions");
        auto reserved=L::NativeResourceRegistryTestAccess::SelectedReserve(opaqueRegistry,output.Get(),writer.Get(),*held);
        check(reserved.has_value(),"selected SR Output reservation reuses the existing Color action");
        if(!reserved)return 1;
        using R=L::OpaqueSrEvaluationReceipt;R::Entry entry;
        entry.featureHandle=reinterpret_cast<void*>(7);entry.featureGeneration=1;entry.nativeList=writer.Get();
        entry.recordingIncarnation=1;entry.parameters=reinterpret_cast<void*>(8);entry.entrySequence=1;entry.completeCoverage=true;
        for(std::size_t i=0;i<3;++i)entry.inputs[i]={other.Get(),second->identity};
        entry.output={output.Get(),outputView->identity};entry.outputRegion={0,0,16,8};entry.reservedRevision=reserved->revision;
        R receipt(entry);check(L::SelectedSrTestAccess::Enroll(receipt)&&receipt.MarkEntered(),"selected operation enters with retained Resource inputs");
        writer->CopyResource(output.Get(),other.Get());
        R::Return returned;returned.featureHandle=entry.featureHandle;returned.featureGeneration=entry.featureGeneration;
        returned.nativeList=entry.nativeList;returned.recordingIncarnation=entry.recordingIncarnation;returned.parameters=entry.parameters;
        returned.inputs=entry.inputs;returned.output=entry.output;returned.outputRegion=entry.outputRegion;
        returned.returnSequence=2;returned.workOrdinal=1;returned.completeCoverage=true;
        receipt.Seal(returned);
        check(!opaqueRegistry.CommitOpaqueWrite(*reserved,output.Get(),writer.Get(),*held,receipt),"ordinary Color action gains no general Output authority");
        check(L::NativeResourceRegistryTestAccess::SelectedCommit(opaqueRegistry,*reserved,*held,receipt)&&S::CompleteContent(*outputView),
            "private selected SR operation joins the actual Output write through the same Resource owner");
        held.reset();Api(writer->Close());
    }
    ReadBorrows<L::NativeResourceRegistry>(device.Get(),writerAllocator.Get(),writer.Get(),check);
    if(foreign)
    {
        auto elsewhere=Texture(foreign.Get());check(!registry.Observe(elsewhere.Get())&&registry.Used()==2,"actual foreign device refused");
        std::cout<<"foreign-device API control: EXECUTED; no commands submitted\n";
    }
    else std::cout<<"foreign-device API control: NOT_RUN; no distinct D3D12 adapter/device available\n";
    auto mipped=Texture(device.Get(),2),array=Texture(device.Get(),1,2);
    check(!registry.Observe(mipped.Get())&&!registry.Observe(array.Get())&&registry.Used()==2,"unsupported subresource views refuse before allocation");
    // Wrapper QI reenters registry. The inner registration wins and outer deduplicates.
    auto reentrantTexture=Texture(device.Get());ComPtr<Alias> reentrant;reentrant.Attach(new Alias(reentrantTexture.Get()));
    const C::ResourceView* inner=nullptr;reentrant->query=[&]{inner=registry.Observe(reentrantTexture.Get());};
    const auto* outer=registry.Observe(reentrant.Get());
    check(inner&&outer==inner&&registry.Used()==3,"COM reentry deduplicates without locked external calls");
    auto raw=texture.Get();texture.Reset();alias.Reset();ComPtr<ID3D12Device> retained;
    Api(raw->GetDevice(IID_PPV_ARGS(&retained)));
    check(registry.Observe(raw)==view&&retained.Get()==device.Get(),"registry retains physical lifetime after caller drops references");
    for(std::size_t i=registry.Used();i<L::NativeOwnerSet::AllocationSlots;++i)
    {auto next=Texture(device.Get());check(registry.Observe(next.Get())!=nullptr,"free capacity registers distinct live allocation");}
    auto overflow=Texture(device.Get());check(!registry.Observe(overflow.Get())&&registry.Used()==L::NativeOwnerSet::AllocationSlots,
        "capacity refuses without reusing live issuer");
    check(registry.Observe(raw)==view&&view->identity==identity,"capacity exhaustion preserves existing identity");
    registry.CloseAdmissions();check(!registry.Observe(raw)&&!registry.Observe(overflow.Get()),"closed registry refuses old and new registrations");
    check(!registry.CurrentRegisteredView(raw),"return snapshot refuses revoked registry");
    ClosedProjection(registry,view,check);
    check(view->identity==identity&&!view->identity.contentRevision.IsKnown(),"closure retains immutable descriptions without fake content");
    auto closing=std::make_unique<L::NativeOwnerSet>(Symbol("native.resource.close"));ComPtr<Alias> closeAlias;
    closeAlias.Attach(new Alias(raw));closeAlias->query=[&]{closing->Resources().CloseAdmissions();};
    check(!closing->Resources().Observe(closeAlias.Get())&&closing->Resources().Used()==0,"reentrant closure wins before identity issue");
    check(!std::is_default_constructible_v<L::NativeResourceRegistry>&&!std::is_copy_constructible_v<L::NativeResourceRegistry>,
        "only canonical owner set constructs registry");
    using Access=L::NativeResourceRegistryTestAccess;
    auto partial=std::make_unique<L::NativeOwnerSet>(Symbol("native.resource.partial"));
    Access::FailRepresentationAfterResourceIssue(partial->Resources());auto partialTexture=Texture(device.Get());
    auto* partialRaw=partialTexture.Get();
    check(!partial->Resources().Observe(partialRaw)&&partial->Resources().Used()==1,"partial issuance consumes slot and refuses view");
    const auto sequence=Access::NextRecord(partial->Resources());partialTexture.Reset();
    check(!partial->Resources().Observe(partialRaw)&&partial->Resources().Used()==1&&
        Access::NextRecord(partial->Resources())==sequence+1,"partial issuance never retries or reissues same retained lifetime");
    ComPtr<ID3D12Device> partialDevice;Api(partialRaw->GetDevice(IID_PPV_ARGS(&partialDevice)));
    check(partialDevice.Get()==device.Get(),"partial issuance still retains actual resource lifetime");
    auto projectionFailure=std::make_unique<L::NativeOwnerSet>(Symbol("native.resource.projection.failure"));
    auto& projected=projectionFailure->Resources();const auto* projectionBase=projected.Observe(raw);
    const auto* firstProjection=projected.ProjectRectangle(projectionBase,{0,0,2,2});
    check(firstProjection!=nullptr,"projection partial-issuance fixture has valid positive control");
    Access::ProjectionRepresentation(projected,false);
    check(!projected.ProjectRectangle(projectionBase,{0,0,3,3})&&Access::ProjectionCount(projected)==2,
          "failure after view issuance consumes projection reservation");
    Access::ProjectionRepresentation(projected,true);const auto projectionSequence=Access::NextRecord(projected);
    check(!projected.ProjectRectangle(projectionBase,{0,0,3,3})&&Access::ProjectionCount(projected)==2&&
          Access::NextRecord(projected)==projectionSequence+1,"partial projection cannot retry after owner recovery");
    const auto* nextProjection=projected.ProjectRectangle(projectionBase,{0,0,4,4});
    check(firstProjection&&nextProjection&&nextProjection->identity.resourceViewIncarnation.KnownPart()->value.value==
          firstProjection->identity.resourceViewIncarnation.KnownPart()->value.value+2,
          "unpublished partial view identity stays consumed");
    // Do not execute the known-unsafe implicit-destructor reentry. The red test
    // fails the absent explicit teardown; green then exercises real destruction.
    const bool explicitTeardown=Access::HasExplicitTeardown<L::NativeResourceRegistry>();
    check(explicitTeardown,"registry provides explicit retained-reference teardown");
    if(explicitTeardown)
    {
        auto teardown=std::make_unique<L::NativeOwnerSet>(Symbol("native.resource.teardown"));auto& table=teardown->Resources();
        check(table.Observe(raw)!=nullptr,"teardown fixture registers actual WARP lifetime");
        bool released=false,closedDuringRelease=false,reentryRefused=false;ComPtr<IUnknown> observer;
        observer.Attach(new ReleaseObserver([&]{released=true;closedDuringRelease=Access::Closed(table);
            if(closedDuringRelease){table.CloseAdmissions();reentryRefused=table.Observe(raw)==nullptr;}}));
        Access::RetainObserver(table,observer.Get());observer.Reset();teardown.reset();
        check(released&&closedDuringRelease&&reentryRefused,"Release reentry sees closed registry with live synchronization");
    }
#else
    check(false,"authentic retained resource registry exists");
#endif
    std::cout<<"native-resource-registry checks="<<count<<" failed="<<failed<<"; WARP and optional distinct-device API control; synthetic wrapper; no model or GPU command submission\n";
    return failed?1:0;
}
