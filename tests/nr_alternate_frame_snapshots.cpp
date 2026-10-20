#define NR_GPU_SAFETY_TEST
#include "../OptiScaler/dlssnr/NrGpuSafety.cpp"
#include "afnr_warp_fixture.h"
#include <array>
using namespace DlssNr::GpuSafety;
static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::cerr<<__LINE__<<": " #x "\n";std::exit(1);}}while(false)
static ID3D12CommandList* pausedSource=nullptr;
static ID3D12Fence* sourceFinished=nullptr;
static ID3D12Fence* snapshotGate=nullptr;
static void STDMETHODCALLTYPE SubmitWithCopyGap(ID3D12CommandQueue* queue,UINT count,ID3D12CommandList* const* lists) {
    originalExecute(queue,count,lists);
    if(count==1&&lists[0]==pausedSource) {
        pausedSource=nullptr;HR(queue->Signal(sourceFinished,1));HR(queue->Wait(snapshotGate,1));
    }
}
static void STDMETHODCALLTYPE IgnoreInvalidBatch(ID3D12CommandQueue*,UINT,ID3D12CommandList* const*){}
struct List {
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    Ticket ticket;
    explicit List(ID3D12Device* device,D3D12_COMMAND_LIST_TYPE type) {
        HR(device->CreateCommandAllocator(type,IID_PPV_ARGS(&allocator)));
        HR(device->CreateCommandList(0,type,allocator.Get(),nullptr,IID_PPV_ARGS(&list)));
        CHECK(PrepareRecordingHooks(list.Get()));HR(list->Close());HR(list->Reset(allocator.Get(),nullptr));
        ticket=Record(list.Get());CHECK(ticket);
    }
    void Reset(){HR(list->Reset(allocator.Get(),nullptr));HR(list->Close());}
};
void Run(D3D12_COMMAND_LIST_TYPE type){
    AfnrWarp w(type);
    auto input=w.Texture(DXGI_FORMAT_R32_UINT,8,8),replacement=w.Texture(DXGI_FORMAT_R32_UINT,8,8);
    auto stage=w.Texture(DXGI_FORMAT_R32_UINT,8,8),final=w.Texture(DXGI_FORMAT_R32_UINT,8,8);
    std::array<unsigned,64> first,second,zero{};first.fill(17);second.fill(83);
    w.Upload(input.Get(),first.data(),4,D3D12_RESOURCE_STATE_COPY_SOURCE);
    w.Upload(replacement.Get(),second.data(),4,D3D12_RESOURCE_STATE_COPY_SOURCE);
    w.Upload(stage.Get(),zero.data(),4,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    w.Upload(final.Get(),zero.data(),4,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    const std::array resources{stage.Get(),final.Get()};
    const std::array copies{DerivedSnapshotCopy{stage.Get(),final.Get()}};
    const auto capture=[&](ID3D12GraphicsCommandList* list) {
        AfnrWarp::Barrier(list,stage.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_DEST);
        list->CopyResource(stage.Get(),input.Get());
        AfnrWarp::Barrier(list,stage.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    };
    // Mutation caught: taking the snapshot on each host replay, or permitting
    // replay to race the first copy, would change the final pixels or complete
    // the second queue before the first producer is released.
    List host(w.device.Get(),type);DerivedArtifactRef snapshot;
    auto payload=std::make_shared<int>(17);std::weak_ptr<int> retainedPayload=payload;
    auto action=BeginLocalAction(host.ticket,host.list.Get(),resources);CHECK(action);
    CHECK(PrepareDerivedSnapshot(*action,copies,snapshot,payload,{}));payload.reset();capture(host.list.Get());action.reset();HR(host.list->Close());
    ComPtr<ID3D12Fence> gate;HR(w.device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate)));
    ComPtr<ID3D12Fence> sourceDone;HR(w.device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&sourceDone)));
    pausedSource=host.list.Get();sourceFinished=sourceDone.Get();snapshotGate=gate.Get();
    ID3D12CommandList* commands[]{host.list.Get()};ExecuteOn(SubmitWithCopyGap,w.queue.Get(),1,commands);
    HANDLE sourceEvent=CreateEventW(nullptr,FALSE,FALSE,nullptr);CHECK(sourceEvent);HR(sourceDone->SetEventOnCompletion(1,sourceEvent));
    CHECK(WaitForSingleObject(sourceEvent,5000)==WAIT_OBJECT_0);CloseHandle(sourceEvent);
    // D3D12 requires prior host-list execution to finish before legal replay.
    // The actual source is now finished, but its separate copy is still gated.
    CHECK(!InspectRecording(host.ticket,nullptr).nonReplayable);
    const auto proof=InspectRecording(snapshot->producer,nullptr);
    CHECK(proof.valid&&proof.uniqueSubmission&&proof.nonReplayable&&!proof.completed);
    D3D12_COMMAND_QUEUE_DESC desc{};desc.Type=type;ComPtr<ID3D12CommandQueue> other;HR(w.device->CreateCommandQueue(&desc,IID_PPV_ARGS(&other)));
    List reader(w.device.Get(),type);const std::array finals{final.Get()};
    auto readAction=BeginLocalAction(reader.ticket,reader.list.Get(),finals);CHECK(readAction);
    auto lease=ReserveDerivedRead(snapshot,*readAction,nullptr);CHECK(lease);
    CHECK(lease->Readiness()==DerivedReadiness::DependencyOrderedImmutable);
    readAction.reset();HR(reader.list->Close());
    commands[0]=host.list.Get();other->ExecuteCommandLists(1,commands); // still blocked first copy, genuine replay
    ComPtr<ID3D12Fence> done;HR(w.device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&done)));HR(other->Signal(done.Get(),1));
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);CHECK(event);
    HR(done->SetEventOnCompletion(1,event));CHECK(WaitForSingleObject(event,50)==WAIT_TIMEOUT);
    commands[0]=reader.list.Get();other->ExecuteCommandLists(1,commands);CHECK(SealOwnedRecording(reader.list.Get()));
    HR(other->Signal(done.Get(),2));
    CHECK(!DerivedArtifactReusable(snapshot));HR(gate->Signal(1));CHECK(WaitForSingleObject(event,5000)==WAIT_OBJECT_0);CloseHandle(event);
    event=CreateEventW(nullptr,FALSE,FALSE,nullptr);CHECK(event);HR(done->SetEventOnCompletion(2,event));
    CHECK(WaitForSingleObject(event,5000)==WAIT_OBJECT_0);CloseHandle(event);
    CHECK(!Reusable(host.ticket));CHECK(DerivedArtifactReusable(snapshot));
    // Change the source visible to the recorded host commands, then replay.
    w.Run([&](auto* list){AfnrWarp::Barrier(list,input.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
        list->CopyResource(input.Get(),replacement.Get());
        AfnrWarp::Barrier(list,input.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COPY_SOURCE);});
    commands[0]=host.list.Get();other->ExecuteCommandLists(1,commands);HR(other->Signal(done.Get(),3));
    event=CreateEventW(nullptr,FALSE,FALSE,nullptr);CHECK(event);HR(done->SetEventOnCompletion(3,event));
    CHECK(WaitForSingleObject(event,5000)==WAIT_OBJECT_0);CloseHandle(event);
    CHECK(InspectRecording(snapshot->producer,nullptr).uniqueSubmission);
    auto saved=w.Read(final.Get(),4),replayed=w.Read(stage.Get(),4);
    CHECK(saved.size()==sizeof(first)&&memcmp(saved.data(),first.data(),sizeof(first))==0);
    CHECK(replayed.size()==sizeof(second)&&memcmp(replayed.data(),second.data(),sizeof(second))==0);
    lease.reset();snapshot.reset();Pending();CHECK(!retainedPayload.expired());
    host.Reset();CHECK(Reusable(host.ticket));host.ticket.reset();reader.ticket.reset();Pending();CHECK(retainedPayload.expired());
    CHECK(DerivedAllocatedBytes()==0);
    // A cancelled host recording must make its never-submitted owned copy
    // terminal even while the renderer still retains the host/snapshot tickets.
    for(bool destroy:{false,true}) {
        List cancelled(w.device.Get(),type);DerivedArtifactRef unused;
        auto borrow=BeginLocalAction(cancelled.ticket,cancelled.list.Get(),resources);CHECK(borrow);
        CHECK(PrepareDerivedSnapshot(*borrow,copies,unused,{},{}));borrow.reset();HR(cancelled.list->Close());
        if(destroy)cancelled.list.Reset();else cancelled.Reset();
        CHECK(Reusable(cancelled.ticket));CHECK(DerivedArtifactReusable(unused));
        CHECK(!InspectRecording(unused->producer,nullptr).submitted);
        List attempt(w.device.Get(),type);auto access=BeginLocalAction(attempt.ticket,attempt.list.Get(),finals);CHECK(access);
        CHECK(!ReserveDerivedRead(unused,*access,nullptr));access.reset();HR(attempt.list->Close());attempt.Reset();
        unused.reset();cancelled.ticket.reset();attempt.ticket.reset();Pending();CHECK(DerivedAllocatedBytes()==0);
    }
    // Duplicate first-batch occurrence preserves host execution but cannot
    // establish a unique first result. Cancel our copy and keep it unreadable.
    List duplicate(w.device.Get(),type);DerivedArtifactRef ambiguous;
    auto borrow=BeginLocalAction(duplicate.ticket,duplicate.list.Get(),resources);CHECK(borrow);
    CHECK(PrepareDerivedSnapshot(*borrow,copies,ambiguous,{},{}));capture(duplicate.list.Get());borrow.reset();HR(duplicate.list->Close());
    // Duplicate first-batch lists are invalid D3D12 input. Exercise the hook's
    // refusal/cancellation branch with a no-op forwarder, not an invalid GPU run.
    ID3D12CommandList* twice[]{duplicate.list.Get(),duplicate.list.Get()};ExecuteOn(IgnoreInvalidBatch,w.queue.Get(),2,twice);
    CHECK(!InspectRecording(ambiguous->producer,nullptr).submitted);CHECK(DerivedArtifactReusable(ambiguous));
    w.Wait();duplicate.Reset();ambiguous.reset();duplicate.ticket.reset();Pending();CHECK(DerivedAllocatedBytes()==0);
    w.Clean();
}
int main(){Run(D3D12_COMMAND_LIST_TYPE_DIRECT);Run(D3D12_COMMAND_LIST_TYPE_COMPUTE);
    std::cout<<"AFNR snapshots PASS "<<checks<<" checks; DIRECT/COMPUTE pending replay, immutable pixels, cancellation, duplicate batch, retained payload\n";}
