#define NR_GPU_SAFETY_TEST
#define NR_NATIVE_UNSEALED_OWNER_TESTING
#include "../../../OptiScaler/dlssnr/NrGpuSafety.cpp"
#include <nr/lifecycle/NativeInvocationOwner.h>
#include "../vertical-slice/native_fixture.h"
#include "../finalizer/finalizer_fixture.h"
#include <dxgi1_4.h>
#include <iostream>

using namespace NrTest002;
using Microsoft::WRL::ComPtr;
namespace L=Neurotic::Lifecycle;
namespace P=Neurotic::Protocol;
namespace O=Neurotic::Orchestration;
namespace G=DlssNr::GpuSafety;
static void Api(HRESULT result)
{if(FAILED(result))throw std::runtime_error("controlled WARP prerequisite failed");}

// Link sentinels for renderer-only virtual-method dependencies. This fixture
// supplies no renderer borrow, so reaching any sentinel is an infrastructure
// error; they never simulate successful renderer preparation or output writes.
namespace DlssNr
{
bool NativeRendererInvocationBorrow::Current()const
{throw std::runtime_error("unexpected renderer borrow Current");}
const GpuSafety::LocalRecordingAction& NativeRendererInvocationBorrow::RecordingAction()const
{throw std::runtime_error("unexpected renderer borrow RecordingAction");}
const NativeRendererPreparation::Resources* NativeRendererInvocationBorrow::Inspect()const
{throw std::runtime_error("unexpected renderer borrow Inspect");}
std::unique_ptr<NativeRendererWriteToken> NativeRendererInvocationBorrow::IssueWriteToken(ID3D12Resource*)const
{throw std::runtime_error("unexpected renderer borrow IssueWriteToken");}
bool NativeRendererWriteToken::CurrentFor(ID3D12Resource*,ID3D12GraphicsCommandList*)const
{throw std::runtime_error("unexpected renderer write token CurrentFor");}
}

// Strict enrollment and the semantic metadata below are synthetic. The actual
// private owner, action guard, history state, callback close, recording ticket,
// provider registrations and retirement methods execute unchanged. No NVIDIA
// library, production source enrollment, provider qualification or model runs.
struct SyntheticSnapshotSeal:P::NativeInvocationOwnerPort
{
    static bool Seal(P::NativeInvocationSnapshot& snapshot,const std::shared_ptr<NrContextTests::Store>& store)
    {return SealSnapshot(snapshot,store);}
};
namespace Neurotic::Lifecycle
{
class NativeUnsealedOwnerTestAccess
{
  public:
    static std::unique_ptr<NativeInvocationOwner> Attach(NativeOwnerSet& owners,
        std::shared_ptr<const Protocol::NativeInvocationSnapshot> snapshot,const DlssNr::GpuSafety::Ticket& ticket,
        std::shared_ptr<DlssNr::NativeProviderRetirementOwner> provider,
        std::shared_ptr<DlssNr::ProviderInvocationUse> use
#ifdef NR_NATIVE_FEATURE_RECORDING_V1
        ,std::shared_ptr<DlssNr::NativeFeatureRecordingUse> recording={},std::uint64_t historyEvent=1
#endif
        )
    {
        NativeInvocationOwner::Seeds seeds{snapshot->consumer,snapshot->recording,snapshot->reservation,snapshot->handoff};
        C::ScopeRef scope;scope.key=snapshot->recording;
#ifdef NR_NATIVE_FEATURE_RECORDING_V1
        seeds.history=NrTest002::Key("history",historyEvent);
#endif
        auto owner=std::unique_ptr<NativeInvocationOwner>(new NativeInvocationOwner(
            {},{},{},owners.Resources(),owners,*snapshot->Sample(),scope,seeds,{}));
        owner->snapshot_=std::move(snapshot);owner->ticket_=ticket;
        owner->providerOwner_=std::move(provider);owner->providerUse_=std::move(use);
#ifdef NR_NATIVE_FEATURE_RECORDING_V1
        owner->recordingUse_=std::move(recording);
        if(!owner->AttachFeatureHistory())throw std::runtime_error("actual feature history attachment unavailable");
#else
        owner->history_=std::make_unique<NativeHistoryState>(NrTest002::Key("history.owner"),NrTest002::Key("history"));
#endif
        return owner;
    }
    static void CloseCallback(NativeInvocationOwner& owner){owner.CloseCallback();}
    static auto History(NativeInvocationOwner& owner){return owner.history_;}
    static auto Action(NativeInvocationOwner& owner)
    {return std::make_unique<NativeInvocationOwner::Action>(owner,owner.snapshot_,nullptr);}
    static void ReturnTail(NativeInvocationOwner& owner,bool active){owner.returnTailActive_=active;}
};
}

