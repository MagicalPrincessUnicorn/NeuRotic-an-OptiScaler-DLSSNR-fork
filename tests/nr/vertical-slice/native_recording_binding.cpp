#define NR_GPU_SAFETY_TEST
#include "../../../OptiScaler/dlssnr/NrGpuSafety.cpp"
#include <nr/lifecycle/NativeLeaseBinding.h>
#include <dlssnr/NrSignalReceipts.h>
#include "../finalizer/finalizer_fixture.h"
#include <dxgi1_4.h>
#include <d3d12sdklayers.h>
#include <thread>
#include <future>
using namespace NrTest002;
using Microsoft::WRL::ComPtr;
namespace L=Neurotic::Lifecycle;
namespace G=DlssNr::GpuSafety;
struct LocalOwner final:L::NativeLeaseOwner
{
    FinalizerTests::Fixture fixture;
    ID3D12Resource* resource=nullptr;
    ID3D12GraphicsCommandList* list=nullptr;
    G::Ticket ticket;
    bool ordered=false,stale=false,remote=false;
    LocalOwner(){fixture.binding.description.use=C::UsageKind::OrderedGpuRead;
        fixture.binding.sameRecording=L::SameRecordingUse{Key("recording"),Key("allocation-reservation"),1,3};
        fixture.facts.current=fixture.binding;fixture.facts.holds={};fixture.facts.callbackActive=L::Fact::Yes;}
    std::optional<L::LeaseBinding> Describe(ID3D12Resource* actual)override
    {if(actual!=resource)return {};auto binding=fixture.binding;if(remote)binding.sameRecording.reset();return binding;}
    L::OwnerFacts Refresh(const L::LeaseBinding&,ID3D12Resource*,ID3D12GraphicsCommandList*,ID3D12CommandQueue*)override
    {return {};}// ordinary submitted-ticket refresh cannot authorize this reservation
    L::OwnerFacts RefreshSameRecording(const L::LeaseBinding& binding,ID3D12Resource* actual,
        ID3D12GraphicsCommandList* recording,ID3D12CommandQueue*,const G::Ticket& current)
    {
        if(actual!=resource||recording!=list||current!=ticket||binding.sameRecording!=fixture.binding.sameRecording)return {};
        auto facts=fixture.facts;
        facts.sameRecording=L::SameRecordingFacts{*binding.sameRecording,L::Fact::Yes,
            ordered?L::Fact::Yes:L::Fact::No,L::Fact::Yes,L::Fact::Yes};
        if(stale)facts.current.description.resource.contentRevision=Known(C::ContentRevision{9});
        return facts;
    }
};
static void Api(HRESULT hr){if(FAILED(hr))throw std::runtime_error("software D3D12 prerequisite or API failed");}
int main(int argc,char**argv)
{
 return Guard([&]{Report r("native-recording-binding",argc==2?std::string_view(argv[1]):std::string_view{});
    ComPtr<ID3D12Debug> debug;Api(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));debug->EnableDebugLayer();
    ComPtr<IDXGIFactory4> factory;ComPtr<IDXGIAdapter> warp;ComPtr<ID3D12Device> device;
    Api(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));Api(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)));
    Api(D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
    ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC q{};q.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
    Api(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)));
    ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;
    Api(device->CreateCommandAllocator(q.Type,IID_PPV_ARGS(&allocator)));
    Api(device->CreateCommandList(0,q.Type,allocator.Get(),nullptr,IID_PPV_ARGS(&list)));
    const auto texture=[&](D3D12_RESOURCE_STATES state){ComPtr<ID3D12Resource> result;D3D12_RESOURCE_DESC d{};
        d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=8;d.Height=4;d.DepthOrArraySize=1;d.MipLevels=1;
        d.SampleDesc.Count=1;d.Format=DXGI_FORMAT_R32_FLOAT;D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        Api(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,state,nullptr,IID_PPV_ARGS(&result)));return result;};
    auto source=texture(D3D12_RESOURCE_STATE_COPY_SOURCE),destination=texture(D3D12_RESOURCE_STATE_COPY_DEST);
    auto owner=std::make_shared<LocalOwner>();owner->resource=destination.Get();owner->list=list.Get();owner->ticket=G::Record(list.Get());
    if(!owner->ticket)throw std::runtime_error("recording unavailable");
    L::LeaseReason reason=L::LeaseReason::None;
    L::NativeLeaseRegistration wrongBinding(owner);
    r.Check("R25.begin-wrong-binding-reason",!wrongBinding.Begin(source.Get(),list.Get(),queue.Get(),owner->ticket,owner->ticket,false,&reason)&&reason==L::LeaseReason::InvalidBinding,true,"NativeLeaseRegistration");
    L::NativeLeaseRegistration before(owner);
    r.Check("T16.native-local-before-preparation",!before.Begin(destination.Get(),list.Get(),queue.Get(),owner->ticket,owner->ticket,false),true,"NativeLeaseRegistration");
    list->CopyResource(destination.Get(),source.Get());
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition={destination.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};
    list->ResourceBarrier(1,&barrier);owner->ordered=true;
    const auto knownQueue=owner->fixture.binding.description.queue;
    owner->fixture.binding.description.queue={};owner->fixture.facts.current=owner->fixture.binding;
    L::NativeLeaseRegistration unassigned(owner);
    r.Check("T16.native-local-unassigned-queue",unassigned.Begin(destination.Get(),list.Get(),nullptr,owner->ticket,owner->ticket,false),true,"NativeLeaseRegistration");
    const auto validateLocal=[&]<class Registration>(Registration& value){
        if constexpr(requires{value.ValidateRecording(destination.Get(),list.Get(),nullptr,owner->ticket,owner->ticket,false);})
            return value.ValidateRecording(destination.Get(),list.Get(),nullptr,owner->ticket,owner->ticket,false);
        else return false;};
    r.Check("T16.native-local-unassigned-fresh-recording",validateLocal(unassigned),true,"NativeLeaseRegistration");
    r.Check("T18.native-local-unassigned-not-submission",!unassigned.ValidateSubmit(destination.Get(),list.Get(),nullptr,owner->ticket,owner->ticket,false),true,"NativeLeaseRegistration");
    owner->stale=true;
    r.Check("T16.native-local-unassigned-stale-content",!validateLocal(unassigned),true,"NativeLeaseRegistration");
    owner->stale=false;
    owner->fixture.binding.description.queue=knownQueue;owner->fixture.facts.current=owner->fixture.binding;
    L::NativeLeaseRegistration registration(owner);
    reason=L::LeaseReason::None;
    const bool admitted=registration.Begin(destination.Get(),list.Get(),queue.Get(),owner->ticket,owner->ticket,false,&reason);
    r.Check("T16.native-local-real-recording",admitted,true,"NativeLeaseRegistration");
    r.Check("R25.begin-success-diagnostic-none",reason==L::LeaseReason::None,true,"NativeLeaseRegistration");
    r.Check("T18.native-local-no-submission",!G::InspectRecording(owner->ticket).submitted,true,"GpuSafety::InspectRecording");
    r.Check("T16.native-local-fresh-use",registration.ValidateSubmit(destination.Get(),list.Get(),queue.Get(),owner->ticket,owner->ticket,false,&reason),true,"NativeLeaseRegistration");
    r.Check("R25.submit-success-diagnostic-none",reason==L::LeaseReason::None,true,"NativeLeaseRegistration");
    owner->fixture.facts.permission=L::Fact::No;
    r.Check("SP03.revoked-use-refused",!registration.ValidateSubmit(destination.Get(),list.Get(),queue.Get(),owner->ticket,owner->ticket,false),true,"NativeLeaseRegistration");
    owner->fixture.facts.permission=L::Fact::Yes;
    owner->fixture.facts.current.description.revoked=Known(true);reason=L::LeaseReason::None;
    r.Check("R25.submit-revoked-reason",!registration.ValidateSubmit(destination.Get(),list.Get(),queue.Get(),owner->ticket,owner->ticket,false,&reason)&&reason==L::LeaseReason::Revoked,true,"NativeLeaseRegistration");
    owner->fixture.facts.current=owner->fixture.binding;
    owner->fixture.facts.current.consumer=Key("foreign-consumer");
    reason=L::LeaseReason::None;
    r.Check("SP03.foreign-consumer-refused",!registration.ValidateSubmit(destination.Get(),list.Get(),queue.Get(),owner->ticket,owner->ticket,false,&reason),true,"NativeLeaseRegistration");
    r.Check("R25.submit-consumer-reason",reason==L::LeaseReason::ConsumerChanged,true,"NativeLeaseRegistration");
    owner->fixture.facts.current=owner->fixture.binding;
    owner->stale=true;
    reason=L::LeaseReason::None;
    r.Check("T16.native-local-stale-content",!registration.ValidateSubmit(destination.Get(),list.Get(),queue.Get(),owner->ticket,owner->ticket,false,&reason),true,"NativeLeaseRegistration");
    r.Check("R25.submit-stale-content-reason",reason==L::LeaseReason::ContentChanged,true,"NativeLeaseRegistration");
    owner->stale=false;
    r.Check("T16.native-local-wrong-object",!registration.ValidateSubmit(source.Get(),list.Get(),queue.Get(),owner->ticket,owner->ticket,false),true,"NativeLeaseRegistration");
    namespace SR=DlssNr::SignalReceipts;
    SR::Tracker<G::Ticket> signals;SR::Epoch epoch{1,1,1,1};
    const auto sample=signals.Begin(epoch,100);
    SR::Fact fact;fact.present=true;const auto shape=destination->GetDesc();
    fact.width=static_cast<uint32_t>(shape.Width);fact.height=shape.Height;fact.format=shape.Format;
    signals.Observe(sample,SR::Signal::Color,fact);
    r.Check("NR.sensor-observed-not-consumed",signals.Read(SR::Signal::Color,epoch,101,SR::Mode::Active).state==SR::State::Detected,true,"actual destination descriptor");
    if(registration.ValidateSubmit(destination.Get(),list.Get(),queue.Get(),owner->ticket,owner->ticket,false))signals.Admit(sample,SR::Signal::Color);
    signals.Record(sample,owner->ticket,SR::Bit(SR::Signal::Color));
    const auto pollSignals=[&]{signals.Poll([](const G::Ticket& t){const auto p=G::InspectRecording(t);return SR::Proof{p.valid&&!(p.reusable&&!p.submitted),p.submitted,p.completed};});};
    pollSignals();r.Check("NR.sensor-recording-not-completion",signals.Read(SR::Signal::Color,epoch,102,SR::Mode::Active).state==SR::State::Pending,true,"actual unsubmitted GPU copy");
    ComPtr<ID3D12Fence> gate,marker;Api(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate)));
    Api(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&marker)));
    Api(queue->Wait(gate.Get(),1));
    Api(list->Close());ID3D12CommandList* lists[]={list.Get()};queue->ExecuteCommandLists(1,lists);
    r.Check("T17.native-local-after-submit-no-record",!registration.ValidateSubmit(destination.Get(),list.Get(),queue.Get(),owner->ticket,owner->ticket,false),true,"NativeLeaseRegistration");
    pollSignals();r.Check("NR.sensor-blocked-fence-not-completion",signals.Read(SR::Signal::Color,epoch,103,SR::Mode::Active).state==SR::State::Pending,true,"actual queue fence gate");
    if(!G::SealOwnedRecording(list.Get()))throw std::runtime_error("fixture recording seal failed");
    ComPtr<ID3D12CommandQueue> other;ComPtr<ID3D12CommandAllocator> otherAllocator;ComPtr<ID3D12GraphicsCommandList> otherList;
    Api(device->CreateCommandQueue(&q,IID_PPV_ARGS(&other)));
    Api(device->CreateCommandAllocator(q.Type,IID_PPV_ARGS(&otherAllocator)));
    Api(device->CreateCommandList(0,q.Type,otherAllocator.Get(),nullptr,IID_PPV_ARGS(&otherList)));
    const auto otherTicket=G::Record(otherList.Get());owner->remote=true;
    L::NativeLeaseRegistration refused(owner);
    r.Check("T18.native-revoked-cross-queue-refused",!refused.Begin(destination.Get(),otherList.Get(),other.Get(),owner->ticket,otherTicket,true),true,"NativeLeaseRegistration");
    Api(other->Signal(marker.Get(),1));HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    if(!event)throw std::runtime_error("fixture event unavailable");Api(marker->SetEventOnCompletion(1,event));
    const auto independent=WaitForSingleObject(event,2000);
    Api(gate->Signal(1)); // release in both success/failure cases before any assertion/teardown
    r.Check("T18.native-no-wait-before-owner-validation",independent==WAIT_OBJECT_0,true,"NativeLeaseRegistration");
    if(independent!=WAIT_OBJECT_0&&WaitForSingleObject(event,5000)!=WAIT_OBJECT_0)throw std::runtime_error("fixture cross queue cleanup timeout");
    CloseHandle(event);Api(otherList->Close());Api(otherList->Reset(otherAllocator.Get(),nullptr));Api(otherList->Close());
    if(!G::Drain(5000))throw std::runtime_error("software fixture drain failed");
    pollSignals();r.Check("NR.sensor-copy-completed",signals.Read(SR::Signal::Color,epoch,104,SR::Mode::Active).state==SR::State::Consumed,true,"actual recording submission and GPU drain");
    ++epoch.resources;signals.Read(SR::Signal::Color,epoch,105,SR::Mode::Active);pollSignals();
    r.Check("NR.sensor-old-resource-no-revival",signals.Read(SR::Signal::Color,epoch,106,SR::Mode::Active).state==SR::State::Pending,true,"resource epoch revocation");
    Api(list->Reset(allocator.Get(),nullptr));
    reason=L::LeaseReason::None;
    r.Check("T17.native-local-reset-refused",!registration.ValidateSubmit(destination.Get(),list.Get(),queue.Get(),owner->ticket,owner->ticket,false,&reason),true,"NativeLeaseRegistration");
    r.Check("R25.submit-reset-recording-reason",reason==L::LeaseReason::RecordingUnavailable,true,"NativeLeaseRegistration");
    L::NativeLeaseRegistration resetBegin(owner);reason=L::LeaseReason::None;
    r.Check("R25.begin-reset-recording-reason",!resetBegin.Begin(destination.Get(),list.Get(),queue.Get(),owner->ticket,owner->ticket,false,&reason)&&reason==L::LeaseReason::RecordingUnavailable,true,"NativeLeaseRegistration");
    Api(list->Close());
