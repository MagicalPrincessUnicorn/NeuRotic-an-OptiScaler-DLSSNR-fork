#pragma once
// Product-local adapters. Only POD values and opaque SDK-owned handles cross the DLL.
// Authentication establishes origin and interface consistency, never C04 qualification.
#include "../../../external/FidelityFX-CheckedClosure/CheckedControlledService.h"
#include "../../../external/FidelityFX-CheckedClosure/CheckedAlgorithm.h"
#include <Windows.h>
#include <atomic>
#include <memory>

namespace Neurotic::Lifecycle {
class Fsr3ControlledModule final : public std::enable_shared_from_this<Fsr3ControlledModule> {
    HMODULE module_{};
    FfxNrControlledServiceV1 service_{};
    FfxNrControlledContractV1 contract_{};
    Fsr3ControlledModule(HMODULE module,const FfxNrControlledServiceV1& service,const FfxNrControlledContractV1& contract):module_(module),service_(service),contract_(contract){}
    template<class T> static bool InModule(HMODULE module,T function) noexcept {
        if(!function)return false;
        MEMORY_BASIC_INFORMATION region{};
        if(!VirtualQuery(reinterpret_cast<LPCVOID>(function),&region,sizeof(region))||region.AllocationBase!=module||
           region.State!=MEM_COMMIT||region.Type!=MEM_IMAGE||
           !(region.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))||
           (region.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
        HMODULE owner{};
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(function),&owner))return false;
        const bool same=owner==module;FreeLibrary(owner);return same;
    }
    static bool Valid(const FfxNrContextTicketV1& c) noexcept {
        return c.owner_id&&c.incarnation&&c.registration_generation;
    }
    static bool Same(const FfxNrContextTicketV1& a,const FfxNrContextTicketV1& b) noexcept {
        return a.owner_id==b.owner_id&&a.incarnation==b.incarnation&&a.registration_generation==b.registration_generation;
    }
    static bool Same(const FfxNrResourceDescriptionV1& a,const FfxNrResourceDescriptionV1& b) noexcept {
        return a.dimension==b.dimension&&a.alignment==b.alignment&&a.width==b.width&&a.height==b.height&&
            a.depth_or_array_size==b.depth_or_array_size&&a.mip_levels==b.mip_levels&&a.format==b.format&&
            a.sample_count==b.sample_count&&a.sample_quality==b.sample_quality&&a.layout==b.layout&&a.flags==b.flags;
    }
    static D3D12_RESOURCE_DESC Description(const FfxNrResourceDescriptionV1& d) noexcept {
        D3D12_RESOURCE_DESC r{};r.Dimension=static_cast<D3D12_RESOURCE_DIMENSION>(d.dimension);r.Alignment=d.alignment;
        r.Width=d.width;r.Height=d.height;r.DepthOrArraySize=d.depth_or_array_size;r.MipLevels=d.mip_levels;
        r.Format=static_cast<DXGI_FORMAT>(d.format);r.SampleDesc={d.sample_count,d.sample_quality};
        r.Layout=static_cast<D3D12_TEXTURE_LAYOUT>(d.layout);r.Flags=static_cast<D3D12_RESOURCE_FLAGS>(d.flags);return r;
    }
public:
    struct Facts {
        HMODULE creatingModule{};FfxNrContextTicketV1 context{};
        FfxNrOwnedOutputSnapshotV1 output{};FfxNrAlgorithmSnapshotV1 algorithm{};
        FfxNrControlledContractV1 contract{};
    };
    struct Binding {
        Facts facts;
        std::shared_ptr<ffx::nr::OwnedOutputLease> output;
        std::shared_ptr<ffx::nr::AlgorithmLease> algorithm;
        explicit operator bool()const noexcept{return output&&algorithm;}
    };
    ~Fsr3ControlledModule(){if(module_)FreeLibrary(module_);}
    Fsr3ControlledModule(const Fsr3ControlledModule&)=delete;
    Fsr3ControlledModule& operator=(const Fsr3ControlledModule&)=delete;
    HMODULE Module()const noexcept{return module_;}
    const FfxNrControlledServiceV1& Service()const noexcept{return service_;}
    const FfxNrControlledContractV1& Contract()const noexcept{return contract_;}
    static std::shared_ptr<Fsr3ControlledModule> Authenticate(HMODULE exactCreatingModule,
        const FfxNrControlledServiceV1* claimedTable,bool manualOptIn) {
        if(!manualOptIn||!exactCreatingModule)return {};
        using Query=FfxNrStatusV1(FFX_NR_CALL*)(FfxNrControlledServiceV1*);
        const auto query=reinterpret_cast<Query>(GetProcAddress(exactCreatingModule,"ffxNeuRoticQueryControlledServiceV1"));
        if(!InModule(exactCreatingModule,query))return {};
        HMODULE retained{};
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(query),&retained))return {};
        struct Guard{HMODULE m;~Guard(){if(m)FreeLibrary(m);}} guard{retained};
        if(retained!=exactCreatingModule)return {};
        FfxNrControlledServiceV1 table{};table.size=sizeof(table);table.version=1;table.manual_opt_in=FFX_NR_CONTROLLED_OPT_IN_V1;
        if(query(&table)!=FFX_NR_COMPLETE||table.size!=sizeof(table)||table.version!=1||table.manual_opt_in!=FFX_NR_CONTROLLED_OPT_IN_V1)return {};
        if(claimedTable&&(claimedTable->size!=sizeof(table)||claimedTable->version!=1||claimedTable->manual_opt_in!=table.manual_opt_in))return {};