int main()
try
{
    unsigned checks=0,failures=0;
    const auto check=[&](bool value,const char* name){++checks;if(!value){++failures;std::cerr<<"FAIL "<<name<<'\n';}};
    using Access=L::NativeUnsealedOwnerTestAccess;
    auto final=std::make_unique<FinalizerTests::Fixture>(1);
    auto owners=std::make_unique<L::NativeOwnerSet>(Sym("unsealed.warp.fixture"));
    auto metadata=std::make_unique<NativeTests::Fixture>(false,true);
    metadata->frame.sessionId=Known(*owners->Session());metadata->AsNativeSample(1);
    C::NativeFinalConsumerContractV1 consumer;
    consumer.provider=Id<C::ProviderIncarnation>("fixture.consumer",1);consumer.handoff=final->request.handoff;
    consumer.contract=Sym("fixture.before-fg");consumer.contractVersion=1;consumer.extent={1920,1080};
    C::NativeDeliveryContractV1 delivery;delivery.kind=C::NativeDeliveryKind::FinalConsumerRequired;
    delivery.profile=metadata->request.requestedProfile;delivery.placement=metadata->request.placement;
    delivery.returnBoundary=C::NativeReturnBoundary::NgxEvaluateReturn;delivery.historyPolicy=C::NativeHistoryPolicy::OrderedNative;
    delivery.finalConsumer=metadata->store.Publish(consumer,C::OwnerDomain::FrameGeneration);
    final->request.plan.nativeDelivery=metadata->store.Publish(delivery,C::OwnerDomain::StreamCoordinator);
    metadata->request.nativePlan=metadata->store.Publish(final->request.plan,C::OwnerDomain::StreamCoordinator);
    metadata->request.externalPlan={Sym(C::PlanCommit::WireName),final->request.plan.header.record,final->request.plan.header.revision};
    metadata->Rebuild();
    auto snapshot=std::make_shared<P::NativeInvocationSnapshot>();snapshot->product=*metadata->product;
    snapshot->frame=metadata->frame;snapshot->generation=final->request.plan.routeGeneration.KnownPart()->value;
    snapshot->consumer=Key("consumer");snapshot->recording=Key("recording");snapshot->reservation=Key("reservation");
    snapshot->handoff=final->request.handoff;
    C::GenerationVector generations;generations.Insert(C::GenerationToken{snapshot->generation.Describe()});
    snapshot->generations=metadata->store.Publish(generations,C::OwnerDomain::IdentityRegistry);
    auto store=std::make_shared<NrContextTests::Store>(std::move(metadata->store));
    if(!SyntheticSnapshotSeal::Seal(*snapshot,store))throw std::runtime_error("synthetic strict metadata sealing failed");
    final->request.recipe=snapshot->product.recipe;final->request.generations=snapshot->generations;
    final->request.expectedConsumer.reset(); // Exact Native obligation supplies the FG contract in this store.
    auto event=[&]{return L::FinalizerEvent{owners->FinalizerJournal().Event(),snapshot->generations};};
    auto claimed=owners->Finalizer()->TryClaimPrimary(final->request,*store,event());
    if(!claimed.claim)throw std::runtime_error("authentic finalizer claim unavailable");
    const auto claim=*claimed.claim;
    check(snapshot->Delivery()==C::NativeDeliveryKind::FinalConsumerRequired&&snapshot->Sample().has_value(),
        "synthetic strict snapshot passes actual metadata seal");

    ComPtr<IDXGIFactory4> factory;ComPtr<IDXGIAdapter> warp;ComPtr<ID3D12Device> device;
    Api(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));Api(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)));
    Api(D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
    ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC queueDesc{};
    Api(device->CreateCommandQueue(&queueDesc,IID_PPV_ARGS(&queue)));
    ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;
    Api(device->CreateCommandAllocator(queueDesc.Type,IID_PPV_ARGS(&allocator)));
    Api(device->CreateCommandList(0,queueDesc.Type,allocator.Get(),nullptr,IID_PPV_ARGS(&list)));
    auto observed=G::Record(list.Get());Api(list->Close());Api(list->Reset(allocator.Get(),nullptr));
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC description{};description.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;description.Width=16;
    description.Height=4;description.DepthOrArraySize=description.MipLevels=1;description.SampleDesc.Count=1;
    description.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    ComPtr<ID3D12Resource> input,output;
    Api(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&description,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&input)));
    Api(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&description,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&output)));
    const auto* inputView=owners->Resources().Observe(input.Get());
    if(!inputView)throw std::runtime_error("actual WARP resource publication unavailable");
    const auto ticket=G::Record(list.Get());auto recordingAction=G::BeginLocalAction(ticket,list.Get(),input.Get());
    if(!recordingAction)throw std::runtime_error("actual WARP recording action unavailable");
    const auto contract=DlssNr::ProviderLifetimeContract::ControlledCallerList();
    const auto feature=Id<C::ProviderIncarnation>("controlled.feature",1);
    auto provider=std::make_shared<DlssNr::NativeProviderRetirementOwner>(contract,feature);
    DlssNr::ProviderRegistrationBinding registration;
    registration.invocation=*snapshot->Sample();registration.registration=Key("registration");
    registration.resource=inputView->identity;registration.feature=feature;registration.recording=ticket;registration.native=input.Get();
