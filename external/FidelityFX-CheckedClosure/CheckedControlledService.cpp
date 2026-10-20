#include "CheckedControlledService.h"
#include "CheckedExperimentalService.h"
#include <atomic>
#include <limits>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <random>
namespace {
using namespace ffx::nr;
template<class F> FfxNrStatusV1 safe(F&& f) noexcept {try{return f();}catch(...){return FFX_NR_API_FAILURE_RETAINED;}}
ExperimentalServiceV1 native() {ExperimentalServiceV1 t{};t.manualOptIn=ExperimentalManualOptInV1;if(ffxNeuRoticQueryExperimentalServiceV1(&t)!=FFX_NR_COMPLETE)return {};return t;}
struct Entry {uint64_t cookie=0,refs=0;std::shared_ptr<OwnedOutputLease> output;std::shared_ptr<AlgorithmLease> algorithm;};
struct Registry {
 std::mutex mutex;uint64_t next=0,salt=0;std::unordered_map<uint64_t,Entry> entries;
 std::vector<FfxNrContextTicketV1> failures;
 Registry(){std::random_device r;salt=(uint64_t(r())<<32)^r()^uint64_t(reinterpret_cast<uintptr_t>(this));if(!salt)salt=1;}
};
// Intentionally retained at process teardown: unresolved SDK work cannot be freed by CRT exit ordering.
Registry& registry(){static auto* value=new Registry;return *value;}
std::atomic<bool> unknownFailure{false};
bool same(const FfxNrContextTicketV1&a,const FfxNrContextTicketV1&b){return a.owner_id==b.owner_id&&a.incarnation==b.incarnation&&a.registration_generation==b.registration_generation;}
void fail(const FfxNrContextTicketV1& c) noexcept {try{auto&r=registry();std::lock_guard l(r.mutex);r.failures.push_back(c);}catch(...){unknownFailure=true;}}
bool failed(const FfxNrContextTicketV1& c){auto&r=registry();std::lock_guard l(r.mutex);if(unknownFailure)return true;for(auto&v:r.failures)if(same(v,c))return true;return false;}
template<class H,class P> H add(const P& p){if(!p)return {};auto&r=registry();std::lock_guard l(r.mutex);
 for(auto&[id,e]:r.entries){bool match=false;if constexpr(std::is_same_v<P,std::shared_ptr<OwnedOutputLease>>)match=e.output==p;else match=e.algorithm==p;
 if(match&&e.refs){if(e.refs==UINT64_MAX)throw 1;++e.refs;return {id,e.cookie};}}
 if(r.next==UINT64_MAX)throw 1;auto id=++r.next;Entry e{};e.cookie=r.salt^(id*UINT64_C(0x9e3779b97f4a7c15));e.refs=1;
 if constexpr(std::is_same_v<P,std::shared_ptr<OwnedOutputLease>>)e.output=p;else e.algorithm=p;
 r.entries.emplace(id,e);return {id,e.cookie};}
template<class H> Entry lookup(H h){auto&r=registry();std::lock_guard l(r.mutex);auto i=r.entries.find(h.id);if(i==r.entries.end()||!i->second.refs||i->second.cookie!=h.cookie)return {};
 if constexpr(std::is_same_v<H,FfxNrOwnedOutputHandleV1>){if(!i->second.output)return {};}else{if(!i->second.algorithm)return {};}
 return i->second;}
template<class H> FfxNrStatusV1 retain(H h){return safe([&]()->FfxNrStatusV1{auto p=lookup(h);if(!p.refs)return FFX_NR_STALE_CONTEXT;auto&r=registry();std::lock_guard l(r.mutex);auto&i=r.entries.at(h.id);if(!i.refs)return FFX_NR_STALE_CONTEXT;if(i.refs==UINT64_MAX)return FFX_NR_BUSY;++i.refs;return FFX_NR_COMPLETE;});}
template<class H> FfxNrStatusV1 release(H h){return safe([&]()->FfxNrStatusV1{auto p=lookup(h);if(!p.refs)return FFX_NR_STALE_CONTEXT;
 // Inspect outside the registry lock; partial or uncertain destruction retains the creating-module object.
 bool terminal=false;if(p.output){auto s=p.output->inspectRetirement();terminal=s.admissionClosed&&s.actionsQuiescent&&s.sdkComplete&&!s.failed&&!failed(p.output->inspect().context);}else terminal=p.algorithm->inspect().released;
 auto&r=registry();{std::lock_guard l(r.mutex);auto i=r.entries.find(h.id);if(i==r.entries.end()||!i->second.refs)return FFX_NR_STALE_CONTEXT;if(--i->second.refs==0&&terminal)r.entries.erase(i);}return FFX_NR_COMPLETE;});}
struct BorrowOutput {FfxNrOwnedOutputHandleV1 handle;explicit BorrowOutput(const std::shared_ptr<OwnedOutputLease>&p):handle(add<FfxNrOwnedOutputHandleV1>(p)){}~BorrowOutput(){release(handle);}};
struct BorrowAlgorithm {FfxNrAlgorithmHandleV1 handle;explicit BorrowAlgorithm(const std::shared_ptr<AlgorithmLease>&p):handle(add<FfxNrAlgorithmHandleV1>(p)){}~BorrowAlgorithm(){release(handle);}};
FfxNrOwnedWriteReceiptV1 convert(const OwnedWriteReceipt&r){return {r.prepareResult,r.callbackResult,r.closeResult,r.signalResult,uint32_t(r.executeInvoked),uintptr_t(r.fence),r.target};}
struct ConsumerBridge {FfxNrControlledConsumerObserverV1 callbacks;std::weak_ptr<AlgorithmLease> algorithm;std::atomic<bool> fault{false};};
FfxNrStatusV1 consumerBegin(void* u,const std::shared_ptr<OwnedOutputLease>&o,const std::shared_ptr<AlgorithmLease>&a,const FfxNrDispatchTicketV1*d,void**g) noexcept {
 auto&b=*static_cast<ConsumerBridge*>(u);
 try{if(b.fault||failed(a->inspect().swapchain))return FFX_NR_API_FAILURE_RETAINED;BorrowOutput bo(o);BorrowAlgorithm ba(a);return b.callbacks.begin_consumer(b.callbacks.user,bo.handle,ba.handle,d,g);}catch(...){b.fault=true;fail(a->inspect().swapchain);a->closeAdmission();return FFX_NR_API_FAILURE_RETAINED;}}
void consumerEnd(void*u,void*g,int32_t result) noexcept{auto&b=*static_cast<ConsumerBridge*>(u);try{b.callbacks.end_consumer(b.callbacks.user,g,result);}catch(...){b.fault=true;if(auto p=b.algorithm.lock()){fail(p->inspect().swapchain);p->closeAdmission();}}}
FfxNrStatusV1 FFX_NR_CALL selectAlgorithm(void*a,void*s,const FfxNrControlledConsumerObserverV1*o,FfxNrAlgorithmHandleV1*out) noexcept{return safe([&]()->FfxNrStatusV1{
 if(!out||!o||o->size!=sizeof(*o)||o->version!=1||!o->begin_consumer||!o->end_consumer)return FFX_NR_INVALID_ARGUMENT;*out={};auto t=native();if(!t.select_algorithm)return FFX_NR_UNSUPPORTED;
 // The native owner stores this callback address. Retain even if enrollment fails after partial entry.
 auto*b=new ConsumerBridge{*o};AlgorithmConsumerObserver bridge{b,consumerBegin,consumerEnd};std::shared_ptr<AlgorithmLease> p;
 auto status=t.select_algorithm(a,s,&bridge,&p);b->algorithm=p;if(status!=FFX_NR_COMPLETE)return status;if(!p)return FFX_NR_API_FAILURE_RETAINED;*out=add<FfxNrAlgorithmHandleV1>(p);return FFX_NR_COMPLETE;});}
struct WriterBridge{FfxNrControlledWriterCallbacksV1 callbacks;void* user;FfxNrContextTicketV1 context;bool fault=false;};
HRESULT prepare(void*u,const std::shared_ptr<OwnedOutputLease>&p,ID3D12GraphicsCommandList*l)noexcept{auto&b=*static_cast<WriterBridge*>(u);try{BorrowOutput h(p);return b.callbacks.prepare(b.user,h.handle,uintptr_t(l));}catch(...){b.fault=true;fail(b.context);p->closeAdmission();return E_FAIL;}}
HRESULT write(void*u,const std::shared_ptr<OwnedOutputLease>&p,ID3D12GraphicsCommandList*l)noexcept{auto&b=*static_cast<WriterBridge*>(u);if(b.fault)return E_FAIL;try{BorrowOutput h(p);return b.callbacks.write(b.user,h.handle,uintptr_t(l));}catch(...){b.fault=true;fail(b.context);p->closeAdmission();return E_FAIL;}}
HRESULT beginSubmit(void*u,const std::shared_ptr<OwnedOutputLease>&p,void**g)noexcept{auto&b=*static_cast<WriterBridge*>(u);if(b.fault)return E_FAIL;try{BorrowOutput h(p);return b.callbacks.begin_submit(b.user,h.handle,g);}catch(...){b.fault=true;fail(b.context);p->closeAdmission();return E_FAIL;}}
void endSubmit(void*u,void*g,const OwnedWriteReceipt&r)noexcept{auto&b=*static_cast<WriterBridge*>(u);try{auto pod=convert(r);b.callbacks.end_submit(b.user,g,&pod);}catch(...){b.fault=true;fail(b.context);}}
FfxNrStatusV1 FFX_NR_CALL writeOutput(const FfxNrContextTicketV1*c,const FfxNrControlledWriterCallbacksV1*w,void*u,FfxNrOwnedWriteReceiptV1*out)noexcept{return safe([&]()->FfxNrStatusV1{
 if(!c||!out||!w||w->size!=sizeof(*w)||w->version!=1||!w->prepare||!w->write||!w->begin_submit||!w->end_submit)return FFX_NR_INVALID_ARGUMENT;*out={};if(failed(*c))return FFX_NR_API_FAILURE_RETAINED;auto t=native();if(!t.write_owned_output)return FFX_NR_UNSUPPORTED;
 WriterBridge b{*w,u,*c};OwnedWriterCallbacks callbacks{prepare,write,beginSubmit,endSubmit};OwnedWriteReceipt r{};auto status=t.write_owned_output(c,&callbacks,&b,&r);*out=convert(r);return b.fault?FFX_NR_API_FAILURE_RETAINED:status;});}
struct SubmitBridge{FfxNrSubmissionObserverV1 callbacks;FfxNrContextTicketV1 context;};
FfxNrStatusV1 FFX_NR_CALL submitBegin(void*u,const FfxNrDispatchTicketV1*d,void**g)noexcept{auto&b=*static_cast<SubmitBridge*>(u);return safe([&]()->FfxNrStatusV1{if(failed(b.context))return FFX_NR_API_FAILURE_RETAINED;try{return b.callbacks.begin_submit(b.callbacks.user,d,g);}catch(...){fail(b.context);return FFX_NR_API_FAILURE_RETAINED;}});}
void FFX_NR_CALL submitEnd(void*u,void*g,const FfxNrSubmitResultV1*r)noexcept{auto&b=*static_cast<SubmitBridge*>(u);try{b.callbacks.end_submit(b.callbacks.user,g,r);}catch(...){fail(b.context);}}
FfxNrStatusV1 FFX_NR_CALL setObserver(const FfxNrContextTicketV1*c,const FfxNrSubmissionObserverV1*o)noexcept{return safe([&]()->FfxNrStatusV1{if(!c||!o||o->size!=sizeof(*o)||o->version!=1||!o->begin_submit||!o->end_submit)return FFX_NR_INVALID_ARGUMENT;if(failed(*c))return FFX_NR_API_FAILURE_RETAINED;auto t=native();if(!t.set_submit_observer)return FFX_NR_UNSUPPORTED;auto*b=new SubmitBridge{*o,*c};FfxNrSubmissionObserverV1 bridge{sizeof(bridge),1,b,submitBegin,submitEnd};return t.set_submit_observer(c,&bridge);});}
FfxNrStatusV1 FFX_NR_CALL outputInspect(FfxNrOwnedOutputHandleV1 h,FfxNrOwnedOutputSnapshotV1*out)noexcept{return safe([&]()->FfxNrStatusV1{if(!out)return FFX_NR_INVALID_ARGUMENT;*out={};auto p=lookup(h).output;if(!p)return FFX_NR_STALE_CONTEXT;auto s=p->inspect();const auto&d=s.description;
 *out={s.context,uintptr_t(s.resource),{uint32_t(d.Dimension),d.Alignment,d.Width,d.Height,d.DepthOrArraySize,d.MipLevels,uint32_t(d.Format),d.SampleDesc.Count,d.SampleDesc.Quality,uint32_t(d.Layout),uint32_t(d.Flags)},s.allocationGeneration,uintptr_t(s.queue),uintptr_t(s.writerList),s.writerRecording,uintptr_t(s.consumerList),s.consumerRecording,uint32_t(s.writerActive),uint32_t(s.writerSubmitted),uint32_t(s.writerRetired),uint32_t(s.callbackActive),uint32_t(s.admissionOpen),uint32_t(s.actionActive),uint32_t(s.sdkComplete),uint32_t(s.failed||failed(s.context)),uint32_t(s.ordinaryEscapeExcluded),uint32_t(s.recyclingExcluded)};return FFX_NR_COMPLETE;});}
FfxNrStatusV1 FFX_NR_CALL outputWriter(FfxNrOwnedOutputHandleV1 h,uintptr_t list,uint64_t recording)noexcept{return safe([&]()->FfxNrStatusV1{auto p=lookup(h).output;if(!p)return FFX_NR_STALE_CONTEXT;if(failed(p->inspect().context))return FFX_NR_API_FAILURE_RETAINED;return p->beginWriterAction(reinterpret_cast<ID3D12GraphicsCommandList*>(list),recording)?FFX_NR_COMPLETE:FFX_NR_BUSY;});}
FfxNrStatusV1 FFX_NR_CALL outputConsumer(FfxNrOwnedOutputHandleV1 h,const FfxNrDispatchTicketV1*d)noexcept{return safe([&]()->FfxNrStatusV1{if(!d)return FFX_NR_INVALID_ARGUMENT;auto p=lookup(h).output;if(!p)return FFX_NR_STALE_CONTEXT;if(failed(p->inspect().context))return FFX_NR_API_FAILURE_RETAINED;return p->beginConsumerAction(*d)?FFX_NR_COMPLETE:FFX_NR_BUSY;});}
FfxNrStatusV1 FFX_NR_CALL outputEnd(FfxNrOwnedOutputHandleV1 h)noexcept{return safe([&]()->FfxNrStatusV1{auto p=lookup(h).output;if(!p)return FFX_NR_STALE_CONTEXT;p->endAction();return FFX_NR_COMPLETE;});}
FfxNrStatusV1 FFX_NR_CALL outputClose(FfxNrOwnedOutputHandleV1 h)noexcept{return safe([&]()->FfxNrStatusV1{auto p=lookup(h).output;if(!p)return FFX_NR_STALE_CONTEXT;p->closeAdmission();return FFX_NR_COMPLETE;});}
FfxNrStatusV1 FFX_NR_CALL outputRetirement(FfxNrOwnedOutputHandleV1 h,FfxNrOwnedOutputRetirementV1*out)noexcept{return safe([&]()->FfxNrStatusV1{if(!out)return FFX_NR_INVALID_ARGUMENT;*out={};auto p=lookup(h).output;if(!p)return FFX_NR_STALE_CONTEXT;auto s=p->inspectRetirement();*out={uint32_t(s.admissionClosed),uint32_t(s.actionsQuiescent),uint32_t(s.sdkComplete),uint32_t(s.failed||failed(p->inspect().context))};return FFX_NR_COMPLETE;});}
FfxNrStatusV1 FFX_NR_CALL outputSubmission(FfxNrOwnedOutputHandleV1 h,FfxNrSubmitResultV1*out)noexcept{return safe([&]()->FfxNrStatusV1{if(!out)return FFX_NR_INVALID_ARGUMENT;*out={};auto p=lookup(h).output;if(!p)return FFX_NR_STALE_CONTEXT;return p->inspectSubmission(*out)?FFX_NR_COMPLETE:FFX_NR_PENDING;});}
FfxNrStatusV1 FFX_NR_CALL algorithmInspect(FfxNrAlgorithmHandleV1 h,FfxNrAlgorithmSnapshotV1*out)noexcept{return safe([&]()->FfxNrStatusV1{if(!out)return FFX_NR_INVALID_ARGUMENT;*out={};auto p=lookup(h).algorithm;if(!p)return FFX_NR_STALE_CONTEXT;auto s=p->inspect();*out={s.swapchain,s.algorithmGeneration,s.allocationGeneration,uintptr_t(s.input),uint32_t(s.registrationCreated),uint32_t(s.registrationComplete),uint32_t(s.active),uint32_t(s.failed||failed(s.swapchain)),uint32_t(s.admissionClosed),uint32_t(s.destroyAttempted),uint32_t(s.released)};return FFX_NR_COMPLETE;});}
FfxNrStatusV1 FFX_NR_CALL algorithmOwns(FfxNrAlgorithmHandleV1 a,FfxNrOwnedOutputHandleV1 o,uint32_t*out)noexcept{return safe([&]()->FfxNrStatusV1{if(!out)return FFX_NR_INVALID_ARGUMENT;*out=0;auto ap=lookup(a).algorithm;auto op=lookup(o).output;if(!ap||!op)return FFX_NR_STALE_CONTEXT;*out=ap->Owns(op);return FFX_NR_COMPLETE;});}
FfxNrStatusV1 FFX_NR_CALL algorithmClose(FfxNrAlgorithmHandleV1 h)noexcept{return safe([&]()->FfxNrStatusV1{auto p=lookup(h).algorithm;if(!p)return FFX_NR_STALE_CONTEXT;p->closeAdmission();return FFX_NR_COMPLETE;});}
FfxNrStatusV1 FFX_NR_CALL algorithmDrain(FfxNrAlgorithmHandleV1 h,const FfxNrDrainHandleV1*d,uint64_t revision,FfxNrAlgorithmReleaseReceiptV1*out)noexcept{return safe([&]()->FfxNrStatusV1{if(!d||!out)return FFX_NR_INVALID_ARGUMENT;*out={};auto p=lookup(h).algorithm;if(!p)return FFX_NR_STALE_CONTEXT;if(failed(p->inspect().swapchain))return FFX_NR_API_FAILURE_RETAINED;AlgorithmReleaseReceipt r{};auto status=p->releaseAfterDrain(*d,revision,r);*out={r.algorithmGeneration,r.drain,r.evidenceRevision,r.cleanupOperations,r.actualResult,uint32_t(r.wholeContextDestroyed)};return status;});}
FfxNrStatusV1 FFX_NR_CALL finishWriter(const FfxNrContextTicketV1*c,FfxNrOwnedWriterRetirementReceiptV1*out)noexcept{return safe([&]()->FfxNrStatusV1{if(!c||!out)return FFX_NR_INVALID_ARGUMENT;*out={};if(failed(*c))return FFX_NR_API_FAILURE_RETAINED;auto t=native();if(!t.finish_writer)return FFX_NR_UNSUPPORTED;OwnedWriterRetirementReceipt r{};auto status=t.finish_writer(c,&r);*out={uintptr_t(r.list),uintptr_t(r.queue),uintptr_t(r.fence),r.target,r.oldRecording,r.newRecording,r.resetResult,r.closeResult,uint32_t(r.completed),uint32_t(r.oldRecordingRetired)};return status;});}
FfxNrStatusV1 FFX_NR_CALL getContext(void*p,FfxNrContextTicketV1*out)noexcept{return safe([&]()->FfxNrStatusV1{if(!out)return FFX_NR_INVALID_ARGUMENT;auto t=native();return t.get_context?t.get_context(p,out):FFX_NR_UNSUPPORTED;});}
FfxNrStatusV1 FFX_NR_CALL enroll(void*p,const FfxNrEnrollRequestV1*r,FfxNrContextTicketV1*out)noexcept{return safe([&]()->FfxNrStatusV1{if(!r||!out)return FFX_NR_INVALID_ARGUMENT;auto t=native();return t.enroll?t.enroll(p,r,out):FFX_NR_UNSUPPORTED;});}
FfxNrStatusV1 FFX_NR_CALL current(const FfxNrContextTicketV1*c,FfxNrDispatchTicketV1*out)noexcept{return safe([&]()->FfxNrStatusV1{if(!c||!out)return FFX_NR_INVALID_ARGUMENT;if(failed(*c))return FFX_NR_API_FAILURE_RETAINED;auto t=native();return t.current_dispatch?t.current_dispatch(c,out):FFX_NR_UNSUPPORTED;});}
FfxNrStatusV1 FFX_NR_CALL validate(const FfxNrDispatchTicketV1*d)noexcept{return safe([&]()->FfxNrStatusV1{if(!d)return FFX_NR_INVALID_ARGUMENT;if(failed(d->context))return FFX_NR_API_FAILURE_RETAINED;auto t=native();return t.validate_dispatch?t.validate_dispatch(d):FFX_NR_UNSUPPORTED;});}
FfxNrStatusV1 FFX_NR_CALL begin(const FfxNrDrainRequestV1*r,FfxNrDrainHandleV1*h,FfxNrDrainSummaryV1*s)noexcept{return safe([&]()->FfxNrStatusV1{if(!r||!h||!s)return FFX_NR_INVALID_ARGUMENT;auto t=native();if(!t.begin)return FFX_NR_UNSUPPORTED;auto result=t.begin(r,h,s);if(failed(r->context)){s->status=FFX_NR_API_FAILURE_RETAINED;return FFX_NR_API_FAILURE_RETAINED;}return result;});}
FfxNrStatusV1 FFX_NR_CALL poll(const FfxNrDrainHandleV1*h,int64_t deadline,FfxNrDrainSummaryV1*s)noexcept{return safe([&]()->FfxNrStatusV1{if(!h||!s)return FFX_NR_INVALID_ARGUMENT;auto t=native();if(!t.poll)return FFX_NR_UNSUPPORTED;auto result=t.poll(h,deadline,s);if(failed(h->context)){s->status=FFX_NR_API_FAILURE_RETAINED;return FFX_NR_API_FAILURE_RETAINED;}return result;});}
FfxNrStatusV1 FFX_NR_CALL read(const FfxNrDrainHandleV1*h,uint64_t rev,uint32_t first,FfxNrDomainEvidenceV1*o,uint32_t cap,uint32_t*w)noexcept{return safe([&]()->FfxNrStatusV1{if(!h||!w||(!o&&cap))return FFX_NR_INVALID_ARGUMENT;auto t=native();return t.read_domains?t.read_domains(h,rev,first,o,cap,w):FFX_NR_UNSUPPORTED;});}
FfxNrStatusV1 FFX_NR_CALL consume(const FfxNrDrainHandleV1*h,uint64_t rev)noexcept{return safe([&]()->FfxNrStatusV1{if(!h)return FFX_NR_INVALID_ARGUMENT;if(failed(h->context))return FFX_NR_API_FAILURE_RETAINED;auto t=native();return t.consume?t.consume(h,rev):FFX_NR_UNSUPPORTED;});}
}
static FfxNrStatusV1 FFX_NR_CALL queryContract(FfxNrControlledContractV1*out) noexcept {
 if(!out||out->size!=sizeof(*out)||out->version!=1)return FFX_NR_INVALID_ARGUMENT;
 if(sizeof(void*)!=8)return FFX_NR_UNSUPPORTED;
 *out={sizeof(*out),1,FFX_NR_CONTROLLED_CONTRACT_ID_V1,1,0x8664,8,7};return FFX_NR_COMPLETE;
}
extern "C" FfxNrStatusV1 FFX_NR_CALL ffxNeuRoticQueryControlledServiceV1(FfxNrControlledServiceV1*t){return safe([&]()->FfxNrStatusV1{
 if(!t||t->size!=sizeof(*t)||t->version!=1||t->manual_opt_in!=FFX_NR_CONTROLLED_OPT_IN_V1)return FFX_NR_UNSUPPORTED;
 (void)registry(); FfxNrControlledServiceV1 out{};out.size=sizeof(out);out.version=1;out.manual_opt_in=FFX_NR_CONTROLLED_OPT_IN_V1;
 out.get_context=getContext;out.select_algorithm=selectAlgorithm;out.set_submit_observer=setObserver;out.enroll=enroll;out.write_owned_output=writeOutput;out.finish_writer=finishWriter;out.current_dispatch=current;out.validate_dispatch=validate;out.begin=begin;out.poll=poll;out.read_domains=read;out.consume=consume;
 out.output_retain=retain<FfxNrOwnedOutputHandleV1>;out.output_release=release<FfxNrOwnedOutputHandleV1>;out.output_inspect=outputInspect;out.output_begin_writer_action=outputWriter;out.output_begin_consumer_action=outputConsumer;out.output_end_action=outputEnd;out.output_close_admission=outputClose;out.output_inspect_retirement=outputRetirement;out.output_inspect_submission=outputSubmission;
 out.algorithm_retain=retain<FfxNrAlgorithmHandleV1>;out.algorithm_release=release<FfxNrAlgorithmHandleV1>;out.algorithm_inspect=algorithmInspect;out.algorithm_owns=algorithmOwns;out.algorithm_close_admission=algorithmClose;out.algorithm_release_after_drain=algorithmDrain;out.query_contract=queryContract;*t=out;return FFX_NR_COMPLETE;});}