#define NR_CHECK_CONTROLLED_FUNCTION(name) if(!InModule(retained,table.name)||(claimedTable&&claimedTable->name!=table.name))return {};
        NR_CHECK_CONTROLLED_FUNCTION(get_context) NR_CHECK_CONTROLLED_FUNCTION(select_algorithm)
        NR_CHECK_CONTROLLED_FUNCTION(set_submit_observer) NR_CHECK_CONTROLLED_FUNCTION(enroll)
        NR_CHECK_CONTROLLED_FUNCTION(write_owned_output) NR_CHECK_CONTROLLED_FUNCTION(finish_writer)
        NR_CHECK_CONTROLLED_FUNCTION(current_dispatch) NR_CHECK_CONTROLLED_FUNCTION(validate_dispatch)
        NR_CHECK_CONTROLLED_FUNCTION(begin) NR_CHECK_CONTROLLED_FUNCTION(poll) NR_CHECK_CONTROLLED_FUNCTION(read_domains) NR_CHECK_CONTROLLED_FUNCTION(consume)
        NR_CHECK_CONTROLLED_FUNCTION(output_retain) NR_CHECK_CONTROLLED_FUNCTION(output_release) NR_CHECK_CONTROLLED_FUNCTION(output_inspect)
        NR_CHECK_CONTROLLED_FUNCTION(output_begin_writer_action) NR_CHECK_CONTROLLED_FUNCTION(output_begin_consumer_action)
        NR_CHECK_CONTROLLED_FUNCTION(output_end_action) NR_CHECK_CONTROLLED_FUNCTION(output_close_admission)
        NR_CHECK_CONTROLLED_FUNCTION(output_inspect_retirement) NR_CHECK_CONTROLLED_FUNCTION(output_inspect_submission)
        NR_CHECK_CONTROLLED_FUNCTION(algorithm_retain) NR_CHECK_CONTROLLED_FUNCTION(algorithm_release)
        NR_CHECK_CONTROLLED_FUNCTION(algorithm_inspect) NR_CHECK_CONTROLLED_FUNCTION(algorithm_owns)
        NR_CHECK_CONTROLLED_FUNCTION(algorithm_close_admission) NR_CHECK_CONTROLLED_FUNCTION(algorithm_release_after_drain)
        NR_CHECK_CONTROLLED_FUNCTION(query_contract)
#undef NR_CHECK_CONTROLLED_FUNCTION
        FfxNrControlledContractV1 contract{};contract.size=sizeof(contract);contract.version=1;
        if(table.query_contract(&contract)!=FFX_NR_COMPLETE||contract.size!=sizeof(contract)||contract.version!=1||
           contract.contract_id!=FFX_NR_CONTROLLED_CONTRACT_ID_V1||contract.contract_revision!=1||
           contract.machine!=0x8664||contract.pointer_bytes!=sizeof(void*)||contract.route_flags!=7)return {};
        auto* result=new Fsr3ControlledModule(retained,table,contract);guard.m=nullptr;
        return std::shared_ptr<Fsr3ControlledModule>(result);
    }