#ifdef NR_NATIVE_FEATURE_RECORDING_V1
    auto recording=provider->ReserveRecording(*snapshot->Sample(),*recordingAction,list.Get(),{input.Get(),output.Get()});
    if(!recording)throw std::runtime_error("feature recording reservation unavailable");
#endif
    auto use=provider->Reserve({registration},*recordingAction,list.Get());
    if(!use||!provider->Enter(use,*recordingAction,contract))throw std::runtime_error("controlled provider entry unavailable");
    auto owner=Access::Attach(*owners,snapshot,ticket,provider,use
#ifdef NR_NATIVE_FEATURE_RECORDING_V1
        ,recording
#endif
        );
    check(owner->History()->Attempt(snapshot->product.recipe.evaluation,false,{},snapshot->Sample()),"actual history attempt opens tail");
    list->CopyResource(output.Get(),input.Get());
    provider->Returned(use,false); // Possible partial failure still owns GPU work.
    check(!provider->CpuActive(use)&&provider->Pending()==1,"error return closes CPU borrow and keeps pending use");
    recordingAction.reset();Api(list->Close());ID3D12CommandList* lists[]={list.Get()};queue->ExecuteCommandLists(1,lists);
    if(!G::Drain(5000))throw std::runtime_error("controlled WARP drain failed");
    check(!owner->UnsealedTerminal(*snapshot,claim),"completed replayable recording cannot produce owner receipt");
    Api(allocator->Reset());Api(list->Reset(allocator.Get(),nullptr));provider->Poll();
    const auto receipts=provider->Receipts(use);
    check(receipts.size()==1&&provider->ValidateReceipt(use,registration,receipts[0])&&provider->Complete(use)&&provider->Pending()==1,
        "exact observed terminal registration is complete while invocation remains pending");
    check(!owner->UnsealedTerminal(*snapshot,claim),"open callback refuses despite complete provider receipt");
    Access::CloseCallback(*owner);
    check(!owner->UnsealedTerminal(*snapshot,claim),"active history refuses after callback closes");
    const auto interrupted=owners->Finalizer()->InterruptUnsealed(claim,event());
    if(!interrupted.interruption)throw std::runtime_error("authentic finalizer interruption unavailable");
    auto foreignHistory=*snapshot;++foreignHistory.product.recipe.evaluation.value;
    check(!owner->FinalInterrupted(foreignHistory,claim,*interrupted.interruption),"foreign interruption cannot abandon history");
    check(owner->FinalInterrupted(*snapshot,claim,*interrupted.interruption),"actual finalizer acknowledgment reaches exact history owner");
    check(owner->History()->InvocationClosed()&&owner->History()->Pending(),"abandon closes invocation and preserves reset obligation");
    auto action=Access::Action(*owner);
    check(!owner->UnsealedTerminal(*snapshot,claim),"actual Resource action guard prevents terminal receipt");
    action.reset();
    auto wrong=*snapshot;++wrong.recording.value;
    check(!owner->UnsealedTerminal(wrong,claim),"foreign recording snapshot refuses");
    wrong=*snapshot;++wrong.product.recipe.header.revision;
    check(!owner->UnsealedTerminal(wrong,claim),"foreign recipe snapshot refuses");
    Access::ReturnTail(*owner,true);
    check(!owner->UnsealedTerminal(*snapshot,claim)&&owner->Retire(*snapshot,{})==O::Retirement::Outstanding,
        "active outer return tail blocks receipt and retirement despite all physical tails terminal");
    Access::ReturnTail(*owner,false);
    auto terminal=owner->UnsealedTerminal(*snapshot,claim);
    check(terminal.has_value(),"closed callback history action and exact recording produce private owner receipt");
    check(provider->Pending()==1&&!provider->BeginTeardown(),"receipt production leaves provider pending and teardown refused");
    check(owner->Retire(*snapshot,claim)==O::Retirement::Outstanding&&provider->Pending()==1,
        "retained finalizer claim blocks owner retirement and pending-use closure");
    if(!terminal)throw std::runtime_error("private owner receipt unavailable");
    check(owners->Finalizer()->InterruptUnsealed(claim,event()).accepted,"actual finalizer interrupts without seal");
    auto registry=NrContextTests::Header(C::ContractId::C11,C::OwnerDomain::IdentityRegistry,500);
    auto rejected=owners->Finalizer()->RetireInterrupted(claim,*terminal,registry,snapshot->frame,{},*store,event());
    check(!rejected.accepted&&provider->Pending()==1&&!provider->BeginTeardown(),
        "rejected unknown exclusion preserves provider pending and feature teardown refusal");
    check(owner->Retire(*snapshot,claim)==O::Retirement::Outstanding&&provider->Pending()==1,
        "rejected exclusion cannot clear the claim or invocation tail");
    auto retired=owners->Finalizer()->RetireInterrupted(claim,*terminal,registry,snapshot->frame,Known(true),*store,event());
    check(retired.accepted&&!retired.packet&&provider->Pending()==1,
        "authentic finalizer closure does not itself remove provider pending use");
    check(owner->Retire(*snapshot,{})==O::Retirement::Retired&&provider->Pending()==0&&provider->BeginTeardown(),
        "owner retirement after claim closure removes pending use and permits feature teardown");
    Api(list->Close());
