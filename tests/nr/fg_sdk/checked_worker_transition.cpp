#define NOMINMAX
#include <Windows.h>
#include <limits>
#include <cmath>
#include <thread>
#include <atomic>
#include <cstdio>
#include "external/FidelityFX-SDK/sdk/src/backends/dx12/FrameInterpolationSwapchain/FrameInterpolationSwapchainDX12_Helpers.h"
#include "external/FidelityFX-CheckedClosure/CheckedSdkService.h"
static unsigned checks,failures;
#define CHECK(x) do { ++checks; if(!(x)) { ++failures; std::printf("FAIL line %d: %s\n",__LINE__,#x); } } while(0)
int main() {
    const auto waitPublication=[]<class T>(T& state,DWORD budget){
        if constexpr(requires{waitWorkerPublication(state,budget);})return waitWorkerPublication(state,budget);
        else return FFX_NR_UNSUPPORTED;
    };
    int startup=0,startAlias=0;auto starting=ffx::nr::createdRegistry().create(&startup,&startAlias,1);
    HANDLE ready[2]={CreateEventW(nullptr,FALSE,FALSE,nullptr),CreateEventW(nullptr,FALSE,FALSE,nullptr)};
    CHECK(ready[0]&&ready[1]);
    starting->workerHandles[0]=ready[0];
    std::thread delayed([&]{Sleep(20);starting->workerHandles[1]=ready[1];});
    CHECK(waitPublication(*starting,250)==FFX_NR_COMPLETE);delayed.join();
    CHECK(!starting->quarantined.load());CHECK(!starting->enrolled.load());
    CHECK(waitPublication(*starting,0)==FFX_NR_COMPLETE);
    int lost=0,lostAlias=0;auto absent=ffx::nr::createdRegistry().create(&lost,&lostAlias,1);
    CHECK(waitPublication(*absent,10)==FFX_NR_TIMEOUT_RETAINED);
    CHECK(absent->quarantined.load()&&absent->closure.failed());
    absent->workerHandles[0]=ready[0];absent->workerHandles[1]=ready[1];
    CHECK(waitPublication(*absent,20)==FFX_NR_API_FAILURE_RETAINED);
    for(auto handle:ready)CloseHandle(handle);
    wchar_t name[96]; swprintf_s(name,L"Local\\NrSdkEventCollision-%lu",GetCurrentProcessId());
    HANDLE collision=CreateMutexW(nullptr,FALSE,name),event=nullptr; CHECK(collision!=nullptr);
    CHECK(FAILED(ffx::nr::createCheckedEvent(false,event,name))); CHECK(event==nullptr); CloseHandle(collision);
    CHECK(SUCCEEDED(ffx::nr::createCheckedEvent(false,event))); CloseHandle(event);
    int exact=0,alias=0; auto state=ffx::nr::createdRegistry().create(&exact,&alias,1);
    HANDLE events[2]{}; CHECK(SUCCEEDED(ffx::nr::createCheckedEvent(false,events[0]))); CHECK(SUCCEEDED(ffx::nr::createCheckedEvent(false,events[1])));
    std::atomic<unsigned> entered{0}; std::atomic<bool> stop{false};
    std::thread workers[2];
    for(unsigned i=0;i<2;++i) workers[i]=std::thread([&,i] { ffx::nr::WorkerScope scope(&alias,i); ++entered; while(!stop.load()) ffx::nr::waitWorkerEvent(&alias,events[i]); });
    while(entered.load()!=2) std::this_thread::yield(); Sleep(20);
    CHECK(state->workerAcknowledged[0]==0 && state->workerAcknowledged[1]==0);
    CHECK(ffx::nr::acknowledgeWorkers(*state,events,250)==FFX_NR_COMPLETE);
    const auto epoch=state->workerWakeEpoch.load(); CHECK(epoch!=0); CHECK(state->workerAcknowledged[0]==epoch && state->workerAcknowledged[1]==epoch);
    CHECK(state->boundedWorkers.load()); CHECK(!state->quarantined.load());
    // Sticky bounded mode survives a failed/cancelled enrollment attempt.
    state->enrolling=false; stop=true; for(auto& worker:workers) worker.join();
    for(auto handle:events) CloseHandle(handle); CHECK(ffx::nr::createdRegistry().remove(&exact));
    int missing=0,missingAlias=0; auto missingState=ffx::nr::createdRegistry().create(&missing,&missingAlias,1);
    for(auto& handle:events) CHECK(SUCCEEDED(ffx::nr::createCheckedEvent(false,handle)));
    const auto before=GetTickCount64(); CHECK(ffx::nr::acknowledgeWorkers(*missingState,events,30)==FFX_NR_TIMEOUT_RETAINED); CHECK(GetTickCount64()-before<500);
    CHECK(missingState->quarantined.load()); { ffx::nr::WorkScope root(&missing); CHECK(!root); } { ffx::nr::DestroyScope destroy(missingState); CHECK(!destroy); }
    for(auto handle:events) CloseHandle(handle);
    int denied=0,deniedAlias=0; auto deniedState=ffx::nr::createdRegistry().create(&denied,&deniedAlias,1);
    events[0]=CreateEventExW(nullptr,nullptr,0,SYNCHRONIZE); CHECK(events[0]!=nullptr); CHECK(SUCCEEDED(ffx::nr::createCheckedEvent(false,events[1])));
    CHECK(ffx::nr::acknowledgeWorkers(*deniedState,events,30)==FFX_NR_API_FAILURE_RETAINED); CHECK(deniedState->quarantined.load()); CHECK(deniedState->closure.failed());
    for(auto handle:events) CloseHandle(handle);
    // Invalid publication in an admitted root latches the actual Win32 failure.
    int publication=0,pubAlias=0; auto pub=ffx::nr::createdRegistry().create(&publication,&pubAlias,1); pub->enrolled=true; pub->closure.sealInventory();
    { ffx::nr::WorkScope root(&publication); CHECK(bool(root)); CHECK(!ffx::nr::publishEvent(nullptr)); }
    auto drain=pub->closure.begin(1); CHECK(pub->closure.poll(drain,1,100)==FFX_NR_API_FAILURE_RETAINED);
    struct FailedEnrollment { unsigned pins=0; ffx::nr::CreatedContext* state=nullptr; HANDLE events[2]{}; } failed;
    int failedAlias=0; auto failedState=ffx::nr::createdRegistry().create(&failed,&failedAlias,1); failed.state=failedState.get();
    for(auto& handle:failed.events) CHECK(SUCCEEDED(ffx::nr::createCheckedEvent(false,handle)));
    failedState->retainPin=[](void* p){++static_cast<FailedEnrollment*>(p)->pins;};
    failedState->releasePin=[](void* p){--static_cast<FailedEnrollment*>(p)->pins;};
    failedState->enrollOwner=[](void* p)->FfxNrStatusV1 { auto& f=*static_cast<FailedEnrollment*>(p); return ffx::nr::acknowledgeWorkers(*f.state,f.events,20); };
    FfxNrEnrollRequestV1 request{sizeof(request),1,1,0,0}; FfxNrContextTicketV1 id{};
    CHECK(ffx::nr::serviceEnroll(&failedAlias,&request,&id)==FFX_NR_TIMEOUT_RETAINED);
    CHECK(failed.pins==1); CHECK(failedState->quarantined.load());
    CHECK(ffx::nr::serviceEnroll(&failedAlias,&request,&id)==FFX_NR_API_FAILURE_RETAINED); CHECK(failed.pins==1);
    { ffx::nr::DestroyScope destruction(failedState); CHECK(!destruction); }
    CHECK(!ffx::nr::createdRegistry().remove(&failed));
    for(auto handle:failed.events) CloseHandle(handle);
    std::printf("%u checks, %u failures\n",checks,failures); return failures?1:0;
}