private:
    class Algorithm;
    class Output final:public ffx::nr::OwnedOutputLease {
        friend class Fsr3ControlledModule;
        friend class Algorithm;
        std::shared_ptr<Fsr3ControlledModule> module_;FfxNrOwnedOutputHandleV1 handle_{};
        FfxNrOwnedOutputSnapshotV1 original_{};mutable std::atomic_bool failed_{false};
        bool Check(FfxNrStatusV1 s)const noexcept{if(s!=FFX_NR_COMPLETE)failed_=true;return s==FFX_NR_COMPLETE;}
    public:
        Output(std::shared_ptr<Fsr3ControlledModule> m,FfxNrOwnedOutputHandleV1 h,const FfxNrOwnedOutputSnapshotV1& s):module_(std::move(m)),handle_(h),original_(s){}
        ~Output()override{module_->service_.output_release(handle_);}
        ffx::nr::OwnedOutputSnapshot inspect()const noexcept override {
            FfxNrOwnedOutputSnapshotV1 s{};ffx::nr::OwnedOutputSnapshot r{};
            if(!Check(module_->service_.output_inspect(handle_,&s))||!Same(s.context,original_.context)||
                s.resource!=original_.resource||s.allocation_generation!=original_.allocation_generation||s.queue!=original_.queue||
                !Same(s.description,original_.description)){failed_=true;r.failed=true;return r;}
            r.context=s.context;r.resource=reinterpret_cast<ID3D12Resource*>(s.resource);r.description=Description(s.description);
            r.allocationGeneration=s.allocation_generation;r.queue=reinterpret_cast<ID3D12CommandQueue*>(s.queue);
            r.writerList=reinterpret_cast<ID3D12GraphicsCommandList*>(s.writer_list);r.writerRecording=s.writer_recording;
            r.consumerList=reinterpret_cast<ID3D12GraphicsCommandList*>(s.consumer_list);r.consumerRecording=s.consumer_recording;
            r.writerActive=s.writer_active==1;r.writerSubmitted=s.writer_submitted==1;r.writerRetired=s.writer_retired==1;
            r.callbackActive=s.callback_active==1;r.admissionOpen=s.admission_open==1;r.actionActive=s.action_active==1;
            r.sdkComplete=s.sdk_complete==1;r.failed=s.failed!=0||failed_.load();
            r.ordinaryEscapeExcluded=s.ordinary_escape_excluded==1;r.recyclingExcluded=s.recycling_excluded==1;return r;
        }
        bool beginWriterAction(ID3D12GraphicsCommandList* list,uint64_t generation)noexcept override {
            return !inspect().failed&&Check(module_->service_.output_begin_writer_action(handle_,reinterpret_cast<uintptr_t>(list),generation));
        }
        bool beginConsumerAction(const FfxNrDispatchTicketV1& d)noexcept override {
            return !inspect().failed&&Check(module_->service_.output_begin_consumer_action(handle_,&d));
        }
        void endAction()noexcept override{Check(module_->service_.output_end_action(handle_));}
        void closeAdmission()noexcept override{Check(module_->service_.output_close_admission(handle_));}
        ffx::nr::OwnedOutputRetirement inspectRetirement()const noexcept override {
            FfxNrOwnedOutputRetirementV1 r{};
            if(inspect().failed)return {false,false,false,true};
            if(!Check(module_->service_.output_inspect_retirement(handle_,&r)))return {false,false,false,true};
            return {r.admission_closed==1,r.actions_quiescent==1,r.sdk_complete==1,r.failed!=0||failed_.load()};
        }
        bool inspectSubmission(FfxNrSubmitResultV1& result)const noexcept override {
            result={};result.size=sizeof(result);result.version=1;
            const auto status=module_->service_.output_inspect_submission(handle_,&result);
            // A submission may legitimately not have occurred yet; preserve pending.
            if(status==FFX_NR_PENDING)return false;
            return Check(status)&&!failed_.load()&&result.size==sizeof(result)&&result.version==1;
        }
    };
    class Algorithm final:public ffx::nr::AlgorithmLease {
        std::shared_ptr<Fsr3ControlledModule> module_;FfxNrAlgorithmHandleV1 handle_{};
        std::shared_ptr<Output> output_;FfxNrAlgorithmSnapshotV1 original_{};mutable std::atomic_bool failed_{false};
        bool Check(FfxNrStatusV1 s)const noexcept{if(s!=FFX_NR_COMPLETE)failed_=true;return s==FFX_NR_COMPLETE;}
    public:
        Algorithm(std::shared_ptr<Fsr3ControlledModule> m,FfxNrAlgorithmHandleV1 h,std::shared_ptr<Output> o,const FfxNrAlgorithmSnapshotV1& s):
            module_(std::move(m)),handle_(h),output_(std::move(o)),original_(s){}
        ~Algorithm()override{module_->service_.algorithm_release(handle_);}
        ffx::nr::AlgorithmSnapshot inspect()const noexcept override {
            FfxNrAlgorithmSnapshotV1 s{};ffx::nr::AlgorithmSnapshot r{};
            if(!Check(module_->service_.algorithm_inspect(handle_,&s))||!Same(s.swapchain,original_.swapchain)||
               s.algorithm_generation!=original_.algorithm_generation||s.allocation_generation!=original_.allocation_generation||s.input!=original_.input){failed_=true;r.failed=true;return r;}
            r.swapchain=s.swapchain;r.algorithmGeneration=s.algorithm_generation;r.allocationGeneration=s.allocation_generation;
            r.input=reinterpret_cast<ID3D12Resource*>(s.input);r.registrationCreated=s.registration_created==1;
            r.registrationComplete=s.registration_complete==1;r.active=s.active==1;r.failed=s.failed!=0||failed_.load();
            r.admissionClosed=s.admission_closed==1;r.destroyAttempted=s.destroy_attempted==1;r.released=s.released==1;return r;
        }
        bool Owns(const std::shared_ptr<ffx::nr::OwnedOutputLease>& output)const noexcept override {
            if(output.get()!=output_.get()||inspect().failed||output_->inspect().failed)return false;
            uint32_t owns{};return Check(module_->service_.algorithm_owns(handle_,output_->handle_,&owns))&&owns==1;
        }
        void closeAdmission()noexcept override{Check(module_->service_.algorithm_close_admission(handle_));}
        FfxNrStatusV1 releaseAfterDrain(const FfxNrDrainHandleV1& d,uint64_t revision,ffx::nr::AlgorithmReleaseReceipt& result)noexcept override {
            result={};FfxNrAlgorithmReleaseReceiptV1 r{};
            if(!Same(d.context,original_.swapchain)||!d.drain_id||!d.closed_epoch||!revision)return FFX_NR_INVALID_ARGUMENT;
            if(inspect().failed)return FFX_NR_API_FAILURE_RETAINED;
            const auto status=module_->service_.algorithm_release_after_drain(handle_,&d,revision,&r);
            result.algorithmGeneration=r.algorithm_generation;result.drain=r.drain;result.evidenceRevision=r.evidence_revision;
            result.cleanupOperations=r.cleanup_operations;result.actualResult=r.actual_result;result.wholeContextDestroyed=r.whole_context_destroyed==1;
            if(status==FFX_NR_COMPLETE&&(r.algorithm_generation!=original_.algorithm_generation||!Same(r.drain.context,d.context)||
                r.drain.drain_id!=d.drain_id||r.drain.closed_epoch!=d.closed_epoch||r.evidence_revision!=revision||
                r.actual_result!=0||r.whole_context_destroyed!=1)){failed_=true;return FFX_NR_API_FAILURE_RETAINED;}
            if(status!=FFX_NR_COMPLETE&&status!=FFX_NR_PENDING&&status!=FFX_NR_BUSY)failed_=true;
            return status;
        }
    };
