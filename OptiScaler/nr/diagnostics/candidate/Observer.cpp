#include "Observer.h"
#include "Sink.h"
#include "Reducer.h"
#include "../../../dlssnr/FrameTraceContract.h"
#include <windows.h>
#include <memory>
#include <string>
#include <sstream>
#include <iomanip>

namespace DlssNr::CandidateObserver {
namespace {
Sink sink;
std::atomic_flag snapshotLock=ATOMIC_FLAG_INIT;
// Keep mutable publication outside the image: LTCG can place an unread static
// Snapshot in .rdata when the product has no TryRead consumer. Retain the final
// snapshot until module teardown so closed captures remain readable.
std::unique_ptr<Snapshot> published;
std::atomic<uint32_t> familyStates{0x11}; // each nibble: unknown=0, absent=1, installed=2, failed=3
std::atomic<bool> startupAttempted{false},snapshotAvailable{false};
std::atomic<bool> startupWorker{false},manualBusy{false},protectionBlocked{true},stopping{false};
ManualRouter manualRouter;
std::unique_ptr<ManualSession> manualSession;
#ifdef NR_CANDIDATE_TEST_GATE
std::atomic<bool> captureTestReady{false},captureTestReleased{false};
#endif
struct WorkerConfig {HMODULE module=nullptr;wchar_t destination[2048]{};char session[33]{};};
constexpr uint64_t FileCap=32ull*1024*1024,FooterReserve=65536;
static_assert(sizeof(Sink)+sizeof(Reducer)+2*sizeof(Snapshot)+sizeof(published)+sizeof(WorkerConfig)+64*sizeof(Event)+1024*1024+65536<8*1024*1024);

const char* KindName(Kind kind) noexcept {
    static constexpr const char* names[]={"create_committed","create_placed","create_failed","create_capability_only",
        "create_anomalous_output","create_result_unclassified","create_interface_unclassified","create_alternate_path",
        "coverage","epoch_boundary","session_end"};
    auto n=static_cast<uint32_t>(kind);return n<11?names[n]:"unsupported";
}
std::string DescriptorJson(const Event& e) {
    std::ostringstream o;o<<"{\"api\":\"D3D12\"";
    auto numeric=[&](const char* name,uint64_t value,bool decimal=false) {
        o<<",\""<<name<<"\":";
        if(!e.descriptorKnown) {o<<"{\"state\":\"unknown\",\"reason\":\"descriptor_not_observed\"}";return;}
        o<<"{\"state\":\"known\",\"value\":";
        if(decimal)o<<'"';o<<value;if(decimal)o<<'"';o<<'}';
    };
    o<<",\"dimension\":";
    if(!e.descriptorKnown)o<<"{\"state\":\"unknown\",\"reason\":\"descriptor_not_observed\"}";
    else {
        static constexpr const char* dimensions[]={"unknown","buffer","texture1d","texture2d","texture3d"};
        o<<"{\"state\":\"known\",\"value\":\""<<dimensions[e.descriptor.dimension<=4?e.descriptor.dimension:0]<<"\"}";
    }
    const auto& d=e.descriptor;
    numeric("alignment",d.alignment,true);numeric("width",d.width,true);numeric("height",d.height);
    numeric("depth_or_array_size",d.depth);numeric("mip_levels",d.mips);numeric("format",d.format);
    numeric("layout",d.layout);numeric("resource_flags",d.flags);numeric("sample_count",d.sampleCount);
    numeric("sample_quality",d.sampleQuality);numeric("initial_state",d.initialState);o<<'}';return o.str();
}
std::string EventJson(const Event& e,const Reducer& reducer) {
    std::ostringstream o;
    const char* source=e.source==1?"D3D12_COMMITTED_NORMAL":e.source==2?"D3D12_PLACED_NORMAL":
                       e.source==3?"D3D12_IGD_ALTERNATE":"OBSERVER_CONTROL";
    o<<"{\"type\":\"event\",\"schema_version\":\"2.0\",\"sequence\":\""<<e.sequence
     <<"\",\"epoch\":\""<<e.epoch<<"\",\"window\":\""<<e.window<<"\",\"elapsed_ms\":\""<<e.elapsedMs
     <<"\",\"kind\":\""<<KindName(e.kind)<<"\",\"source\":\""<<source<<"\",\"result\":"<<e.result
     <<",\"descriptor_known\":"<<(e.descriptorKnown?"true":"false")<<",\"descriptor\":"<<DescriptorJson(e)
     <<",\"token\":\""<<reducer.Token(e.sequence)<<"\",\"predecessor\":\""<<reducer.Predecessor(e.sequence)
     <<"\",\"native_observation\":\""<<e.nativeObservation<<"\",\"present_observation\":\""<<e.presentObservation
     <<"\",\"coverage_mask\":"<<e.coverageMask<<",\"reason\":"<<static_cast<uint32_t>(e.reason)<<",\"heap_offset\":";
    if(e.kind==Kind::Placed)o<<'"'<<e.heapOffset<<'"';else o<<"null";
    o<<",\"iid\":\""<<std::hex<<std::setfill('0');
    for(auto b:e.iid)o<<std::setw(2)<<static_cast<unsigned>(b);
    o<<std::dec<<"\",\"heap\":{\"known\":"<<(e.heapKnown?"true":"false")
     <<",\"type\":"<<e.heapType<<",\"cpu_page\":"<<e.cpuPage<<",\"memory_pool\":"<<e.memoryPool
     <<",\"creation_node\":"<<e.creationNode<<",\"visible_node\":"<<e.visibleNode<<",\"flags\":"<<e.heapFlags<<"}}";
    return o.str();
}
bool Write(HANDLE file,const std::string& text,uint64_t& bytes,bool terminal=false) noexcept {
    if(text.size()+1>8192 || bytes+text.size()+1>(terminal?FileCap:FileCap-FooterReserve)) return false;
    DWORD wrote=0;const auto length=static_cast<DWORD>(text.size());
    if(!WriteFile(file,text.data(),length,&wrote,nullptr) || wrote!=length)return false;
    if(!WriteFile(file,"\n",1,&wrote,nullptr) || wrote!=1)return false;
    bytes+=text.size()+1;return true;
}
std::string Header(const WorkerConfig& cfg) {
    return std::string("{\"type\":\"header\",\"schema_version\":\"2.0\",\"profile_id\":\"OBS-CLOSURE-1\",\"session\":\"")+cfg.session+
      "\",\"redaction\":\"session_local_observation_tokens\",\"source_identity\":\"unavailable\",\"protection_policy\":\"unknown\","
      "\"limits\":" +
      R"({"event_bytes":512,"event_slots":4096,"reserved_control_slots":64,"session_events":65536,"data_events":65504,"control_events":32,"worker_batch":64,"window_ms":16,"window_data":512,"window_control":32,"resource_window":8,"frame_data":512,"resource_frame":8,"frame_streams":16,"frames_per_stream":4,"candidate_records":1024,"candidate_bytes":2048,"address_entries":1024,"tombstones":256,"window_quota_entries":1024,"frame_quota_bytes":1048576,"snapshot_bytes":65536,"snapshot_count":2,"display_candidates":64,"scratch_bytes":2097152,"memory_bytes":8388608,"duration_ms":30000,"trace_bytes":33554432,"footer_reserve":65536,"line_bytes":8192,"input_bytes":67108864,"input_events":65536,"report_bytes":4194304,"pack_bytes":67108864})" +
      ",\"family_states\":"+std::to_string(familyStates.load())+",\"capture_start_offset\":\"unavailable\",\"owner_facts\":\"unsupported\"}";
}
DWORD WINAPI Worker(void* argument) noexcept {
    const auto module=static_cast<WorkerConfig*>(argument)->module;
    // All RAII objects finish before final own-module release.
    {
        std::unique_ptr<WorkerConfig> cfg(static_cast<WorkerConfig*>(argument));
        HANDLE file=INVALID_HANDLE_VALUE;
        try {
            std::wstring child=cfg->destination;child+=L"\\candidate-";
            for(unsigned i=0;i<32;i++)child+=static_cast<wchar_t>(cfg->session[i]);
            if(!CreateDirectoryW(child.c_str(),nullptr))throw 1;
            const auto path=child+L"\\trace.jsonl";
            file=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
            if(file==INVALID_HANDLE_VALUE)throw 1;
            auto reducer=std::make_unique<Reducer>();uint64_t bytes=0,serialized=0;
            published=std::make_unique<Snapshot>(); // validated opt-in only; allocation failure refuses capture
            if(!Write(file,Header(*cfg),bytes))throw 1;
            if(!sink.Start())throw 1;
            Event coverage{};coverage.kind=Kind::Coverage;coverage.coverageMask=familyStates.load();
            (void)sink.TryEmit(coverage);
#ifdef NR_CANDIDATE_TEST_GATE
            // Pause after startup coverage; fixture producers cannot race the worker's
            // lock or window advancement while exercising deterministic quota losses.
            captureTestReady.store(true,std::memory_order_release);
            while(sink.Enabled() && !captureTestReleased.load(std::memory_order_acquire))Sleep(1);
#endif
            const ULONGLONG start=GetTickCount64();uint64_t lastSnapshot=0;
            Event batch[64]{};bool writable=true;
            while(sink.Enabled() || !sink.EmptyAfterClose()) {
                const auto elapsed=GetTickCount64()-start;
                if(sink.Enabled()) {
                    sink.AdvanceWindow(elapsed);
                    if(elapsed>=30000)sink.Close(Reason::Deadline);
                }
                auto count=sink.Drain(batch,64);
                for(uint32_t i=0;i<count;i++) {
                    if(!reducer->Apply(batch[i])) {sink.Close(Reason::CounterExhausted);writable=false;}
                    if(writable) {
                        auto line=EventJson(batch[i],*reducer);
                        if(bytes+line.size()+1>FileCap-FooterReserve) {sink.Close(Reason::FileLimit);writable=false;}
                        else if(!Write(file,line,bytes)) {sink.Close(Reason::WriterFailure);writable=false;}
                        else ++serialized;
                    }
                    batch[i]={};
                }
                if(elapsed-lastSnapshot>=250 || !sink.Enabled()) {
                    reducer->AdvanceTime(elapsed);
                    auto snapshot=reducer->Snapshot();snapshot.coverage=sink.Stats();
                    snapshot.coverage.familyStates=familyStates.load();
                    if(!snapshotLock.test_and_set(std::memory_order_acquire)) {
                        *published=snapshot;snapshotAvailable.store(true,std::memory_order_release);
                        snapshotLock.clear(std::memory_order_release);
                    }
                    lastSnapshot=elapsed;
                }
                if(!count && (sink.Enabled() || !sink.EmptyAfterClose()))Sleep(16);
            }
            const auto stats=sink.Stats();std::ostringstream footer;
            footer<<"{\"type\":\"footer\",\"schema_version\":\"2.0\",\"last_sequence\":\""<<stats.sequence
                  <<"\",\"enqueued\":\""<<stats.enqueued<<"\",\"serialized\":\""<<serialized<<"\",\"dropped\":\""<<stats.dropped
                  <<"\",\"drop_exact\":"<<(stats.dropExact?"true":"false")<<",\"loss_mask\":"<<stats.lossMask
                  <<",\"high_water\":"<<stats.highWater<<",\"reason\":"<<static_cast<uint32_t>(stats.reason)
                  <<",\"elapsed_ms\":\""<<(GetTickCount64()-start)<<"\",\"complete_transport\":"<<(stats.complete && serialized==stats.enqueued?"true":"false")
                  <<",\"bytes_before_footer\":\""<<bytes<<"\",\"families\":[";
            for(unsigned i=0;i<4;i++) {
                if(i)footer<<',';
                footer<<"{\"enqueued\":\""<<stats.familyEnqueued[i]<<"\",\"dropped\":\""<<stats.familyDropped[i]
                      <<"\",\"drop_exact\":"<<(stats.familyDropExact[i]?"true":"false")<<'}';
            }
            footer<<"],\"budget_refusals\":{\"profile\":\"quota-reasons-1\",\"reasons\":[";
            for(uint32_t i=0;i<BudgetCauseCount;i++) {
                if(i)footer<<',';
                footer<<"{\"reason\":\""<<BudgetCauseNames[i]<<"\",\"count\":\""<<stats.budgetRefused[i]
                      <<"\",\"exact\":"<<(stats.budgetRefusalExact[i]?"true":"false")<<'}';
            }
            footer<<"]}}";
            (void)Write(file,footer.str(),bytes,true);reducer->ClearPrivate();
        } catch(...) {sink.Close(Reason::WriterFailure);}
        if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);
    }
    startupWorker.store(false,std::memory_order_release);
    FreeLibraryAndExitThread(module,0);
}
}
bool Enabled() noexcept {return sink.Enabled() || manualBusy.load(std::memory_order_acquire);}
bool ManualBusy() noexcept {return manualBusy.load(std::memory_order_acquire);}
ManualLease AcquireManualLease() noexcept {return ManualBusy()?manualRouter.Pin():ManualLease{};}
bool BeginManual(uint64_t operation) noexcept {
    if(!operation || protectionBlocked.load() || stopping.load() || startupWorker.load())return false;
    bool expected=false;if(!manualBusy.compare_exchange_strong(expected,true))return false;
    try {
        auto next=std::make_unique<ManualSession>(operation);
        if(!manualRouter.Open(*next)) {manualBusy.store(false);return false;}
        manualSession=std::move(next);
        Event coverage{};coverage.kind=Kind::Coverage;coverage.coverageMask=familyStates.load();
        auto lease=manualRouter.Pin();(void)lease.Emit(coverage);return true;
    }catch(...) {manualBusy.store(false);return false;}
}
void AdvanceManual(uint64_t elapsed) noexcept {
    if(manualSession) {if(elapsed>=2000 || stopping.load())manualSession->Close();manualSession->Advance(elapsed);}
}
bool EndManual(Snapshot& result) noexcept {
    if(!manualSession)return true;
    manualSession->Close();manualSession->Advance(2000);
#ifdef NR_CANDIDATE_MANUAL_TEST
    extern void BeforeManualRetireForTest() noexcept;
    BeforeManualRetireForTest();
#endif
    if(!manualRouter.Retire(*manualSession))return false;
    // No producer can enqueue or hold the sink lock after successful retirement.
    manualSession->Advance(2000);
    result=manualSession->Read();result.coverage.familyStates=familyStates.load();
    manualSession.reset();manualBusy.store(false,std::memory_order_release);return true;
}
#ifdef NR_CANDIDATE_TEST_GATE
bool CaptureTestGateReady() noexcept {return captureTestReady.load(std::memory_order_acquire);}
void ReleaseCaptureTestGate() noexcept {captureTestReleased.store(true,std::memory_order_release);}
#endif
EmitResult TryEmit(const Event& event) noexcept {return sink.TryEmit(event);}
ImportResult TryObserveOwnerFact(const OwnerFact& fact) noexcept {
    if(!fact.available || !Enabled())return ImportResult::Unavailable;
    if(fact.major!=2 || fact.minor!=0)return ImportResult::UnsupportedContract;
    // This version declares owner adapters unsupported. A real owner-version
    // adapter is required before transporting or joining any supplied identity.
    Event event{};event.kind=Kind::OwnerFact;
    if(ManualBusy()) {
        auto lease=AcquireManualLease();if(lease)(void)lease.Emit(event);
        return ImportResult::UnsupportedContract;
    }
    const auto result=TryEmit(event);
    if(result==EmitResult::Unsupported)return ImportResult::UnsupportedContract;
    return ImportResult::Unavailable;
}
void RequestClose(Reason reason) noexcept {sink.Close(reason);if(reason==Reason::SourceDetach)stopping.store(true);}
void NoteNested() noexcept {sink.NoteLoss(Reason::Nested);}
ReadResult TryRead(Snapshot& destination) noexcept {
    if(!snapshotAvailable.load(std::memory_order_acquire))return ReadResult::Unavailable;
    if(snapshotLock.test_and_set(std::memory_order_acquire))return ReadResult::Contended;
    destination=*published;snapshotLock.clear(std::memory_order_release);
    destination.coverage=sink.Stats(); // a pending publication must not conceal newer loss
    destination.coverage.familyStates=familyStates.load();
    return Enabled()?ReadResult::Snapshot:ReadResult::Closed;
}
void ObserveCoverage(uint32_t states) noexcept {
    const auto previous=familyStates.exchange(states,std::memory_order_acq_rel);
    if(!Enabled())return;
    Event e{};e.kind=Kind::Coverage;e.coverageMask=states;(void)TryEmit(e);
    if(states==0x11 && previous!=0x11) {e.kind=Kind::Boundary;e.reason=Reason::SourceDetach;(void)TryEmit(e);}
}
void StartCold(bool protectionDenied) noexcept {
    const DWORD lastError=GetLastError();
    if(startupAttempted.exchange(true,std::memory_order_acq_rel))return;
    protectionBlocked.store(protectionDenied);
    HMODULE reference=nullptr;
    try {
        WorkerConfig initial{};char mode[4]{};
        const auto modeLength=GetEnvironmentVariableA("NEUROTIC_CANDIDATE_OBSERVER",mode,sizeof(mode));
        const auto sessionLength=GetEnvironmentVariableA("NEUROTIC_FRAME_TRACE_SESSION",initial.session,sizeof(initial.session));
        const auto pathLength=GetEnvironmentVariableW(L"NEUROTIC_DIAGNOSTIC_DIRECTORY",initial.destination,2048);
        if(protectionDenied || modeLength!=1 || mode[0]!='1' || sessionLength!=32 ||
           !FrameTrace::ValidSession(std::string_view(initial.session,32)) || pathLength==0 || pathLength>=2000)throw 1;
        if(pathLength<3 || initial.destination[1]!=L':' || (initial.destination[2]!=L'\\' && initial.destination[2]!=L'/'))throw 1;
        wchar_t root[4]={initial.destination[0],L':',L'\\',0};const auto drive=GetDriveTypeW(root);
        if(drive!=DRIVE_FIXED && drive!=DRIVE_REMOVABLE)throw 1;
        const auto attributes=GetFileAttributesW(initial.destination);
        if(attributes==INVALID_FILE_ATTRIBUTES || !(attributes&FILE_ATTRIBUTE_DIRECTORY) || (attributes&FILE_ATTRIBUTE_REPARSE_POINT))throw 1;
        // A drive-local spelling must not redirect through a junction to another destination.
        for(DWORD i=3;i<pathLength;i++)if(initial.destination[i]==L'\\' || initial.destination[i]==L'/') {
            const auto saved=initial.destination[i];initial.destination[i]=0;
            const auto parent=GetFileAttributesW(initial.destination);initial.destination[i]=saved;
            if(parent==INVALID_FILE_ATTRIBUTES || (parent&FILE_ATTRIBUTE_REPARSE_POINT))throw 1;
        }
        auto cfg=std::make_unique<WorkerConfig>(initial);
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(&StartCold),&reference))throw 1;
        cfg->module=reference;
        startupWorker.store(true);
        const auto thread=CreateThread(nullptr,1024*1024,Worker,cfg.get(),0,nullptr);
        if(!thread)throw 1;
        cfg.release();reference=nullptr;CloseHandle(thread);
    }catch(...) {startupWorker.store(false);if(reference)FreeLibrary(reference);}
    SetLastError(lastError);
}
}
