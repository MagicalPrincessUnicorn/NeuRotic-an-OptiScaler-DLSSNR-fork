// Included only by the existing NGX source translation unit. No second source,
// Resource registry, finalizer or lifetime authority is created here.
#include <dlssnr/CanonicalProviderIdentity.h>
#include <dlssnr/NativeControlledFgCatalog.h>
#include "NativeControlledFgBoundary.h"
namespace Neurotic::Lifecycle
{
bool NativeProcessBootstrap::BeginControlledSelection(std::uintptr_t feature,std::uint64_t generation)
{
    std::lock_guard lock(state_->mutex);
    if(!feature||!generation||state_->closed||state_->controlledFeature||state_->callbacks||
       state_->preparing||state_->initialPort||state_->lifetime)return false;
    state_->controlledFeature=feature;state_->controlledFeatureId=reinterpret_cast<NVSDK_NGX_Handle*>(feature)->Id;
    state_->controlledFeatureGeneration=generation;return true;
}
bool NativeProcessBootstrap::EnrollControlled(const DlssNr::NativeControlledFgEnrollmentV1& e)
{
    if(e.size!=sizeof(e)||e.version!=1||e.manualOptIn!=DlssNr::NativeControlledFgOptInV1||
       e.reserved||!e.sdkModule||!e.sdkContext||!e.width||!e.height||
       e.format!=DXGI_FORMAT_R16G16B16A16_FLOAT)return false;
    std::wstring modulePath(32768,L'\0');
    const auto pathLength=GetModuleFileNameW(reinterpret_cast<HMODULE>(e.sdkModule),modulePath.data(),static_cast<DWORD>(modulePath.size()));
    if(!pathLength||pathLength>=modulePath.size())return false;modulePath.resize(pathLength);
    auto moduleFile=std::make_shared<DlssNr::Canonical::LockedFile>(std::filesystem::path(modulePath));
    if(moduleFile->hash!=DlssNr::NativeControlledFgSdkSha256||moduleFile->bytes!=DlssNr::NativeControlledFgSdkBytes)return false;
    auto module=Fsr3ControlledModule::Authenticate(reinterpret_cast<HMODULE>(e.sdkModule),e.service,true);
    if(!module)return false;
    auto binding=module->Bind(reinterpret_cast<void*>(e.sdkContext),e.context,e.output,e.algorithm);
    if(!binding)return false;
    const auto original=binding.output->inspect();
    if(original.description.Width!=e.width||original.description.Height!=e.height||
       original.description.Format!=e.format||original.writerSubmitted||original.writerRetired||
       original.failed||original.writerActive||original.actionActive||original.writerRecording||!original.writerList)return false;
    auto selected=std::make_shared<ControlledFg>();selected->module=module;selected->moduleFile=std::move(moduleFile);
    selected->output=binding.output;selected->algorithm=binding.algorithm;
    selected->outputHandle=e.output;selected->algorithmHandle=e.algorithm;
    selected->observation.outputResource=reinterpret_cast<std::uintptr_t>(original.resource);
    selected->observation.allocationGeneration=original.allocationGeneration;
    selected->observation.algorithmGeneration=binding.algorithm->inspect().algorithmGeneration;
    // This predicate is bound by the authentic product source, never supplied
    // by a client. It is rechecked after Reset and before snapshot sealing.
    selected->writerCurrent=[output=binding.output,algorithm=binding.algorithm,original](ID3D12GraphicsCommandList* list,ID3D12Resource* target)noexcept {
        const auto o=output->inspect();const auto a=algorithm->inspect();
        return algorithm->Owns(output)&&!o.failed&&!a.failed&&o.writerActive&&o.actionActive&&o.admissionOpen&&
            !o.writerSubmitted&&!o.writerRetired&&o.writerRecording&&o.writerList==original.writerList&&list==o.writerList&&target==o.resource&&
            o.resource==original.resource&&o.allocationGeneration==original.allocationGeneration&&
            o.ordinaryEscapeExcluded&&o.recyclingExcluded&&a.registrationCreated&&a.registrationComplete&&!a.released;
    };
    std::lock_guard lock(state_->mutex);
    if(state_->closed||state_->priorWritersClosing||state_->callbacks||state_->preparing||!state_->controlledFeature||
       state_->controlledFeatureId!=e.featureId||state_->controlledFg||!state_->Owner())return false;
    auto& owners=*state_->Owner();
    const auto provider=owners.Provider()->Incarnation(owners.SourceJournal().Event());
    const auto handoff=owners.Handoff()->ContractChanged(owners.FinalizerJournal().Event());
    if(!Context::Established(provider.value)||!Context::Established(handoff.value))return false;
    selected->consumer.provider=provider.value.KnownPart()->value;
    selected->consumer.handoff=handoff.value.KnownPart()->value;
    selected->consumer.contract=Protocol::Symbol("FSR3.ControlledOwnedOutput.v1");
    selected->consumer.contractVersion=1;selected->consumer.color=C::ColorDomain::SceneLinear;
    selected->consumer.extent={e.width,e.height};
    selected->enrollment=owners.FrameGenerationJournal().Event().evidence.record;
    state_->controlledFg=std::move(selected);return true;
}
bool NativeProcessBootstrap::FinishControlledWriters(const std::function<bool()>& release)
{
    std::shared_ptr<ControlledFg> selected;
    {
        std::lock_guard lock(state_->mutex);selected=state_->controlledFg;
        if(state_->closed||state_->callbacks||state_->preparing||!selected||state_->priorWritersClosing)return false;
        state_->priorWritersClosing=true;
    }
    // Close only writer ingress; pending Resource/finalizer admission remains.
    state_->srLifetime->CloseAdmission();
    std::lock_guard selectedLock(selected->mutex);
    if(!selected->invocation||!selected->claim||!selected->transaction||selected->observation.failed)return false;
    auto& invocation=*selected->invocation;
    const auto output=selected->output->inspect();
    const auto stage=invocation.CommittedStage();
    if(!stage||stage->outcome!=C::NativeStageOutcome::HostReturnRecorded||!output.writerRetired||
       !DlssNr::GpuSafety::InspectTerminalRecording(invocation.ticket_))return false;
    if(!release||!release()){selected->observation.failed=1;return false;}
    selected->observation.featureReleased=1;
    state_->srLifetime->Poll();RetireSuccessfulSrCaptures();
    const auto sr=state_->srLifetime->Retirement();
    const auto* content=invocation.publishedOutput_?Context::ResolveMetadata(*invocation.publishedOutput_,*invocation.store_):nullptr;
    const auto* view=content?Context::ResolveNativeOutput(*content,*invocation.store_):nullptr;
    if(!sr||!view||!state_->srLifetime->CoversOutput(*sr,output.resource,invocation.ticket_,view->identity)||
       !invocation.providerOwner_->ClosePriorWriter(invocation.providerUse_,invocation.recordingUse_,invocation.ticket_,output.resource))return false;
    selected->observation.priorWritersClosed=1;
    selected->observation.evaluation=invocation.snapshot_->product.recipe.evaluation.value;
    selected->observation.outputRevision=view->identity.contentRevision.KnownPart()->value.value;
    if(auto write=selected->transaction->SelectedOutput())selected->observation.recordingIncarnation=write->recordingIncarnation;
    return state_->lifetime->SubmitStageDelivery(*stage);
}
namespace {
struct ControlledSubmitGuard
{
    std::unique_ptr<SelectedFsr3NativeLeaseOwner::SdkAction> sdk;
    std::unique_ptr<SelectedFsr3ResourceOwner::Guard> resource;
};
bool SameControlledDispatch(const FfxNrDispatchTicketV1& a,const FfxNrDispatchTicketV1& b)
{
    return a.size==sizeof(a)&&b.size==sizeof(b)&&a.version==1&&b.version==1&&
        a.context.owner_id==b.context.owner_id&&a.context.incarnation==b.context.incarnation&&
        a.context.registration_generation==b.context.registration_generation&&a.dispatch_id==b.dispatch_id&&
        a.registration_generation==b.registration_generation&&a.recording_incarnation==b.recording_incarnation&&
        a.buffer_generation==b.buffer_generation&&a.command_list==b.command_list&&a.command_queue==b.command_queue&&
        a.present_color==b.present_color&&a.frame_id==b.frame_id;
}
}
FfxNrStatusV1 NativeProcessBootstrap::BeginControlledConsumer(FfxNrOwnedOutputHandleV1 output,
    FfxNrAlgorithmHandleV1 algorithm,const FfxNrDispatchTicketV1& dispatch,void** token)
{
    if(!token)return FFX_NR_INVALID_ARGUMENT;*token=nullptr;
    std::shared_ptr<ControlledFg> selected;
    {
        std::lock_guard lock(state_->mutex);selected=state_->controlledFg;
        if(!selected||state_->closed)return FFX_NR_INVALID_ARGUMENT;
    }
    std::lock_guard selectedLock(selected->mutex);
    if(!selected->observation.priorWritersClosed||selected->observation.failed||selected->consumerAttempted||
       output.id!=selected->outputHandle.id||output.cookie!=selected->outputHandle.cookie||
       algorithm.id!=selected->algorithmHandle.id||algorithm.cookie!=selected->algorithmHandle.cookie)return FFX_NR_INVALID_ARGUMENT;
    selected->consumerAttempted=true;
    auto& invocation=*selected->invocation;
    auto* list=reinterpret_cast<ID3D12GraphicsCommandList*>(dispatch.command_list);
    auto ticket=DlssNr::GpuSafety::Record(list);
    auto& refusal=selected->observation.reserved;
    auto lease=SelectedFsr3NativeLeaseOwner::Create(invocation,state_->srLifetime,selected->output,selected->algorithm,dispatch,ticket,refusal);
    if(!lease){selected->observation.failed=1;return FFX_NR_INCOMPLETE_COVERAGE;}
    const auto stage=invocation.CommittedStage();
    const auto* content=stage&&stage->returnedOutput?Context::ResolveMetadata(*stage->returnedOutput,*invocation.store_):nullptr;
    if(!content){refusal=20;return FFX_NR_INCOMPLETE_COVERAGE;}
    auto boundary=BuildControlledFgBoundary(
        invocation.store_->owners.FrameGenerationJournal().Header(C::ContractId::C11,invocation.scope_),
        invocation.snapshot_->product.recipe,invocation.snapshot_->generations,*content,*stage,
        selected->transaction->Subject(),selected->consumer);
    auto publication=ControlledPublication::Create(state_->lifetime,*selected->claim,invocation.store_,selected->transaction,lease->resource,boundary);
    selected->selection=std::move(*lease);selected->publication=publication;selected->dispatch=dispatch;
    bool admitted=false;
    if(!publication)refusal=21;
    else if(!publication->Offer(reinterpret_cast<ID3D12Resource*>(dispatch.present_color),list,ticket,*selected->selection->action))
        refusal=publication->LastOfferFailure();
    else {admitted=publication->DispatchCurrent();if(!admitted)refusal=22;}
    if(!admitted)
    {selected->selection->action.reset();selected->observation.failed=1;return FFX_NR_INCOMPLETE_COVERAGE;}
    selected->consumerThread=std::this_thread::get_id();selected->observation.consumerEntered=1;
    *token=selected.get();return FFX_NR_COMPLETE;
}
void NativeProcessBootstrap::EndControlledConsumer(void* token,std::int32_t result)
{
    std::shared_ptr<ControlledFg> selected;
    {std::lock_guard lock(state_->mutex);selected=state_->controlledFg;}
    if(!selected)return;std::lock_guard selectedLock(selected->mutex);
    if(token!=selected.get()||!selected->publication||selected->observation.consumerReturned||selected->consumerThread!=std::this_thread::get_id())
    {selected->observation.failed=1;return;}
    selected->publication->DispatchReturned(result==0?SourceBoundDispatchEffect::ReturnedAccepted:SourceBoundDispatchEffect::ReturnedRejected,result);
    selected->selection->action.reset();selected->observation.consumerReturned=1;
    if(result!=0)selected->observation.failed=1;
}
FfxNrStatusV1 NativeProcessBootstrap::BeginControlledSubmit(const FfxNrDispatchTicketV1& dispatch,void** token)
{
    if(!token)return FFX_NR_INVALID_ARGUMENT;*token=nullptr;
    std::shared_ptr<ControlledFg> selected;
    {std::lock_guard lock(state_->mutex);selected=state_->controlledFg;}
    if(!selected)return FFX_NR_INVALID_ARGUMENT;std::lock_guard selectedLock(selected->mutex);
    if(!selected->selection||!selected->observation.consumerReturned||selected->observation.failed||
       selected->submitActive||!SameControlledDispatch(dispatch,selected->dispatch))return FFX_NR_INVALID_ARGUMENT;
    auto guard=std::make_shared<ControlledSubmitGuard>();
    guard->sdk=selected->selection->owner->AcquireSdkAction(dispatch);if(!guard->sdk)return FFX_NR_INCOMPLETE_COVERAGE;
    auto* list=reinterpret_cast<ID3D12GraphicsCommandList*>(dispatch.command_list);
    guard->resource=selected->selection->resource->AcquireSourceBound(reinterpret_cast<ID3D12Resource*>(dispatch.present_color),
        list,DlssNr::GpuSafety::Record(list),*guard->sdk);
    if(!guard->resource)return FFX_NR_INCOMPLETE_COVERAGE;
    selected->submitActive=true;selected->submitThread=std::this_thread::get_id();
    *token=guard.get();selected->submitGuard=std::move(guard);return FFX_NR_COMPLETE;
}
void NativeProcessBootstrap::EndControlledSubmit(void* token,const FfxNrSubmitResultV1& receipt)
{
    std::shared_ptr<ControlledFg> selected;
    {std::lock_guard lock(state_->mutex);selected=state_->controlledFg;}
    if(!selected)return;std::lock_guard selectedLock(selected->mutex);
    if(!selected->submitActive||!token||token!=selected->submitGuard.get()||selected->submitThread!=std::this_thread::get_id())
    {selected->observation.failed=1;return;}
    // SDK calls this synchronously on the same submission thread. Destruction
    // releases the real Resource action before journal/finalizer advancement.
    selected->submitActive=false;selected->submitGuard.reset();
    if(!SameControlledDispatch(receipt.dispatch,selected->dispatch)||receipt.size!=sizeof(receipt)||receipt.version!=1||
       receipt.execute_invoked!=1||receipt.output_published!=1||FAILED(receipt.close_result)||FAILED(receipt.signal_result))selected->observation.failed=1;
    selected->publication->Advance();
}
bool NativeProcessBootstrap::ObserveControlled(DlssNr::NativeControlledFgObservationV1& result,bool advance)
{
    std::shared_ptr<ControlledFg> selected;
    {std::lock_guard lock(state_->mutex);selected=state_->controlledFg;}
    if(!selected)return false;
    std::lock_guard selectedLock(selected->mutex);
    // Consumer/submission guards retain Resource's thread-affine action lock
    // across the foreign call. A concurrent observer must not wait on that
    // lock while holding this callback-state mutex needed by the matching end.
    if(selected->submitActive||(selected->observation.consumerEntered&&!selected->observation.consumerReturned))
    {result=selected->observation;return true;}
    if(!selected->output||!selected->algorithm){result=selected->observation;return true;}
    if(advance&&selected->selection)
    {
        if(auto terminal=selected->selection->owner->RetireOwnedConsumerRecording())
            selected->observation.consumerRecordingTerminal=static_cast<std::uint32_t>(terminal->state);
    }
    if(advance&&selected->publication)selected->publication->Advance();
    FfxNrSubmitResultV1 submission{};
    selected->observation.submissionObserved=selected->output->inspectSubmission(submission)?1u:0u;
    const auto algorithm=selected->algorithm->inspect();
    selected->observation.algorithmReleased=algorithm.released&&algorithm.destroyAttempted&&algorithm.admissionClosed;
    if(algorithm.failed||selected->output->inspect().failed)selected->observation.failed=1;
    if(selected->publication&&selected->publication->Retired())
    {
        selected->observation.resourceRetired=1;selected->observation.sourceExcluded=1;
        selected->observation.historyAcknowledged=1;selected->observation.terminal=1;
    }
    result=selected->observation;return true;
}
}
namespace DlssNr
{
unsigned NativeDx12Source::ControlledBegin(std::uintptr_t feature)
{
    auto* handle=reinterpret_cast<NVSDK_NGX_Handle*>(feature);if(!handle)return 0;
    if(State::Instance().currentFG!=nullptr)return 0;
    const auto snapshot=HandleToFeature.Read(handle->Id);
    if(!snapshot||snapshot.feature!=NVSDK_NGX_Feature_SuperSampling)return 0;
    Neurotic::Lifecycle::NativeProcessBootstrap* host=nullptr;
    {std::lock_guard lock(mutex_);if(globallyClosed_)return 0;if(!host_)host_=new Neurotic::Lifecycle::NativeProcessBootstrap;host=host_;}
    return host->BeginControlledSelection(feature,snapshot.generation)?1u:0u;
}
unsigned NativeDx12Source::ControlledEnroll(const NativeControlledFgEnrollmentV1* enrollment)
{
    Neurotic::Lifecycle::NativeProcessBootstrap* host=nullptr;
    {std::lock_guard lock(mutex_);if(globallyClosed_)return 0;host=host_;}
    if(!host||!enrollment)return 0;
    if(State::Instance().currentFG!=nullptr)return 0;
    const auto snapshot=HandleToFeature.Read(static_cast<unsigned>(enrollment->featureId));
    if(!snapshot||snapshot.generation!=host->state_->controlledFeatureGeneration)return 0;
    return host->EnrollControlled(*enrollment)?1u:0u;
}
unsigned NativeDx12Source::ControlledFinish(NativeControlledFgObservationV1* observation)
{
    Neurotic::Lifecycle::NativeProcessBootstrap* host=nullptr;
    {std::lock_guard lock(mutex_);host=host_;}
    if(!host||!observation)return 0;
    const bool done=host->FinishControlledWriters([host]{
        auto* feature=reinterpret_cast<NVSDK_NGX_Handle*>(host->state_->controlledFeature);
        if(State::Instance().currentFG!=nullptr)return false;
        const auto snapshot=feature?HandleToFeature.Read(feature->Id):decltype(HandleToFeature.Read(0)){};
        if(!snapshot||snapshot.generation!=host->state_->controlledFeatureGeneration)return false;
        return NVSDK_NGX_D3D12_ReleaseFeature(feature)==NVSDK_NGX_Result_Success;
    });
    host->ObserveControlled(*observation,false);return done?1u:0u;
}
FfxNrStatusV1 NativeDx12Source::ControlledConsumer(FfxNrOwnedOutputHandleV1 output,FfxNrAlgorithmHandleV1 algorithm,const FfxNrDispatchTicketV1* dispatch,void** token)
{Neurotic::Lifecycle::NativeProcessBootstrap* host;{std::lock_guard lock(mutex_);host=host_;}return host&&dispatch?host->BeginControlledConsumer(output,algorithm,*dispatch,token):FFX_NR_INVALID_ARGUMENT;}
void NativeDx12Source::ControlledReturned(void* token,std::int32_t result)
{Neurotic::Lifecycle::NativeProcessBootstrap* host;{std::lock_guard lock(mutex_);host=host_;}if(host)host->EndControlledConsumer(token,result);}
FfxNrStatusV1 NativeDx12Source::ControlledSubmit(const FfxNrDispatchTicketV1* dispatch,void** token)
{Neurotic::Lifecycle::NativeProcessBootstrap* host;{std::lock_guard lock(mutex_);host=host_;}return host&&dispatch?host->BeginControlledSubmit(*dispatch,token):FFX_NR_INVALID_ARGUMENT;}
void NativeDx12Source::ControlledSubmitted(void* token,const FfxNrSubmitResultV1* result)
{Neurotic::Lifecycle::NativeProcessBootstrap* host;{std::lock_guard lock(mutex_);host=host_;}if(host&&result)host->EndControlledSubmit(token,*result);}
unsigned NativeDx12Source::ControlledObserve(NativeControlledFgObservationV1* result,bool advance)
{Neurotic::Lifecycle::NativeProcessBootstrap* host;{std::lock_guard lock(mutex_);host=host_;}return host&&result&&host->ObserveControlled(*result,advance)?1u:0u;}
}
extern "C" __declspec(dllexport) unsigned __cdecl OptiScaler_W03_QueryControlledFgBridgeV1(DlssNr::NativeControlledFgBridgeV1* table)noexcept
{
    if(!table||table->size!=sizeof(*table)||table->version!=1||table->manualOptIn!=DlssNr::NativeControlledFgOptInV1)return 0;
    table->begin_selection=[](std::uintptr_t p)->unsigned{try{return DlssNr::NativeDx12Source::ControlledBegin(p);}catch(...){return 0;}};
    table->install_recording_hooks=[](std::uintptr_t p)->unsigned{try{if(!p)return 0;auto* list=reinterpret_cast<ID3D12GraphicsCommandList*>(p);D3D12Hooks::InstallNativeRecordingHooks(list);return DlssNr::GpuSafety::PrepareRecordingHooks(list)?1u:0u;}catch(...){return 0;}};
    table->pre_enroll=[](const DlssNr::NativeControlledFgEnrollmentV1* e)->unsigned{try{return DlssNr::NativeDx12Source::ControlledEnroll(e);}catch(...){return 0;}};
    table->finish_prior_writers=[](DlssNr::NativeControlledFgObservationV1* o)->unsigned{try{return DlssNr::NativeDx12Source::ControlledFinish(o);}catch(...){return 0;}};
    table->begin_consumer=[](FfxNrOwnedOutputHandleV1 o,FfxNrAlgorithmHandleV1 a,const FfxNrDispatchTicketV1* d,void** t)->FfxNrStatusV1{try{return DlssNr::NativeDx12Source::ControlledConsumer(o,a,d,t);}catch(...){return FFX_NR_API_FAILURE_RETAINED;}};
    table->end_consumer=[](void* t,int32_t r){try{DlssNr::NativeDx12Source::ControlledReturned(t,r);}catch(...){}};
    table->begin_submit=[](const FfxNrDispatchTicketV1* d,void** t)->FfxNrStatusV1{try{return DlssNr::NativeDx12Source::ControlledSubmit(d,t);}catch(...){return FFX_NR_API_FAILURE_RETAINED;}};
    table->end_submit=[](void* t,const FfxNrSubmitResultV1* r){try{DlssNr::NativeDx12Source::ControlledSubmitted(t,r);}catch(...){}};
    table->advance=[](DlssNr::NativeControlledFgObservationV1* o)->unsigned{try{return DlssNr::NativeDx12Source::ControlledObserve(o,true);}catch(...){return 0;}};
    table->query_terminal=[](DlssNr::NativeControlledFgObservationV1* o)->unsigned{try{return DlssNr::NativeDx12Source::ControlledObserve(o,false);}catch(...){return 0;}};
    return 1;
}
