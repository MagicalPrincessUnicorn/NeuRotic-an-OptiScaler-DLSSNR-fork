#include "OptiScaler/nr/lifecycle/Fsr3ControlledModule.h"
#include <cstdio>
using Neurotic::Lifecycle::Fsr3ControlledModule;
static int checks{},failures{};
#define CHECK(x) do{++checks;if(!(x)){++failures;printf("FAIL %d %s\n",__LINE__,#x);}}while(0)
template<class T>struct Stub;
template<class R,class... A>struct Stub<R(FFX_NR_CALL*)(A...)>{static R FFX_NR_CALL Call(A...){return FFX_NR_UNSUPPORTED;}};
static FfxNrControlledServiceV1 table{};
static FfxNrOwnedOutputSnapshotV1 output{};
static FfxNrAlgorithmSnapshotV1 algorithm{};
static FfxNrContextTicketV1 context{1,2,3};
static bool stale{},foreign{},badContract{},pending{true},actionFailure{},badRelease{};
static int outputRefs{},algorithmRefs{};
extern "C" FfxNrStatusV1 FFX_NR_CALL ffxNeuRoticQueryControlledServiceV1(FfxNrControlledServiceV1* t){*t=table;return FFX_NR_COMPLETE;}
#pragma comment(linker,"/EXPORT:ffxNeuRoticQueryControlledServiceV1")
static void Init(){
 table={};table.size=sizeof(table);table.version=1;table.manual_opt_in=FFX_NR_CONTROLLED_OPT_IN_V1;
#define STUB(n) table.n=&Stub<decltype(table.n)>::Call;
 STUB(get_context) STUB(select_algorithm) STUB(set_submit_observer) STUB(enroll)
 STUB(write_owned_output) STUB(finish_writer) STUB(current_dispatch) STUB(validate_dispatch)
 STUB(begin) STUB(poll) STUB(read_domains) STUB(consume)
 STUB(output_retain) STUB(output_release) STUB(output_inspect) STUB(output_begin_writer_action)
 STUB(output_begin_consumer_action) STUB(output_end_action) STUB(output_close_admission)
 STUB(output_inspect_retirement) STUB(output_inspect_submission)
 STUB(algorithm_retain) STUB(algorithm_release) STUB(algorithm_inspect) STUB(algorithm_owns)
 STUB(algorithm_close_admission) STUB(algorithm_release_after_drain) STUB(query_contract)
#undef STUB
 table.query_contract=[](FfxNrControlledContractV1* c)->FfxNrStatusV1{*c={sizeof(*c),1,FFX_NR_CONTROLLED_CONTRACT_ID_V1,badContract?2u:1u,0x8664,8,7};return FFX_NR_COMPLETE;};
 table.get_context=[](void* p,FfxNrContextTicketV1* c)->FfxNrStatusV1{if(p!=(void*)0x10)return FFX_NR_STALE_CONTEXT;*c=context;return FFX_NR_COMPLETE;};
 table.output_retain=[](FfxNrOwnedOutputHandleV1 h)->FfxNrStatusV1{if(stale||h.id!=11||h.cookie!=12)return FFX_NR_STALE_CONTEXT;++outputRefs;return FFX_NR_COMPLETE;};
 table.output_release=[](FfxNrOwnedOutputHandleV1)->FfxNrStatusV1{--outputRefs;return FFX_NR_COMPLETE;};
 table.algorithm_retain=[](FfxNrAlgorithmHandleV1 h)->FfxNrStatusV1{if(stale||h.id!=21||h.cookie!=22)return FFX_NR_STALE_CONTEXT;++algorithmRefs;return FFX_NR_COMPLETE;};
 table.algorithm_release=[](FfxNrAlgorithmHandleV1)->FfxNrStatusV1{--algorithmRefs;return FFX_NR_COMPLETE;};
 table.output_inspect=[](FfxNrOwnedOutputHandleV1,FfxNrOwnedOutputSnapshotV1* o)->FfxNrStatusV1{if(stale)return FFX_NR_STALE_CONTEXT;*o=output;return FFX_NR_COMPLETE;};
 table.algorithm_inspect=[](FfxNrAlgorithmHandleV1,FfxNrAlgorithmSnapshotV1* a)->FfxNrStatusV1{if(stale)return FFX_NR_STALE_CONTEXT;*a=algorithm;return FFX_NR_COMPLETE;};
 table.algorithm_owns=[](FfxNrAlgorithmHandleV1,FfxNrOwnedOutputHandleV1,uint32_t* o)->FfxNrStatusV1{*o=foreign?0:1;return FFX_NR_COMPLETE;};
 table.output_begin_writer_action=[](FfxNrOwnedOutputHandleV1,uintptr_t,uint64_t)->FfxNrStatusV1{return actionFailure?FFX_NR_API_FAILURE_RETAINED:FFX_NR_COMPLETE;};
 table.output_inspect_retirement=[](FfxNrOwnedOutputHandleV1,FfxNrOwnedOutputRetirementV1* r)->FfxNrStatusV1{*r={1,1,1,0};return FFX_NR_COMPLETE;};
 table.output_inspect_submission=[](FfxNrOwnedOutputHandleV1,FfxNrSubmitResultV1* s)->FfxNrStatusV1{if(pending)return FFX_NR_PENDING;*s={};s->size=sizeof(*s);s->version=1;return FFX_NR_COMPLETE;};
 table.algorithm_release_after_drain=[](FfxNrAlgorithmHandleV1,const FfxNrDrainHandleV1* d,uint64_t rev,FfxNrAlgorithmReleaseReceiptV1* r)->FfxNrStatusV1{
  *r={10,*d,badRelease?rev+1:rev,64,0,1};return FFX_NR_COMPLETE;};
 output={};output.context=context;output.resource=0x100;output.queue=0x200;output.allocation_generation=9;
 output.description.dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;output.description.width=1920;output.description.height=1080;
 output.description.depth_or_array_size=1;output.description.mip_levels=1;output.description.sample_count=1;
 output.description.format=DXGI_FORMAT_R8G8B8A8_UNORM;output.ordinary_escape_excluded=output.recycling_excluded=1;
 algorithm={};algorithm.swapchain=context;algorithm.input=output.resource;algorithm.allocation_generation=9;algorithm.algorithm_generation=10;
 algorithm.registration_created=algorithm.registration_complete=1;
}
int main(){
 Init();const auto module=GetModuleHandleW(nullptr);
 CHECK(!Fsr3ControlledModule::Authenticate(module,&table,false));
 CHECK(!Fsr3ControlledModule::Authenticate(GetModuleHandleW(L"kernel32.dll"),&table,true));
 auto changed=table;changed.version=2;CHECK(!Fsr3ControlledModule::Authenticate(module,&changed,true));
 changed=table;changed.output_inspect=nullptr;CHECK(!Fsr3ControlledModule::Authenticate(module,&changed,true));
 auto saved=table.output_inspect;table.output_inspect=reinterpret_cast<decltype(saved)>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"GetTickCount"));
 CHECK(!Fsr3ControlledModule::Authenticate(module,&table,true));table.output_inspect=saved;
 badContract=true;CHECK(!Fsr3ControlledModule::Authenticate(module,&table,true));badContract=false;
 auto transport=Fsr3ControlledModule::Authenticate(module,&table,true);CHECK(transport);
 CHECK(!transport->Bind((void*)0x11,context,{11,12},{21,22}));
 CHECK(!transport->Bind((void*)0x10,{1,2,4},{11,12},{21,22}));
 CHECK(!transport->Bind((void*)0x10,context,{11,99},{21,22}));CHECK(outputRefs==0);
 CHECK(!transport->Bind((void*)0x10,context,{11,12},{21,99}));CHECK(outputRefs==0&&algorithmRefs==0);
 foreign=true;CHECK(!transport->Bind((void*)0x10,context,{11,12},{21,22}));foreign=false;
 CHECK(outputRefs==0&&algorithmRefs==0);
 ++algorithm.allocation_generation;CHECK(!transport->Bind((void*)0x10,context,{11,12},{21,22}));--algorithm.allocation_generation;
 {
  auto bound=transport->Bind((void*)0x10,context,{11,12},{21,22});CHECK(bound);CHECK(outputRefs==1&&algorithmRefs==1);
  CHECK(bound.algorithm->Owns(bound.output));CHECK(!bound.algorithm->Owns({}));
  CHECK(!bound.output->inspect().failed);CHECK(bound.output->inspect().description.Width==1920);
  FfxNrSubmitResultV1 submit{};CHECK(!bound.output->inspectSubmission(submit));CHECK(!bound.output->inspect().failed);
  pending=false;CHECK(bound.output->inspectSubmission(submit));
  CHECK(bound.output->beginWriterAction(nullptr,1));actionFailure=true;CHECK(!bound.output->beginWriterAction(nullptr,1));
  CHECK(bound.output->inspect().failed);CHECK(bound.output->inspectRetirement().failed);actionFailure=false;
 }
 CHECK(outputRefs==0&&algorithmRefs==0);
 {
  auto bound=transport->Bind((void*)0x10,context,{11,12},{21,22});CHECK(bound);
  ++output.description.width;CHECK(bound.output->inspect().failed);--output.description.width;CHECK(bound.output->inspect().failed);
 }
 {
  auto bound=transport->Bind((void*)0x10,context,{11,12},{21,22});CHECK(bound);stale=true;CHECK(bound.algorithm->inspect().failed);stale=false;
  CHECK(bound.algorithm->inspect().failed);
 }
 {
  auto bound=transport->Bind((void*)0x10,context,{11,12},{21,22});CHECK(bound);
  ffx::nr::AlgorithmReleaseReceipt release{};
  CHECK(bound.algorithm->releaseAfterDrain({context,1,2},3,release)==FFX_NR_COMPLETE);CHECK(release.cleanupOperations==64&&release.wholeContextDestroyed);
  CHECK(bound.algorithm->releaseAfterDrain({{1,2,4},1,2},3,release)==FFX_NR_INVALID_ARGUMENT);
  badRelease=true;CHECK(bound.algorithm->releaseAfterDrain({context,1,2},3,release)==FFX_NR_API_FAILURE_RETAINED);
  CHECK(bound.algorithm->inspect().failed);badRelease=false;
 }
 CHECK(outputRefs==0&&algorithmRefs==0);
 printf("controlled_module checks=%d failures=%d\n",checks,failures);return failures?1:0;
}