public:
    Binding Bind(void* exactCreatedContext,const FfxNrContextTicketV1& expected,
        FfxNrOwnedOutputHandleV1 output,FfxNrAlgorithmHandleV1 algorithm) {
        if(!exactCreatedContext||!Valid(expected)||!output.id||!output.cookie||!algorithm.id||!algorithm.cookie)return {};
        FfxNrContextTicketV1 actual{};
        if(service_.get_context(exactCreatedContext,&actual)!=FFX_NR_COMPLETE||!Same(actual,expected))return {};
        if(service_.output_retain(output)!=FFX_NR_COMPLETE)return {};
        struct Refs {Fsr3ControlledModule& m;FfxNrOwnedOutputHandleV1 o;FfxNrAlgorithmHandleV1 a{};
            ~Refs(){if(a.id)m.service_.algorithm_release(a);if(o.id)m.service_.output_release(o);}} refs{*this,output};
        if(service_.algorithm_retain(algorithm)!=FFX_NR_COMPLETE)return {};refs.a=algorithm;
        FfxNrOwnedOutputSnapshotV1 o{};FfxNrAlgorithmSnapshotV1 a{};uint32_t owns{};
        if(service_.output_inspect(output,&o)!=FFX_NR_COMPLETE||service_.algorithm_inspect(algorithm,&a)!=FFX_NR_COMPLETE||
           service_.algorithm_owns(algorithm,output,&owns)!=FFX_NR_COMPLETE||owns!=1||
           !Same(o.context,expected)||!Same(a.swapchain,expected)||o.failed||a.failed||!o.resource||!o.queue||
           !o.allocation_generation||!a.algorithm_generation||a.allocation_generation!=o.allocation_generation||a.input!=o.resource||
           a.registration_created!=1||a.registration_complete!=1||a.released||a.admission_closed||
           o.ordinary_escape_excluded!=1||o.recycling_excluded!=1||o.description.dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||
           !o.description.width||!o.description.height||o.description.depth_or_array_size!=1||o.description.mip_levels!=1||
           o.description.sample_count!=1||o.description.sample_quality!=0)return {};
        auto out=std::make_shared<Output>(shared_from_this(),output,o);refs.o={};
        auto algo=std::make_shared<Algorithm>(shared_from_this(),algorithm,out,a);refs.a={};
        return {{module_,expected,o,a,contract_},out,algo};
    }
};
}
