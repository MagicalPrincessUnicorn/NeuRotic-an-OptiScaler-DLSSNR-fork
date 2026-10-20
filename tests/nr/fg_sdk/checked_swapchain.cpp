// Actual v1 SDK swapchain/worker mechanics on WARP. No FG algorithm/provider is loaded.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <limits>
#include <cmath>
#include <cstdio>
#include <atomic>
#include <thread>
#include "external/FidelityFX-SDK/sdk/src/backends/dx12/FrameInterpolationSwapchain/FrameInterpolationSwapchainDX12.h"
#include "external/FidelityFX-CheckedClosure/CheckedSdkService.h"
#include "external/FidelityFX-CheckedClosure/CheckedOwnedOutput.h"
#include "external/FidelityFX-CheckedClosure/CheckedAlgorithmState.h"
struct TestSwapchain : FrameInterpolationSwapChainDX12 { using FrameInterpolationSwapChainDX12::real; };
static unsigned checks,failures;
static std::atomic<unsigned> callbacks{0},submissions{0};
static int testMode=0;
static std::shared_ptr<ffx::nr::OwnedOutputLease> outputLease;
static ID3D12DescriptorHeap* outputHeap=nullptr;
static std::shared_ptr<ffx::nr::AlgorithmState> algorithmLease;
static unsigned algorithmCleanupCalls=0,algorithmConsumerCalls=0;
#define CHECK(x) do { ++checks; if(!(x)) { ++failures; std::printf("FAIL line %d: %s\n",__LINE__,#x); } } while(0)
static FfxNrContextTicketV1 context;
static void endDuringAcquisition(ffx::nr::OwnedOutputLease* lease) {
    std::thread ending([&]{lease->endAction();});ending.join();
    CHECK(!lease->inspectRetirement().actionsQuiescent);
}
static FfxNrStatusV1 beginAlgorithm(void*,const std::shared_ptr<ffx::nr::OwnedOutputLease>& output,const std::shared_ptr<ffx::nr::AlgorithmLease>& algorithm,const FfxNrDispatchTicketV1* ticket,void** guard) {
    CHECK(algorithm->Owns(output));CHECK(algorithm->inspect().registrationComplete);CHECK(algorithm->inspect().input==output->inspect().resource);
    if(!output->beginConsumerAction(*ticket))return FFX_NR_BUSY;*guard=output.get();++algorithmConsumerCalls;return FFX_NR_COMPLETE;
}
static void endAlgorithm(void*,void* guard,int32_t result){CHECK(result==0);static_cast<ffx::nr::OwnedOutputLease*>(guard)->endAction();}
static FfxErrorCode dispatch(const FfxFrameGenerationDispatchDescription* description,void*) {
    FfxNrDispatchTicketV1 ticket{}; ticket.size=sizeof(ticket); ticket.version=1;
    CHECK(ffx::nr::serviceCurrent(&context,&ticket)==FFX_NR_COMPLETE);
    CHECK(ticket.command_list==reinterpret_cast<uintptr_t>(description->commandList));
    CHECK(ticket.present_color==reinterpret_cast<uintptr_t>(description->presentColor.resource));
    CHECK(ffx::nr::serviceValidate(&ticket)==FFX_NR_COMPLETE);
    if(testMode==5) { CHECK(outputLease->inspect().resource==description->presentColor.resource); ffx::nr::testActionAcquiring=endDuringAcquisition; CHECK(outputLease->beginConsumerAction(ticket)); ffx::nr::testActionAcquiring=nullptr; CHECK(outputLease->inspect().actionActive); outputLease->endAction(); CHECK(outputLease->inspectRetirement().actionsQuiescent); }
    if(testMode==5) {
        auto admission=algorithmLease->enter();CHECK(bool(admission));
        CHECK(algorithmLease->dispatch(description->commandList,description->frameID,static_cast<ID3D12Resource*>(description->presentColor.resource),static_cast<ID3D12Resource*>(description->outputs[0].resource)));
        ffx::nr::AlgorithmState::ConsumerScope consumer(algorithmLease.get());CHECK(bool(consumer));consumer.succeeded();
    }
    int otherOwner=0,otherAlias=0; auto other=ffx::nr::createdRegistry().create(&otherOwner,&otherAlias,1);
    { ffx::nr::WorkScope nested(&otherOwner); CHECK(!nested); CHECK(ffx::nr::activeContext->identity.owner_id==context.owner_id); }
    CHECK(ffx::nr::createdRegistry().remove(&otherOwner));
    ++ticket.dispatch_id; CHECK(ffx::nr::serviceValidate(&ticket)==FFX_NR_STALE_CONTEXT);
    // Empty recording tests SDK submission mechanics; it does not claim algorithm output.
    if(testMode==0 || testMode>=3) const_cast<FfxFrameGenerationDispatchDescription*>(description)->numInterpolatedFrames=0;
    if(testMode==2) CHECK(SUCCEEDED(static_cast<ID3D12GraphicsCommandList*>(description->commandList)->Close()));
    ++callbacks; return testMode==1 ? FFX_ERROR_INVALID_ARGUMENT : FFX_OK;
}
static FfxNrStatusV1 __cdecl admit(void*,const FfxNrDispatchTicketV1* ticket,void** guard) {
    *guard=nullptr; return ffx::nr::serviceValidate(ticket);
}
static void __cdecl submitted(void*,void*,const FfxNrSubmitResultV1* receipt) {
    CHECK((receipt->callback_result==FFX_OK)==(testMode!=1));
    CHECK(receipt->execute_invoked==((testMode==0 || testMode>=3)?1u:0u));
    CHECK(SUCCEEDED(receipt->close_result)==(testMode!=2));
    CHECK(SUCCEEDED(receipt->signal_result)==(testMode!=2));
    CHECK(receipt->output_published==0); ++submissions;
}
struct LifecycleFixture { std::atomic<int> pins{0}; std::atomic<bool> entered{false},finish{false}; };
static void lifecycleAndWorkerSchedules() {
    LifecycleFixture fixture; int alias=0;
    auto state=ffx::nr::createdRegistry().create(&fixture,&alias,1);
    state->retainPin=[](void* p) { ++static_cast<LifecycleFixture*>(p)->pins; };
    state->releasePin=[](void* p) { --static_cast<LifecycleFixture*>(p)->pins; };
    state->enrollOwner=[](void* p)->FfxNrStatusV1 { auto& f=*static_cast<LifecycleFixture*>(p); f.entered=true; while(!f.finish.load()) std::this_thread::yield(); return f.pins==1 ? FFX_NR_COMPLETE:FFX_NR_API_FAILURE_RETAINED; };
    FfxNrEnrollRequestV1 req{sizeof(req),1,1,0,0}; FfxNrContextTicketV1 id{}; FfxNrStatusV1 result=FFX_NR_PENDING;
    std::thread enrollment([&]{ result=ffx::nr::serviceEnroll(&alias,&req,&id); });
    while(!fixture.entered.load()) std::this_thread::yield();
    { ffx::nr::DestroyScope destroy(state); CHECK(!destroy); CHECK(fixture.pins==1); }
    fixture.finish=true; enrollment.join(); CHECK(result==FFX_NR_COMPLETE); CHECK(fixture.pins==1);
    { ffx::nr::DestroyScope destroy(state); CHECK(!destroy); }
    {
      ffx::nr::WorkerScope worker(&alias);
      ffx::nr::createdRegistry().unbindAlias(&alias); // worker must no longer depend on registry lookup
      state->workerShutdownRequested=true;
      CHECK(ffx::nr::workerStopping(&alias,false));
      state->workerShutdownRequested=false;
      HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr); const auto before=GetTickCount64();
      CHECK(ffx::nr::waitWorkerEvent(&alias,event)==WAIT_TIMEOUT); CHECK(GetTickCount64()-before<500);
      CHECK(!state->closure.failed()); CloseHandle(event);
      { ffx::nr::WorkScope missingJob(&alias,0); CHECK(!missingJob); CHECK(ffx::nr::activeContext==nullptr); }
      auto root=state->closure.enterRoot(); CHECK(state->publish(0,root.child()));
      { ffx::nr::WorkScope job(&alias,0); CHECK(bool(job)); CHECK(ffx::nr::activeContext==state.get()); }
    }
    state->destroyReady=true;
    { ffx::nr::DestroyScope destroy(state); CHECK(bool(destroy)); }
    CHECK(ffx::nr::serviceEnroll(&fixture,&req,&id)==FFX_NR_STALE_CONTEXT);
    state->releasePin(&fixture); CHECK(fixture.pins==0); state->enrolled=false; CHECK(ffx::nr::createdRegistry().remove(&fixture));
}
static HRESULT prepareOutput(void*,const std::shared_ptr<ffx::nr::OwnedOutputLease>& lease,ID3D12GraphicsCommandList* list) {
    outputLease=lease; const auto view=lease->inspect(); CHECK(view.resource!=nullptr); CHECK(view.ordinaryEscapeExcluded && view.recyclingExcluded);
    CHECK((view.description.Flags&D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)!=0); CHECK(view.description.Format==DXGI_FORMAT_R16G16B16A16_FLOAT); CHECK(list!=nullptr);
    ID3D12Device* device=nullptr; if(FAILED(view.resource->GetDevice(IID_PPV_ARGS(&device)))) return E_FAIL;
    D3D12_DESCRIPTOR_HEAP_DESC desc{};desc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;desc.NumDescriptors=1;
    const auto result=device->CreateDescriptorHeap(&desc,IID_PPV_ARGS(&outputHeap));
    if(SUCCEEDED(result)) device->CreateRenderTargetView(view.resource,nullptr,outputHeap->GetCPUDescriptorHandleForHeapStart());
    device->Release();return result;
}
static HRESULT writeOutput(void*,const std::shared_ptr<ffx::nr::OwnedOutputLease>& lease,ID3D12GraphicsCommandList* list) {
    auto view=lease->inspect();CHECK(view.writerList==list && view.writerRecording!=0);CHECK(!view.writerSubmitted);CHECK(view.writerActive);ffx::nr::testActionAcquiring=endDuringAcquisition;CHECK(lease->beginWriterAction(list,view.writerRecording));ffx::nr::testActionAcquiring=nullptr;CHECK(lease->inspect().actionActive);
    if(testMode==5){auto admission=algorithmLease->enter();CHECK(bool(admission));CHECK(algorithmLease->Owns(lease));CHECK(algorithmLease->prepare(list,0,view.resource,view.resource));CHECK(!algorithmLease->prepare(list,0,view.resource,view.resource));}
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition.pResource=view.resource;barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_PRESENT;barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_RENDER_TARGET;
    list->ResourceBarrier(1,&barrier);float color[4]={0.1f,0.2f,0.3f,1.0f};list->ClearRenderTargetView(outputHeap->GetCPUDescriptorHandleForHeapStart(),color,0,nullptr);
    std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);list->ResourceBarrier(1,&barrier);lease->endAction();CHECK(lease->inspectRetirement().actionsQuiescent);return testMode==6?E_FAIL:S_OK;
}
static HRESULT beforeWriteSubmit(void*,const std::shared_ptr<ffx::nr::OwnedOutputLease>&,void** guard) { *guard=nullptr;return S_OK; }
static void afterWriteSubmit(void*,void*,const ffx::nr::OwnedWriteReceipt& receipt) {CHECK(receipt.executeInvoked==(testMode!=6));CHECK(SUCCEEDED(receipt.signalResult));CHECK(receipt.fence!=nullptr && receipt.target!=0);}
int main(int argc,char** argv) {
    lifecycleAndWorkerSchedules();
    if(argc>1) testMode=std::atoi(argv[1]);
    WNDCLASSW wc{}; wc.lpfnWndProc=DefWindowProcW; wc.hInstance=GetModuleHandleW(nullptr); wc.lpszClassName=L"NrCheckedSdkWarp";
    CHECK(RegisterClassW(&wc)!=0); HWND window=CreateWindowW(wc.lpszClassName,L"SDK WARP mechanics",WS_OVERLAPPEDWINDOW,0,0,128,128,nullptr,nullptr,wc.hInstance,nullptr); CHECK(window!=nullptr);
    IDXGIFactory4* factory=nullptr; IDXGIAdapter* warp=nullptr; ID3D12Device* device=nullptr; ID3D12CommandQueue* queue=nullptr;
    CHECK(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))); CHECK(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)))); CHECK(SUCCEEDED(D3D12CreateDevice(warp,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device))));
    D3D12_COMMAND_QUEUE_DESC queueDesc{}; CHECK(SUCCEEDED(device->CreateCommandQueue(&queueDesc,IID_PPV_ARGS(&queue))));
    auto swapchain=new TestSwapchain;
    DXGI_SWAP_CHAIN_DESC1 desc{}; desc.Width=64; desc.Height=64; desc.Format=testMode>=5?DXGI_FORMAT_R16G16B16A16_FLOAT:DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count=1; desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.BufferCount=2; desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
    CHECK(SUCCEEDED(swapchain->init(window,&desc,nullptr,queue,factory)));
    ID3D12Resource* buffer=nullptr; if(testMode<5) { CHECK(SUCCEEDED(swapchain->GetBuffer(0,IID_PPV_ARGS(&buffer)))); SafeRelease(buffer); }
    FfxFrameGenerationConfig config{}; config.frameGenerationEnabled=true; config.frameGenerationCallback=dispatch; config.interpolationRect.width=64; config.interpolationRect.height=64;
    swapchain->setFrameGenerationConfig(&config);
    CHECK(ffx::nr::serviceGetContext(swapchain,&context)==FFX_NR_COMPLETE);
    FfxNrSubmissionObserverV1 observer{sizeof(observer),1,nullptr,admit,submitted}; CHECK(ffx::nr::serviceObserver(&context,&observer)==FFX_NR_COMPLETE);
    FfxNrEnrollRequestV1 request{sizeof(request),1,0xF65CDD1201001003ULL,testMode>=5?ffx::nr::ControlledOutputEnrollment:0,0};
    if(testMode<5) { auto forbidden=request;forbidden.flags=ffx::nr::ControlledOutputEnrollment;CHECK(ffx::nr::serviceEnroll(swapchain,&forbidden,&context)==FFX_NR_UNSUPPORTED); }
    FfxNrStatusV1 status=FFX_NR_UNSUPPORTED; const auto start=GetTickCount64();
    do { status=ffx::nr::serviceEnroll(swapchain,&request,&context); if(status==FFX_NR_COMPLETE) break; Sleep(1); } while(GetTickCount64()-start<1000);
    CHECK(status==FFX_NR_COMPLETE);
    if(status!=FFX_NR_COMPLETE) return 1;
    auto enrolledState=ffx::nr::createdRegistry().find(context);
    if(testMode==5){
        algorithmLease=std::make_shared<ffx::nr::AlgorithmState>(&algorithmCleanupCalls,[]{++algorithmCleanupCalls;ffx::nr::AlgorithmCleanup::Observe(0);return 0;});
        CHECK(algorithmLease->select(enrolledState,{nullptr,beginAlgorithm,endAlgorithm}));enrolledState->algorithm=algorithmLease;
        auto admission=algorithmLease->enter();CHECK(bool(admission));CHECK(algorithmLease->configure(0));
    }
    CHECK(enrolledState->boundedWorkers.load());
    CHECK(enrolledState->workerWakeEpoch.load()!=0);
    CHECK(enrolledState->workerAcknowledged[0]==enrolledState->workerWakeEpoch && enrolledState->workerAcknowledged[1]==enrolledState->workerWakeEpoch);
    if(testMode>=5) {
        CHECK(FAILED(swapchain->Present(0,0)));
        CHECK(FAILED(swapchain->GetBuffer(0,IID_PPV_ARGS(&buffer)))); CHECK(buffer==nullptr);
        ffx::nr::OwnedWriterCallbacks writer{prepareOutput,writeOutput,beforeWriteSubmit,afterWriteSubmit}; ffx::nr::OwnedWriteReceipt receipt{};
        CHECK(ffx::nr::serviceWriteOwnedOutput(&context,&writer,nullptr,&receipt)==(testMode==6?FFX_NR_API_FAILURE_RETAINED:FFX_NR_COMPLETE));
        CHECK(outputLease->inspect().writerSubmitted==(testMode!=6));
        CHECK(ffx::nr::serviceWriteOwnedOutput(&context,&writer,nullptr,&receipt)!=FFX_NR_COMPLETE);
        if(testMode==5){
            CHECK(FAILED(swapchain->Present(0,0)));CHECK(!outputLease->inspect().writerRetired);
            ffx::nr::OwnedWriterRetirementReceipt retired{};const auto limit=GetTickCount64()+1000;
            do{status=ffx::nr::serviceFinishWriter(&context,&retired);if(status==FFX_NR_PENDING)Sleep(1);}while(status==FFX_NR_PENDING && GetTickCount64()<limit);
            CHECK(status==FFX_NR_COMPLETE);CHECK(retired.completed && retired.oldRecordingRetired);CHECK(retired.oldRecording!=retired.newRecording);CHECK(outputLease->inspect().writerRetired);
            CHECK(ffx::nr::serviceFinishWriter(&context,&retired)==FFX_NR_COMPLETE);
        }
    }
    if(testMode!=6) CHECK(SUCCEEDED(swapchain->Present(0,0)));
    else CHECK(FAILED(swapchain->Present(0,0)));
    if(testMode==3) { ffx::nr::WorkScope root(swapchain); CHECK(bool(root)); CHECK(FAILED(ffx::nr::observeResult(swapchain->real()->Present(5,0)))); }
    if(testMode==4) { ffx::nr::WorkScope root(swapchain); CHECK(bool(root)); ID3D12Resource* invalid=nullptr; CHECK(FAILED(ffx::nr::observeResult(swapchain->real()->GetBuffer(UINT_MAX,IID_PPV_ARGS(&invalid))))); CHECK(invalid==nullptr); }

    CHECK(callbacks==(testMode==6?0u:1u)); CHECK(submissions==(testMode==6?0u:1u));
    if(testMode==5){FfxNrSubmitResultV1 receipt{};CHECK(outputLease->inspectSubmission(receipt));CHECK(receipt.execute_invoked==1);CHECK(receipt.dispatch.present_color==reinterpret_cast<uintptr_t>(outputLease->inspect().resource));}
    FfxNrDrainRequestV1 close{}; close.size=sizeof(close); close.version=1; close.context=context; close.operation_nonce=1; close.mode=1;
    LARGE_INTEGER frequency{}; QueryPerformanceFrequency(&frequency); close.absolute_deadline_qpc=ffx::nr::nowQpc()+frequency.QuadPart*5;
    FfxNrDrainHandleV1 handle{}; FfxNrDrainSummaryV1 summary{}; summary.size=sizeof(summary); summary.version=1;
    status=ffx::nr::serviceBegin(&close,&handle,&summary);
    while((status==FFX_NR_PENDING || status==FFX_NR_BUSY) && ffx::nr::nowQpc()<close.absolute_deadline_qpc) { Sleep(1); status=ffx::nr::servicePoll(&handle,close.absolute_deadline_qpc,&summary); }
    CHECK(status==((testMode==0 || testMode==5)?FFX_NR_COMPLETE:FFX_NR_API_FAILURE_RETAINED));
    CHECK(summary.domain_count>=40);
    CHECK(summary.worker_count==2);
    CHECK(summary.coverage_flags==0);
    if(testMode==0 || testMode==5) CHECK(summary.terminated_worker_count==2);
    FfxNrDomainEvidenceV1 domains[256]{}; uint32_t written=0;
    CHECK(ffx::nr::serviceRead(&handle,summary.evidence_revision,0,domains,256,&written)==FFX_NR_COMPLETE);
    bool checkedPresent=false;
    for(uint32_t i=0;i<written;++i) if(domains[i].logical_role_bits & (1u<<2)) { CHECK(domains[i].required_target==(testMode==6?0u:1u)); checkedPresent=true; }
    CHECK(checkedPresent);
    CHECK(FAILED(swapchain->Present(0,0)));
    if(testMode==5){
        CHECK(algorithmConsumerCalls==1);CHECK(ffx::nr::serviceConsume(&handle,summary.evidence_revision)==FFX_NR_PENDING);
        ffx::nr::AlgorithmReleaseReceipt receipt{};CHECK(algorithmLease->releaseAfterDrain(handle,summary.evidence_revision,receipt)==FFX_NR_COMPLETE);CHECK(receipt.wholeContextDestroyed);CHECK(algorithmCleanupCalls==1);
        CHECK(algorithmLease->releaseAfterDrain(handle,summary.evidence_revision,receipt)==FFX_NR_COMPLETE);CHECK(algorithmCleanupCalls==1);CHECK(!algorithmLease->enter());
    }
    if(status==FFX_NR_COMPLETE) CHECK(ffx::nr::serviceConsume(&handle,summary.evidence_revision)==FFX_NR_COMPLETE);
    if(testMode==5) { CHECK(outputLease->inspectRetirement().sdkComplete); CHECK(outputLease->inspectRetirement().admissionClosed); CHECK(outputLease->inspect().resource!=nullptr); outputLease.reset(); SafeRelease(outputHeap); }
    if(testMode==0 || testMode==5) { swapchain->Release(); SafeRelease(queue); SafeRelease(device); SafeRelease(warp); SafeRelease(factory); DestroyWindow(window); }
    std::printf("%u checks, %u failures\n",checks,failures); return failures?1:0;
}