#ifndef NR_NATIVE_FEATURE_RECORDING_V1
    check(false,"persistent feature history and recording exclusion unavailable");
#else
    // A second invocation shares the real feature owner's history and scratch,
    // even when provider entry never happens. Its recording capability cannot
    // be released by cancelling the separate unused provider registration.
    Api(list->Reset(allocator.Get(),nullptr));
    const auto aTicket=G::Record(list.Get());
    auto aAction=G::BeginLocalAction(aTicket,list.Get(),input.Get());
    auto featureOwner=std::make_shared<DlssNr::NativeProviderRetirementOwner>(contract,
        Id<C::ProviderIncarnation>("controlled.feature",2));
    auto aSample=*snapshot->Sample();aSample.producerOrdinal+=1;aSample.callback.value+=1;
    auto bSample=aSample;bSample.producerOrdinal+=1;bSample.callback.value+=1;
    auto physical=featureOwner->ReserveRecording(aSample,*aAction,list.Get(),{input.Get(),output.Get()});
    check(bool(physical)&&featureOwner->RecordingCurrent(physical,aSample,aTicket),
        "exact feature recording owns shared scratch before commands");
    auto ownerA=Access::Attach(*owners,snapshot,aTicket,featureOwner,{},physical);
    auto* history=ownerA->History();
    auto retainedHistory=Access::History(*ownerA);
    const auto aEvaluation=Id<C::EvaluationId>("persistent.evaluation",1);
    check(history&&history->Attempt(aEvaluation,false,{},aSample),"first feature history attempt admitted");
    list->CopyResource(output.Get(),input.Get());
    history->Abandon(aEvaluation);
    Access::CloseCallback(*ownerA);
    registration.invocation=aSample;registration.registration=Key("registration",2);
    registration.recording=aTicket;registration.feature=featureOwner->Feature();
    auto unused=featureOwner->Reserve({registration},*aAction,list.Get());
    check(unused&&featureOwner->Cancel(unused)&&featureOwner->Pending()==0,
        "pre-entry failure cancels only provider reservation");
    check(featureOwner->RecordingHeld(),"legacy renderer remains excluded after unused provider cancellation");
    auto wrongSample=aSample;++wrongSample.producerOrdinal;
    check(!featureOwner->RecordingCurrent(physical,wrongSample,aTicket)&&
        !featureOwner->RecordingCurrent(physical,aSample,ticket),"foreign sample or recording cannot claim exclusion");
    check(!featureOwner->CloseRecording(physical),"recorded pre-provider copy cannot release physical reservation");
    aAction.reset();Api(list->Close());
    ComPtr<ID3D12CommandAllocator> bAllocator;ComPtr<ID3D12GraphicsCommandList> bList;
    Api(device->CreateCommandAllocator(queueDesc.Type,IID_PPV_ARGS(&bAllocator)));
    Api(device->CreateCommandList(0,queueDesc.Type,bAllocator.Get(),nullptr,IID_PPV_ARGS(&bList)));
    G::Record(bList.Get());Api(bList->Close());Api(bList->Reset(bAllocator.Get(),nullptr));
    const auto bTicket=G::Record(bList.Get());auto bAction=G::BeginLocalAction(bTicket,bList.Get(),input.Get());
    auto otherFeature=std::make_shared<DlssNr::NativeProviderRetirementOwner>(contract,
        Id<C::ProviderIncarnation>("controlled.feature",3));
    auto conflicting=otherFeature->ReserveRecording(bSample,*bAction,bList.Get(),{input.Get(),output.Get()});
    check(!conflicting,"different feature cannot reserve allocations held by an older recording");
    check(!featureOwner->ReserveRecording(bSample,*bAction,bList.Get(),{input.Get(),output.Get()}),
        "second recording refused while first remains unsubmitted");
    queue->ExecuteCommandLists(1,lists);
    ComPtr<ID3D12Fence> completion;Api(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&completion)));
    Api(queue->Signal(completion.Get(),1));const auto deadline=GetTickCount64()+5000;
    while(completion->GetCompletedValue()<1&&GetTickCount64()<deadline)Sleep(1);
    if(completion->GetCompletedValue()!=1)throw std::runtime_error("persistent feature completion failed");
    check(!featureOwner->CloseRecording(physical)&&
        !featureOwner->ReserveRecording(bSample,*bAction,bList.Get(),{input.Get(),output.Get()}),
        "completed replayable work still excludes second recording");
    Api(allocator->Reset());Api(list->Reset(allocator.Get(),nullptr));
    check(ownerA->Retire(*snapshot,{})==O::Retirement::Retired,
        "actual invocation owner releases physical reservation after exact terminal recording");
    check(!featureOwner->RecordingHeld(),"normal terminal invocation restores renderer admission");
    ownerA.reset();
    auto nextPhysical=featureOwner->ReserveRecording(bSample,*bAction,bList.Get(),{input.Get(),output.Get()});
    auto ownerB=Access::Attach(*owners,snapshot,bTicket,featureOwner,{},nextPhysical,2);
    auto* nextHistory=ownerB->History();
    check(nextPhysical&&nextHistory&&nextHistory==history&&nextHistory->RequiresReset()&&
        nextHistory->Projection().history==Key("history",1),
        "next invocation retains reset debt and stable feature history identity");
    const auto bEvaluation=Id<C::EvaluationId>("persistent.evaluation",2);
    check(!nextHistory->Attempt(bEvaluation,false,{},bSample)&&nextHistory->Attempt(bEvaluation,true,{},bSample),
        "next model attempt requires reset without fabricating an acknowledgement");
    nextHistory->Abandon(bEvaluation);
    Access::CloseCallback(*ownerB);
    bAction.reset();Api(bList->Close());Api(bList->Reset(bAllocator.Get(),nullptr));
    check(ownerB->Retire(*snapshot,{})==O::Retirement::Retired&&featureOwner->BeginTeardown(),
        "terminal physical use permits feature teardown with history retained by old owner");
    if(conflicting)check(otherFeature->CloseRecording(conflicting),"unexpected cross-feature reservation can close only after exact terminality");
    check(history->Pending(),"physical retirement cannot acknowledge history reset");
    auto recreated=otherFeature;
    const auto freshTicket=G::Record(bList.Get());auto freshAction=G::BeginLocalAction(freshTicket,bList.Get(),input.Get());
    auto freshUse=recreated->ReserveRecording(bSample,*freshAction,bList.Get(),{input.Get(),output.Get()});
    auto freshHistory=recreated->History(freshUse,owners->HistoryJournal().Binding().subject,Key("history",3));
    check(freshHistory&&!freshHistory->RequiresReset()&&history->Pending(),
        "new feature incarnation has distinct history while old debt remains inspectable");
    freshAction.reset();Api(bList->Close());Api(bList->Reset(bAllocator.Get(),nullptr));
    check(recreated->CloseRecording(freshUse)&&recreated->BeginTeardown(),"fresh feature unused recording retires after reset");
    check(retainedHistory&&retainedHistory->Pending(),"old feature history remains retained through recreation");
    Api(bList->Close());Api(list->Close());
    // Feature history is exclusive even if a second callback proposes distinct
    // committed storage. Keep this negative independent of allocation overlap.
    Api(bList->Reset(bAllocator.Get(),nullptr));
    const auto featureTicket=G::Record(bList.Get());
    auto featureAction=G::BeginLocalAction(featureTicket,bList.Get(),input.Get());
    auto featureOnly=std::make_shared<DlssNr::NativeProviderRetirementOwner>(contract,
        Id<C::ProviderIncarnation>("controlled.feature",4));
    auto firstFeatureUse=featureOnly->ReserveRecording(aSample,*featureAction,bList.Get(),{input.Get()});
    auto secondFeatureUse=featureOnly->ReserveRecording(bSample,*featureAction,bList.Get(),{output.Get()});
    check(firstFeatureUse&&!secondFeatureUse,"one feature history excludes a second invocation with distinct allocations");
    featureAction.reset();Api(bList->Close());Api(bList->Reset(bAllocator.Get(),nullptr));
    check(featureOnly->CloseRecording(firstFeatureUse),"feature-only reservation releases after terminal recording");
    if(secondFeatureUse)featureOnly->CloseRecording(secondFeatureUse);
    Api(bList->Close());
    Api(bList->Reset(bAllocator.Get(),nullptr));
    const auto quarantineTicket=G::Record(bList.Get());
    auto quarantineAction=G::BeginLocalAction(quarantineTicket,bList.Get(),input.Get());
    DlssNr::NativeProviderRetirementOwner unavailable(DlssNr::ProviderLifetimeContract::NgxFeature18(),feature);
    bool unsupportedEmpty=true;
    for(unsigned i=0;i<130;++i)
        unsupportedEmpty=unsupportedEmpty&&!unavailable.ReserveRecording(bSample,*quarantineAction,bList.Get(),{input.Get(),output.Get()});
    check(unsupportedEmpty&&!unavailable.RecordingHeld(),"unsupported contract never consumes recording retention capacity");
    unsigned quarantined=0;
    for(unsigned i=0;i<129;++i)
    {
        auto orphan=std::make_unique<DlssNr::NativeProviderRetirementOwner>(contract,
            Id<C::ProviderIncarnation>("abandoned.feature",i+1));
        ComPtr<ID3D12Resource> distinct;
        Api(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&description,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&distinct)));
        if(orphan->ReserveRecording(bSample,*quarantineAction,bList.Get(),{distinct.Get()}))++quarantined;
        // Owner destruction leaves its pre-reserved slot; no destructor append.
    }
    check(quarantined==128,"abandoned recording uses retain a fixed bound before capacity refusal");
    quarantineAction.reset();Api(bList->Close());Api(bList->Reset(bAllocator.Get(),nullptr));Api(bList->Close());
#endif
    std::cout<<"Native unsealed owner: "<<checks<<" checks, "<<failures
             <<" failures; synthetic strict enrollment plus WARP lifecycle; NVIDIA provider NOT_RUN\n";
    return failures?1:0;
}
catch(const std::exception& error){std::cerr<<"INFRA_ERROR "<<error.what()<<'\n';return 2;}
