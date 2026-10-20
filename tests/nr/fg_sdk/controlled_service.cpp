#include "external/FidelityFX-CheckedClosure/CheckedControlledService.h"
#include <windows.h>
#if defined(CONTROLLED_TEST_DLL)
#include "external/FidelityFX-CheckedClosure/CheckedExperimentalService.h"
using namespace ffx::nr;
#endif
#include <type_traits>
#include <cstdio>
static_assert(std::is_standard_layout_v<FfxNrControlledServiceV1> && std::is_trivial_v<FfxNrControlledServiceV1>);
static_assert(std::is_trivially_copyable_v<FfxNrOwnedOutputSnapshotV1>);
static int checks=0, failures=0;
#define CHECK(x) do { ++checks; if(!(x)){++failures;printf("FAIL %d: %s\n",__LINE__,#x);} } while(0)
#if defined(CONTROLLED_TEST_DLL)
struct Output:OwnedOutputLease {
 FfxNrContextTicketV1 context{};bool action=false; OwnedOutputSnapshot inspect()const noexcept override{ OwnedOutputSnapshot s{};s.context=context;s.allocationGeneration=17;s.resource=(ID3D12Resource*)0x1234;s.description.Width=1920;return s; }
 bool beginWriterAction(ID3D12GraphicsCommandList*,uint64_t)noexcept override{if(action)return false;return action=true;}
 bool beginConsumerAction(const FfxNrDispatchTicketV1&)noexcept override{return beginWriterAction(nullptr,0);}
 void endAction()noexcept override{action=false;} void closeAdmission()noexcept override{}
 OwnedOutputRetirement inspectRetirement()const noexcept override{return {true,!action,true,false};}
 bool inspectSubmission(FfxNrSubmitResultV1& out)const noexcept override{out={};out.size=sizeof(out);out.version=1;return true;}
};
struct Algorithm:AlgorithmLease {
 std::shared_ptr<OwnedOutputLease> output; bool released=false; int releaseCalls=0;
 AlgorithmSnapshot inspect()const noexcept override{AlgorithmSnapshot s{};s.algorithmGeneration=19;s.swapchain=output->inspect().context;s.released=released;return s;}
 bool Owns(const std::shared_ptr<OwnedOutputLease>& p)const noexcept override{return p==output;}
 void closeAdmission()noexcept override{}
 FfxNrStatusV1 releaseAfterDrain(const FfxNrDrainHandleV1& d,uint64_t r,AlgorithmReleaseReceipt& o)noexcept override{++releaseCalls;o={19,d,r,8,0,true};released=true;return FFX_NR_COMPLETE;}
};
static auto output=std::make_shared<Output>();static auto algorithm=std::make_shared<Algorithm>();
static AlgorithmConsumerObserver observer{};static std::weak_ptr<Output> oldOutput;
static FfxNrStatusV1 select(void* identity,void*,const AlgorithmConsumerObserver* o,std::shared_ptr<AlgorithmLease>* out){oldOutput=output;output=std::make_shared<Output>();output->context.owner_id=uintptr_t(identity);algorithm=std::make_shared<Algorithm>();observer=*o;algorithm->output=output;*out=algorithm;return FFX_NR_COMPLETE;}
static FfxNrStatusV1 write(const FfxNrContextTicketV1*,const OwnedWriterCallbacks* c,void* u,OwnedWriteReceipt* r){r->prepareResult=c->prepare(u,output,nullptr);if(FAILED(r->prepareResult))return FFX_NR_API_FAILURE_RETAINED;r->callbackResult=c->write(u,output,nullptr);if(FAILED(r->callbackResult))return FFX_NR_API_FAILURE_RETAINED;void* g=nullptr;auto hr=c->beginSubmit(u,output,&g);if(FAILED(hr))return FFX_NR_API_FAILURE_RETAINED;c->endSubmit(u,g,*r);return FFX_NR_COMPLETE;}
extern "C" FfxNrStatusV1 FFX_NR_CALL ffxNeuRoticQueryExperimentalServiceV1(ExperimentalServiceV1* t){t->select_algorithm=select;t->write_owned_output=write;return FFX_NR_COMPLETE;}
extern "C" __declspec(dllexport) FfxNrStatusV1 controlledTestConsumer() {FfxNrDispatchTicketV1 d{};void* g=nullptr;auto s=observer.begin_consumer(observer.user,output,algorithm,&d,&g);observer.end_consumer(observer.user,g,0);return s;}
extern "C" __declspec(dllexport) int controlledTestReleaseCalls(){return algorithm->releaseCalls;}
extern "C" __declspec(dllexport) int controlledTestRetainedFailedOutput(){return !oldOutput.expired();}
#else
static decltype(&ffxNeuRoticQueryControlledServiceV1) query=nullptr;
static FfxNrStatusV1 (*testConsumer)()=nullptr;
static int (*testReleaseCalls)()=nullptr;static int (*testRetained)()=nullptr;
#define ffxNeuRoticQueryControlledServiceV1 query
static FfxNrControlledServiceV1 api{};static FfxNrOwnedOutputHandleV1 held{};static bool failWrite=false;
static int32_t FFX_NR_CALL prepare(void*,FfxNrOwnedOutputHandleV1 h,uintptr_t){held=h;CHECK(api.output_retain(h)==0);return 0;}
static int32_t FFX_NR_CALL writer(void*,FfxNrOwnedOutputHandleV1,uintptr_t){if(failWrite)throw 1;return 0;}
static int32_t FFX_NR_CALL beginSubmit(void*,FfxNrOwnedOutputHandleV1,void**){return 0;}
static void FFX_NR_CALL endSubmit(void*,void*,const FfxNrOwnedWriteReceiptV1*){}
static FfxNrStatusV1 FFX_NR_CALL beginConsumer(void*,FfxNrOwnedOutputHandleV1 o,FfxNrAlgorithmHandleV1 a,const FfxNrDispatchTicketV1*,void**){uint32_t owns=0;CHECK(api.algorithm_owns(a,o,&owns)==0&&owns);throw 1;}
static void FFX_NR_CALL endConsumer(void*,void*,int32_t){throw 1;}
int main(int argc,char**argv){
 if(argc!=2)return 2;auto dll=LoadLibraryA(argv[1]);if(!dll)return 3;
 query=reinterpret_cast<decltype(query)>(GetProcAddress(dll,"ffxNeuRoticQueryControlledServiceV1"));
 testConsumer=reinterpret_cast<decltype(testConsumer)>(GetProcAddress(dll,"controlledTestConsumer"));
 testReleaseCalls=reinterpret_cast<decltype(testReleaseCalls)>(GetProcAddress(dll,"controlledTestReleaseCalls"));
 testRetained=reinterpret_cast<decltype(testRetained)>(GetProcAddress(dll,"controlledTestRetainedFailedOutput"));
 if(!query||!testConsumer||!testReleaseCalls||!testRetained)return 4;
 CHECK(ffxNeuRoticQueryControlledServiceV1(nullptr)!=0);api.size=sizeof(api);api.version=1;
 CHECK(ffxNeuRoticQueryControlledServiceV1(&api)!=0);api.manual_opt_in=FFX_NR_CONTROLLED_OPT_IN_V1;
 CHECK(ffxNeuRoticQueryControlledServiceV1(&api)==0); FfxNrControlledContractV1 contract{sizeof(contract),1};CHECK(api.query_contract(&contract)==0&&contract.contract_id==FFX_NR_CONTROLLED_CONTRACT_ID_V1&&contract.route_flags==7);contract.version=2;CHECK(api.query_contract(&contract)==FFX_NR_INVALID_ARGUMENT);
 FfxNrControlledConsumerObserverV1 obs{sizeof(obs),1,nullptr,beginConsumer,endConsumer};FfxNrAlgorithmHandleV1 a{};
 CHECK(api.select_algorithm(nullptr,nullptr,&obs,&a)==0);
 FfxNrControlledWriterCallbacksV1 cb{sizeof(cb),1,prepare,writer,beginSubmit,endSubmit};FfxNrOwnedWriteReceiptV1 receipt{};FfxNrContextTicketV1 context{};
 CHECK(api.write_owned_output(&context,&cb,nullptr,&receipt)==0);
 FfxNrOwnedOutputSnapshotV1 snapshot{};CHECK(api.output_inspect(held,&snapshot)==0&&snapshot.allocation_generation==17&&snapshot.description.width==1920);
 uint32_t owns=0;CHECK(api.algorithm_owns(a,held,&owns)==0&&owns);
 CHECK(api.output_inspect({a.id,a.cookie},&snapshot)==FFX_NR_STALE_CONTEXT);
 auto foreign=held;foreign.cookie^=1;CHECK(api.output_retain(foreign)==FFX_NR_STALE_CONTEXT);
 CHECK(api.output_begin_writer_action(held,0,1)==0);CHECK(api.output_begin_writer_action(held,0,1)!=0);CHECK(api.output_end_action(held)==0);
 auto stale=held;CHECK(api.output_release(held)==0);CHECK(api.output_retain(stale)==FFX_NR_STALE_CONTEXT);CHECK(api.output_release(stale)==FFX_NR_STALE_CONTEXT);
 FfxNrDrainHandleV1 drain{};FfxNrAlgorithmReleaseReceiptV1 ar{};CHECK(api.algorithm_release_after_drain(a,&drain,4,&ar)==0&&ar.cleanup_operations==8&&ar.whole_context_destroyed);
 failWrite=true;CHECK(api.write_owned_output(&context,&cb,nullptr,&receipt)==FFX_NR_API_FAILURE_RETAINED);CHECK(receipt.callback_result<0);CHECK(api.output_release(held)==0);

 FfxNrAlgorithmSnapshotV1 as{};CHECK(api.algorithm_inspect(a,&as)==0&&as.algorithm_generation==19);
 CHECK(api.algorithm_retain(a)==0);CHECK(api.algorithm_release(a)==0);CHECK(api.algorithm_inspect(a,&as)==0);
 CHECK(api.algorithm_release_after_drain(a,&drain,4,&ar)==FFX_NR_API_FAILURE_RETAINED);CHECK(testReleaseCalls()==1);
 CHECK(api.algorithm_release(a)==0);CHECK(api.algorithm_retain(a)==FFX_NR_STALE_CONTEXT);
 CHECK(api.select_algorithm(reinterpret_cast<void*>(2),nullptr,&obs,&a)==0);CHECK(testRetained()==1);CHECK(testConsumer()==FFX_NR_API_FAILURE_RETAINED);CHECK(testConsumer()==FFX_NR_API_FAILURE_RETAINED);CHECK(api.algorithm_release_after_drain(a,&drain,4,&ar)==FFX_NR_API_FAILURE_RETAINED);CHECK(testReleaseCalls()==0);CHECK(api.algorithm_release(a)==0);
 printf("controlled facade: %d checks, %d failures\n",checks,failures);return failures?1:0;
}


#endif



