#define NR_GPU_SAFETY_TEST
#include "../../../OptiScaler/dlssnr/NrGpuSafety.cpp"
#include "../contracts/Test002Support.h"
#include "../representations/RepresentationFixture.h"
#include <dxgi1_4.h>
#include <dlssnr/NativeProviderEntry.h>
#include <iostream>
#include <thread>
#if __has_include(<dlssnr/NativeProviderLifetime.h>)
#include <dlssnr/NativeProviderLifetime.h>
#endif
using namespace NrTest002;
using Microsoft::WRL::ComPtr;
namespace G=DlssNr::GpuSafety;
static void Api(HRESULT h){if(FAILED(h))throw std::runtime_error("WARP lifecycle prerequisite failed");}
int main()
try
{
    unsigned checks=0,failures=0;
    const auto check=[&](bool ok,const char* name){++checks;if(!ok){++failures;std::cerr<<"FAIL "<<name<<'\n';}};
#if __has_include(<dlssnr/NativeProviderLifetime.h>)
    using namespace DlssNr;
    ComPtr<IDXGIFactory4> factory;ComPtr<IDXGIAdapter> warp;ComPtr<ID3D12Device> device;
    Api(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));Api(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)));
    DXGI_ADAPTER_DESC warpDesc{};Api(warp->GetDesc(&warpDesc));
    std::string warpName;
    for(const wchar_t character:warpDesc.Description)
    {if(character==0)break;warpName.push_back(character>=32&&character<=126?static_cast<char>(character):'?');}
    std::cout<<"WARP_ADAPTER name="<<warpName<<" vendor="<<warpDesc.VendorId<<
        " device="<<warpDesc.DeviceId<<" luid_low="<<warpDesc.AdapterLuid.LowPart<<
        " luid_high="<<warpDesc.AdapterLuid.HighPart<<'\n';
    Api(D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
    ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC q{};
    Api(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)));
    ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;
    Api(device->CreateCommandAllocator(q.Type,IID_PPV_ARGS(&allocator)));
    Api(device->CreateCommandList(0,q.Type,allocator.Get(),nullptr,IID_PPV_ARGS(&list)));
    ComPtr<ID3D12Resource> resource;D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=256;
    desc.Height=1;desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    Api(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&resource)));
    ComPtr<ID3D12Resource> controlledOutput;
    Api(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&controlledOutput)));
    auto feature=Id<C::ProviderIncarnation>("feature",1);
    const auto contract=ProviderLifetimeContract::NgxFeature18(Feature18HostContractIdentity);
    NativeProviderRetirementOwner owner(contract,feature);
    NativeProviderRetirementOwner foreign(contract,feature);
    NativeProviderRetirementOwner malformedFeature(contract,{});
    NativeProviderRetirementOwner unavailable(ProviderLifetimeContract::NgxFeature18(),feature);
    NrRepresentationTests::Fixture fixture;
    std::uint64_t serial=0;
    const auto binding=[&](const G::Ticket& ticket){ProviderRegistrationBinding b;
        b.invocation.session=Id<C::SessionId>("session",1);b.invocation.stream=Id<C::RenderStreamId>("stream",1);
        b.invocation.view=Id<C::ViewId>("view",1);b.invocation.feature=Id<C::ObjectIncarnation>("feature",1);
        b.invocation.ingress=Id<C::ObjectIncarnation>("ingress",1);b.invocation.producerOrdinal=++serial;
        b.invocation.callback=Key("callback",serial);b.registration=Key("registration",serial);
        b.resource=fixture.source.identity;b.feature=feature;b.recording=ticket;b.native=resource.Get();return b;};
    auto first=G::Record(list.Get());Api(list->Close());Api(list->Reset(allocator.Get(),nullptr));
    unsigned completed=0;
    constexpr unsigned sequentialInvocations=1024;
    for(unsigned i=0;i<sequentialInvocations;++i)
    {
        auto ticket=G::Record(list.Get());auto action=G::BeginLocalAction(ticket,list.Get(),resource.Get());
        if(!action)throw std::runtime_error("recording action unavailable");
        auto b=binding(ticket);
        check(!unavailable.Reserve({b},*action,list.Get())&&unavailable.Pending()==0,"unsupported contract cancels before pool reservation");
        auto wrong=b;wrong.feature={};
        check(!malformedFeature.Reserve({wrong},*action,list.Get()),"missing feature identity cannot reserve");
        wrong=b;++wrong.feature.value;
        check(!owner.Reserve({wrong},*action,list.Get()),"foreign feature generation cannot reserve");
        wrong=b;wrong.resource.resourceGeneration={};
        check(!owner.Reserve({wrong},*action,list.Get()),"unknown resource generation cannot reserve");
        auto use=owner.Reserve({b},*action,list.Get());
        check(use&&owner.Pending()==1&&!owner.CpuActive(use)&&!owner.GpuHeld(use,b.registration,b.resource),"reservation does not activate provider borrow");
        check(!owner.Reserve({b},*action,list.Get()),"duplicate registration rejected");
        check(owner.Enter(use,*action,contract)&&owner.CpuActive(use)&&owner.GpuHeld(use,b.registration,b.resource),"exact provider entry activates CPU borrow");
        check(!owner.Enter(use,*action,contract),"entry at most once");
        list->CopyBufferRegion(controlledOutput.Get(),0,resource.Get(),0,256);
        owner.Returned(use,false); // An error return is still possibly effectful.
        check(!owner.CpuActive(use)&&owner.Receipts(use).empty()&&owner.GpuHeld(use,b.registration,b.resource),"error return closes CPU only");
        action.reset();Api(list->Close());ID3D12CommandList* lists[]={list.Get()};queue->ExecuteCommandLists(1,lists);
        if(i==0)queue->ExecuteCommandLists(1,lists);
        owner.Poll();check(owner.Receipts(use).empty(),"submission or completion alone cannot mint receipt");
        if(!G::Drain(5000))throw std::runtime_error("WARP drain failed");
        owner.Poll();check(owner.Receipts(use).empty(),"completed replayable recording retained");
        Api(allocator->Reset());Api(list->Reset(allocator.Get(),nullptr));owner.Poll();
        const auto receipts=owner.Receipts(use);
        check(receipts.size()==1&&owner.Complete(use)&&owner.Pending()==1&&!owner.GpuHeld(use,b.registration,b.resource),"terminal recording issues complete receipt set");
        if(receipts.size()==1)
        {
            check(owner.ValidateReceipt(use,b,receipts[0]),"authenticated exact receipt");
            check(receipts[0]->Contract().Identity()==Feature18HostContractIdentity,"matched host contract is explicit on terminal receipt");
            auto stale=b;++stale.registration.value;check(!owner.ValidateReceipt(use,stale,receipts[0]),"wrong registration rejected");
            stale=b;++stale.feature.value;check(!owner.ValidateReceipt(use,stale,receipts[0]),"wrong feature generation rejected");
            stale=b;stale.resource.resourceGeneration={};check(!owner.ValidateReceipt(use,stale,receipts[0]),"wrong resource generation rejected");
            stale=b;++stale.invocation.producerOrdinal;check(!owner.ValidateReceipt(use,stale,receipts[0]),"wrong invocation rejected");
            stale=b;stale.recording=G::Record(list.Get());check(!owner.ValidateReceipt(use,stale,receipts[0]),"new recording cannot use old receipt");
            check(!foreign.ValidateReceipt(use,b,receipts[0])&&!foreign.Complete(use),"foreign receipt owner rejected");
            check(receipts[0]->Terminal().submissions==(i==0?2u:1u),"receipt retains actual replay count");
            owner.Poll();check(owner.Receipts(use)[0]==receipts[0],"one immutable receipt per registration");
        }
        check(owner.CloseInvocation(use)&&owner.Pending()==0,"feature tail closes only after invocation tails");
        ++completed;
    }
    check(completed==sequentialInvocations,"1024 controlled invocations recycle registrations without growth");
    {
        NativeProviderRetirementOwner preEntryOwner(contract,feature);
        auto preEntryTicket=G::Record(list.Get());
        auto preEntryAction=G::BeginLocalAction(preEntryTicket,list.Get(),resource.Get());
        if(!preEntryAction)throw std::runtime_error("pre-entry recording action unavailable");
        auto preEntryBinding=binding(preEntryTicket);
        auto preEntryRecording=preEntryOwner.ReserveRecording(preEntryBinding.invocation,*preEntryAction,
            list.Get(),{resource.Get(),controlledOutput.Get()});
        check(bool(preEntryRecording),"pre-entry encode/copy recording reserved independently");
        list->CopyBufferRegion(controlledOutput.Get(),0,resource.Get(),0,256);
        auto preEntryUse=preEntryOwner.Reserve({preEntryBinding},*preEntryAction,list.Get());
        check(preEntryUse&&preEntryOwner.Enter(preEntryUse,*preEntryAction,contract)&&
            preEntryOwner.CancelActivatedBeforeCall(preEntryUse)&&preEntryOwner.Complete(preEntryUse)&&
            preEntryOwner.Pending()==0&&preEntryOwner.RecordingHeld(),
            "cancelled activation releases only never-called provider registration");
        preEntryAction.reset();Api(list->Close());
        ID3D12CommandList* preEntryLists[]={list.Get()};queue->ExecuteCommandLists(1,preEntryLists);
        check(!preEntryOwner.CloseRecording(preEntryRecording),
            "pre-entry copy recording retained while it can replay");
        if(!G::Drain(5000))throw std::runtime_error("pre-entry WARP drain failed");
        Api(allocator->Reset());Api(list->Reset(allocator.Get(),nullptr));
        check(preEntryOwner.CloseRecording(preEntryRecording)&&!preEntryOwner.RecordingHeld(),
            "pre-entry copy recording closes only after terminal non-replayability");
    }
    // Controlled provider capacity: these are entered, possibly effectful uses
    // on an actual open WARP list. They have neither submission completion nor
    // a non-replayable recording fact. A CPU return cannot free their slots.
    {
        NativeProviderRetirementOwner capacityOwner(contract,feature);
        auto capacityTicket=G::Record(list.Get());
        auto capacityAction=G::BeginLocalAction(capacityTicket,list.Get(),resource.Get());
        if(!capacityAction)throw std::runtime_error("capacity recording action unavailable");
        using RetainedUse=std::pair<ProviderRegistrationBinding,std::shared_ptr<ProviderInvocationUse>>;
        std::vector<RetainedUse> heldUses;
        unsigned recordedCopies=0;
        bool allEntered=true;
        const auto capacityCheck=[&](bool fact,const char* name){
            check(fact,name);
            std::cout<<"CAPACITY "<<(fact?"PASS ":"FAIL ")<<name<<'\n';
        };
        for(unsigned i=0;i<64;++i)
        {
            auto heldBinding=binding(capacityTicket);
            auto held=capacityOwner.Reserve({heldBinding},*capacityAction,list.Get());
            const bool entered=capacityOwner.Enter(held,*capacityAction,contract);
            allEntered=allEntered&&entered;
            if(entered)
            {
                list->CopyBufferRegion(controlledOutput.Get(),0,resource.Get(),0,256);
                ++recordedCopies;capacityOwner.Returned(held,false);
            }
            heldUses.emplace_back(heldBinding,std::move(held));
        }
        capacityCheck(allEntered&&capacityOwner.Pending()==64&&recordedCopies==64,
            "free provider slots admit exactly 64 entered recording uses");
        capacityOwner.Poll();
        auto overflowBinding=binding(capacityTicket);
        auto overflow=capacityOwner.Reserve({overflowBinding},*capacityAction,list.Get());
        if(overflow&&capacityOwner.Enter(overflow,*capacityAction,contract))
        {
            list->CopyBufferRegion(controlledOutput.Get(),0,resource.Get(),0,256);
            ++recordedCopies;capacityOwner.Returned(overflow,false);
        }
        capacityCheck(!overflow&&capacityOwner.Pending()==64&&recordedCopies==64,
            "full unresolved provider table refuses before another recording");
        bool retained=true;
        for(const auto& [heldBinding,held]:heldUses)
            retained=capacityOwner.GpuHeld(held,heldBinding.registration,heldBinding.resource)&&
                capacityOwner.Receipts(held).empty()&&!capacityOwner.Complete(held)&&
                !capacityOwner.Cancel(held)&&!capacityOwner.CloseInvocation(held)&&retained;
        capacityCheck(retained&&capacityOwner.Pending()==64,
            "capacity refusal preserves every older unresolved registration");
        capacityAction.reset();Api(list->Close());Api(list->Reset(allocator.Get(),nullptr));
        capacityOwner.Poll();
        bool terminal=true;
        for(const auto& [heldBinding,held]:heldUses)
        {
            const auto receipts=capacityOwner.Receipts(held);
            terminal=receipts.size()==1&&capacityOwner.ValidateReceipt(held,heldBinding,receipts[0])&&
                receipts[0]->Terminal().submissions==0&&!receipts[0]->ProviderReportedSuccess()&&capacityOwner.Complete(held)&&
                capacityOwner.CloseInvocation(held)&&terminal;
        }
        // A mutant that admitted the overflow also owns that possible prefix.
        if(overflow)terminal=capacityOwner.CloseInvocation(overflow)&&terminal;
        capacityCheck(terminal&&capacityOwner.Pending()==0,
            "only actual reset and matching receipts release full provider table");
        auto freshAction=G::BeginLocalAction(G::Record(list.Get()),list.Get(),resource.Get());
        if(!freshAction)throw std::runtime_error("post-capacity recording action unavailable");
        auto freshBinding=binding(freshAction->RecordingTicket());
        auto fresh=capacityOwner.Reserve({freshBinding},*freshAction,list.Get());
        capacityCheck(fresh&&capacityOwner.Pending()==1&&capacityOwner.Cancel(fresh)&&capacityOwner.Pending()==0,
            "genuinely terminal provider table admits a fresh exact registration");
    }
    // Four exact registrations require four distinct receipts; a live CPU borrow
    // still prevents receipts even when an unsubmitted recording has reset.
    auto groupTicket=G::Record(list.Get());auto groupAction=G::BeginLocalAction(groupTicket,list.Get(),resource.Get());
    auto groupBinding=binding(groupTicket);std::vector<ProviderRegistrationBinding> group{groupBinding};
    for(unsigned i=1;i<4;++i){auto next=groupBinding;next.registration=Key("registration",++serial);group.push_back(next);}
    auto groupUse=owner.Reserve(group,*groupAction,list.Get());
    check(owner.Enter(groupUse,*groupAction,contract),"four registrations activate together");groupAction.reset();
    Api(list->Close());Api(list->Reset(allocator.Get(),nullptr));owner.Poll();
    check(owner.Receipts(groupUse).empty()&&!owner.CloseInvocation(groupUse),"CPU borrow blocks terminal receipt");
    owner.Returned(groupUse,false);owner.Poll();auto groupReceipts=owner.Receipts(groupUse);
    check(groupReceipts.size()==4&&owner.Complete(groupUse),"complete four-registration receipt set");
    if(groupReceipts.size()==4)for(unsigned i=0;i<4;++i)
    {
        check(owner.ValidateReceipt(groupUse,group[i],groupReceipts[i]),"matching registration receipt");
        check(!owner.ValidateReceipt(groupUse,group[i],groupReceipts[(i+1)%4]),"duplicate substituted receipt refused");
    }
    check(owner.CloseInvocation(groupUse),"four-registration invocation closed");
    auto duplicateAction=G::BeginLocalAction(G::Record(list.Get()),list.Get(),resource.Get());
    groupBinding.recording=duplicateAction->RecordingTicket();
    check(!owner.Reserve({groupBinding},*duplicateAction,list.Get()),"retired registration cannot be reused");duplicateAction.reset();
    // Entry notifications are CPU scope only, not per-evaluation NGX release.
    struct Calls{unsigned entered=0,returned=0;bool allow=true;};Calls calls;
    ProviderEntryCallbacks callbacks{&calls,[](void* p)noexcept{auto& c=*static_cast<Calls*>(p);++c.entered;return c.allow;},
        [](void* p,bool)noexcept{++static_cast<Calls*>(p)->returned;}};
    try{ProviderEntryScope entry(&callbacks);check(entry.Enter()&&!entry.Enter(),"entry scope is single use");throw std::runtime_error("controlled provider unwind");}
    catch(const std::runtime_error&){}
    check(calls.entered==1&&calls.returned==1,"unwind closes CPU scope exactly once");calls.allow=false;
    {ProviderEntryScope entry(&callbacks);check(!entry.Enter(),"refusal before provider entry");}
    check(calls.returned==1,"refusal does not forge provider return");
    {ProviderEntryScope entry(&callbacks);}
    check(calls.returned==1,"unentered scope has no return");
    // Queue waits keep completion unknown even after the list is sealed.
    ComPtr<ID3D12Fence> gate;Api(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate)));
    auto delayedTicket=G::Record(list.Get());auto delayedAction=G::BeginLocalAction(delayedTicket,list.Get(),resource.Get());
    auto delayedBinding=binding(delayedTicket);auto delayed=owner.Reserve({delayedBinding},*delayedAction,list.Get());
    check(!owner.Enter(delayed,*delayedAction,ProviderLifetimeContract::NgxFeature18()),"wrong provider contract rejected");
    check(owner.Pending()==0&&!owner.Enter(delayed,*delayedAction,contract),"contract refusal cancels unused reservation without retry");
    delayedBinding=binding(delayedTicket);delayed=owner.Reserve({delayedBinding},*delayedAction,list.Get());
    check(owner.Enter(delayed,*delayedAction,contract),"controlled provider entry");owner.Returned(delayed,false);
    delayedAction.reset();Api(list->Close());Api(queue->Wait(gate.Get(),1));ID3D12CommandList* delayedLists[]={list.Get()};
    queue->ExecuteCommandLists(1,delayedLists);
    ComPtr<ID3D12CommandQueue> secondQueue;Api(device->CreateCommandQueue(&q,IID_PPV_ARGS(&secondQueue)));
    secondQueue->ExecuteCommandLists(1,delayedLists);check(G::SealOwnedRecording(list.Get()),"owned recording seal");
    owner.Poll();check(!owner.Complete(delayed)&&owner.Receipts(delayed).empty(),"unknown completion retains sealed recording");
    Api(gate->Signal(1));if(!G::Drain(5000))throw std::runtime_error("delayed WARP drain failed");owner.Poll();
    check(owner.Complete(delayed)&&owner.CloseInvocation(delayed),"observed completion closes sealed recording");
    const auto delayedReceipts=owner.Receipts(delayed);
    check(delayedReceipts.size()==1&&delayedReceipts[0]->Terminal().submissions==2&&
        delayedReceipts[0]->Terminal().completion.size()==2,"receipt covers both observed queues");
    Api(allocator->Reset());Api(list->Reset(allocator.Get(),nullptr));
    // Final native list destruction is an observed non-replayability fact.
    ComPtr<ID3D12GraphicsCommandList> destroyedList;ComPtr<ID3D12CommandAllocator> destroyedAllocator;
    Api(device->CreateCommandAllocator(q.Type,IID_PPV_ARGS(&destroyedAllocator)));
    Api(device->CreateCommandList(0,q.Type,destroyedAllocator.Get(),nullptr,IID_PPV_ARGS(&destroyedList)));
    G::Record(destroyedList.Get());Api(destroyedList->Close());Api(destroyedList->Reset(destroyedAllocator.Get(),nullptr));
    auto destroyedTicket=G::Record(destroyedList.Get());auto destroyedAction=G::BeginLocalAction(destroyedTicket,destroyedList.Get(),resource.Get());
    auto destroyedBinding=binding(destroyedTicket);auto destroyedUse=owner.Reserve({destroyedBinding},*destroyedAction,destroyedList.Get());
    check(owner.Enter(destroyedUse,*destroyedAction,contract),"destruction scenario entered");owner.Returned(destroyedUse,true);
    destroyedAction.reset();Api(destroyedList->Close());ID3D12CommandList* destructionLists[]={destroyedList.Get()};
    queue->ExecuteCommandLists(1,destructionLists);if(!G::Drain(5000))throw std::runtime_error("destruction WARP drain failed");
    destroyedList.Reset();owner.Poll();auto destroyedReceipts=owner.Receipts(destroyedUse);
    check(destroyedReceipts.size()==1&&destroyedReceipts[0]->Terminal().state==G::RecordingTerminalState::LifetimeEnded&&
        owner.CloseInvocation(destroyedUse),"observed destruction closes completed recording");
    // A reservation cannot migrate across reset, even before provider entry.
    auto staleTicket=G::Record(list.Get());auto staleAction=G::BeginLocalAction(staleTicket,list.Get(),resource.Get());
    auto staleBinding=binding(staleTicket);auto staleUse=owner.Reserve({staleBinding},*staleAction,list.Get());
    staleAction.reset();Api(list->Close());Api(list->Reset(allocator.Get(),nullptr));
    staleAction=G::BeginLocalAction(G::Record(list.Get()),list.Get(),resource.Get());
    check(!owner.Enter(staleUse,*staleAction,contract)&&owner.Cancel(staleUse),"reset revokes reservation entry");staleAction.reset();
    auto ticket=G::Record(list.Get());auto action=G::BeginLocalAction(ticket,list.Get(),resource.Get());auto b=binding(ticket);
    auto unused=owner.Reserve({b},*action,list.Get());check(owner.Cancel(unused)&&owner.Pending()==0,"unused reservation cancels without ticket completion");
    check(!owner.Enter(unused,*action,contract),"cancelled registration never reactivates");
    b=binding(ticket);auto pending=owner.Reserve({b},*action,list.Get());
    check(owner.Enter(pending,*action,contract)&&!owner.Cancel(pending),"entered error cannot cancel as unused");
    bool teardownClosed=true;
    std::thread teardown([&]{teardownClosed=owner.BeginTeardown();});teardown.join();
    check(!teardownClosed&&owner.CpuActive(pending),"racing teardown waits for active provider invocation");
    auto racingBinding=binding(ticket);
    check(!owner.Reserve({racingBinding},*action,list.Get()),"teardown excludes racing admission before active return");
    owner.Returned(pending,true);action.reset();Api(list->Close());Api(list->Reset(allocator.Get(),nullptr));owner.Poll();
    check(owner.Complete(pending)&&!owner.BeginTeardown()&&owner.CloseInvocation(pending)&&owner.BeginTeardown(),"reset cancels unsubmitted GPU hold and permits teardown");
    action=G::BeginLocalAction(G::Record(list.Get()),list.Get(),resource.Get());b=binding(action->RecordingTicket());
    check(!owner.Reserve({b},*action,list.Get()),"teardown excludes new registrations");
    action.reset();Api(list->Close());Api(list->Reset(allocator.Get(),nullptr));
    // The prior NR writer can close independently of its final-consumer history
    // tail. Caller Output is registered before the output pass, not guessed from
    // the model's private output allocation after completion.
    NativeProviderRetirementOwner priorWriter(contract,feature);
    auto priorTicket=G::Record(list.Get());auto priorAction=G::BeginLocalAction(priorTicket,list.Get(),resource.Get());
    auto priorBinding=binding(priorTicket);
    auto priorRecording=priorWriter.ReserveRecording(priorBinding.invocation,*priorAction,list.Get(),{resource.Get()});
    check(priorRecording&&priorWriter.RegisterOutputTarget(priorRecording,*priorAction,list.Get(),controlledOutput.Get()),
        "exact caller output registers before writing with real local recording owner");
    auto priorUse=priorWriter.Reserve({priorBinding},*priorAction,list.Get());
    check(priorUse&&priorWriter.Enter(priorUse,*priorAction,contract),"prior writer provider registration entered");
    priorWriter.Returned(priorUse,false);
    auto extraBinding=binding(priorTicket);auto extraUse=priorWriter.Reserve({extraBinding},*priorAction,list.Get());
    check(extraUse&&priorWriter.Enter(extraUse,*priorAction,contract),"second actual registration is retained for complete-snapshot control");
    priorWriter.Returned(extraUse,true);
    check(!priorWriter.ClosePriorWriter(priorUse,priorRecording,priorTicket,controlledOutput.Get()),
        "prior API failure and unsubmitted recording cannot close writer");
    priorAction.reset();Api(list->Close());ID3D12CommandList* priorLists[]={list.Get()};queue->ExecuteCommandLists(1,priorLists);
    check(G::Drain(5000)&&!priorWriter.ClosePriorWriter(priorUse,priorRecording,priorTicket,controlledOutput.Get()),
        "completed replayable prior writer remains unavailable");
    Api(allocator->Reset());Api(list->Reset(allocator.Get(),nullptr));
    check(!priorWriter.ClosePriorWriter(priorUse,priorRecording,priorTicket,controlledOutput.Get()),
        "prior writer closure refuses a second outstanding provider registration");
    check(priorWriter.CloseInvocation(extraUse),"second terminal registration closes through original owner");
    auto priorClosure=priorWriter.ClosePriorWriter(priorUse,priorRecording,priorTicket,controlledOutput.Get());
    check(priorClosure&&priorWriter.ValidatePriorWriter(*priorClosure,priorUse,priorRecording,priorTicket,controlledOutput.Get()),
        "real terminal recording and matched complete provider receipts close prior writer despite API failure");
    check(priorClosure&&!foreign.ValidatePriorWriter(*priorClosure,priorUse,priorRecording,priorTicket,controlledOutput.Get()),
        "prior writer closure cannot cross owner");
    check(priorClosure&&!priorWriter.ValidatePriorWriter(*priorClosure,priorUse,priorRecording,priorTicket,resource.Get()),
        "prior writer closure cannot substitute output");
    check(priorWriter.CloseInvocation(priorUse)&&priorWriter.CloseRecording(priorRecording),
        "prior writer terminal closure retains ordinary physical cleanup path");
    // Observed failure is quarantine, never inferred no effect.
    NativeProviderRetirementOwner failedOwner(contract,feature);
    auto failedTicket=G::Record(list.Get());auto failedAction=G::BeginLocalAction(failedTicket,list.Get(),resource.Get());
    auto failedBinding=binding(failedTicket);auto failedUse=failedOwner.Reserve({failedBinding},*failedAction,list.Get());
    check(failedOwner.Enter(failedUse,*failedAction,contract),"fault scenario entered");failedOwner.Returned(failedUse,false);
    failedAction.reset();Api(list->Close());Api(list->Reset(allocator.Get(),nullptr));
    {std::lock_guard lock(G::State().mutex);failedTicket->failed=true;}
    failedOwner.Poll();check(failedOwner.Receipts(failedUse).empty()&&!failedOwner.Cancel(failedUse)&&
        !failedOwner.CloseInvocation(failedUse)&&!failedOwner.BeginTeardown(),"unknown failed recording quarantines use");
    Api(list->Close());
#else
    check(false,"registration and receipt interface is missing");
#endif
    std::cout<<"Provider lifetime: "<<checks<<" checks, "<<failures<<" failures; WARP lifecycle plumbing only; proprietary provider NOT_RUN\n";
    return failures?1:0;
}
catch(const std::exception& e){std::cerr<<"INFRA_ERROR "<<e.what()<<'\n';return 2;}