#ifdef NR_GPU_RECORDING_ACTION_V1
    // Real software-D3D12 operations and hooks, separate from the synthetic
    // C14/lease owner above. An attached ticket alone never proves open state.
    ComPtr<ID3D12CommandAllocator> guardedAllocator;
    ComPtr<ID3D12GraphicsCommandList> guardedList;
    Api(device->CreateCommandAllocator(q.Type,IID_PPV_ARGS(&guardedAllocator)));
    Api(device->CreateCommandList(0,q.Type,guardedAllocator.Get(),nullptr,IID_PPV_ARGS(&guardedList)));
    auto unknown=G::Record(guardedList.Get());
    r.Check("SP02.recording-new-list-open-unknown",!G::BeginLocalAction(unknown,guardedList.Get(),destination.Get()),true,"GpuSafety::BeginLocalAction");
    Api(guardedList->Close());Api(guardedList->Reset(guardedAllocator.Get(),nullptr));
    auto current=G::Record(guardedList.Get());
    auto action=G::BeginLocalAction(current,guardedList.Get(),destination.Get());
    r.Check("SP02.recording-observed-open-action",action&&action->Current(),true,"GpuSafety::BeginLocalAction");
    r.Check("SP02.recording-single-action",!G::BeginLocalAction(current,guardedList.Get(),destination.Get()),true,"GpuSafety::BeginLocalAction");
    r.Check("SP02.recording-old-ticket-action",!G::BeginLocalAction(unknown,guardedList.Get(),destination.Get()),true,"GpuSafety::BeginLocalAction");
    // A competing close is serialized, not suppressed. The host call completes
    // normally after the owner releases the borrow. No global queue lock is held.
    std::promise<void> entered;auto entering=entered.get_future();
    auto close=std::async(std::launch::async,[&]{entered.set_value();return guardedList->Close();});
    entering.wait();
    const bool waiting=close.wait_for(std::chrono::milliseconds(50))==std::future_status::timeout;
    r.Check("SP02.recording-close-serialized",waiting&&action&&action->Current(),true,"GpuSafety::Close");
    action.reset();Api(close.get());
    r.Check("SP02.recording-closed-list-refused",!G::BeginLocalAction(current,guardedList.Get(),destination.Get()),true,"GpuSafety::BeginLocalAction");
    r.Check("SP02.recording-close-not-retirement",!G::Reusable(current),true,"GpuSafety::Reusable");
    Api(guardedList->Reset(guardedAllocator.Get(),nullptr));
    r.Check("SP02.recording-reset-retires-unsubmitted",G::Reusable(current),true,"GpuSafety::Reusable");
    auto next=G::Record(guardedList.Get());auto nextAction=G::BeginLocalAction(next,guardedList.Get(),destination.Get());
    r.Check("SP02.recording-reset-new-action",nextAction&&nextAction->Current(),true,"GpuSafety::BeginLocalAction");
    Api(guardedList->Close());
    r.Check("SP02.recording-reentrant-close-invalidates",nextAction&&!nextAction->Current(),true,"GpuSafety::Close");
    Api(guardedList->Reset(guardedAllocator.Get(),nullptr));
    auto replacement=G::Record(guardedList.Get());
    r.Check("SP02.recording-reentrant-reset-no-revival",nextAction&&!nextAction->Current()&&replacement!=next,true,"GpuSafety::Reset");
    nextAction.reset();auto replacementAction=G::BeginLocalAction(replacement,guardedList.Get(),destination.Get());
    r.Check("SP02.recording-action-reservation-released",replacementAction&&replacementAction->Current(),true,"GpuSafety::BeginLocalAction");
    replacementAction.reset();Api(guardedList->Close());
    ID3D12CommandList* activeLists[]={guardedList.Get()};queue->ExecuteCommandLists(1,activeLists);
    r.Check("SP02.recording-submission-preserved",G::InspectRecording(replacement).submitted,true,"GpuSafety::Execute");
    r.Check("SP03.host-list-not-replay-safe-on-first-submit",
        !G::InspectRecording(replacement).nonReplayable,true,"GpuSafety::InspectRecording");
    queue->ExecuteCommandLists(1,activeLists);
    const auto replay=G::InspectRecording(replacement);
    r.Check("SP03.host-list-replay-detected",replay.submitted&&!replay.uniqueSubmission&&
        !replay.nonReplayable,true,"GpuSafety::Execute");
    if(!G::Drain(5000))throw std::runtime_error("action fixture drain failed");
    Api(guardedList->Reset(guardedAllocator.Get(),nullptr));Api(guardedList->Close());
#else
    r.Check("SP02.recording-action-owner-required",false,true,"GpuSafety::BeginLocalAction");
#endif
    return r.Finish();});
}
