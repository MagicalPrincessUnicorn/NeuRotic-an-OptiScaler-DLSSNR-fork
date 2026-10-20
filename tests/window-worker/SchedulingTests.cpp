// GPL-3.0. Native event/message contracts; no model or source application.
#include "../../apps/NeuRoticWindowWorker/control/WorkerWake.h"
#include <iostream>
#include <memory>
#include <thread>
int main() try {
    using namespace nrw;int checks=0;
    auto check=[&](bool ok,const char* what){if(!ok)throw std::runtime_error(what);++checks;};
    MSG msg{};PeekMessageW(&msg,nullptr,0,0,PM_NOREMOVE);
    WakeEvent command,frame;
    check(WaitForWorkerActivity(command.Handle(),frame.Handle(),0)==WakeReason::Maintenance,"idle has no false readiness");
    frame.Signal();check(WaitForWorkerActivity(command.Handle(),frame.Handle(),0)==WakeReason::Capture,"already queued frame wakes without timeout");
    check(WaitForWorkerActivity(command.Handle(),frame.Handle(),0)==WakeReason::Maintenance,"capture notification consumed without spin");
    frame.Signal();command.Signal();
    check(WaitForWorkerActivity(command.Handle(),frame.Handle(),0)==WakeReason::Command,"commands win simultaneous wake");
    check(WaitForWorkerActivity(command.Handle(),frame.Handle(),0)==WakeReason::Capture,"command wake does not lose frame");
    for(int i=0;i<100;++i)frame.Signal();
    check(WaitForWorkerActivity(nullptr,frame.Handle(),0)==WakeReason::Capture,"burst coalesces");
    check(WaitForWorkerActivity(nullptr,frame.Handle(),0)==WakeReason::Maintenance,"no per-frame callback backlog");
    frame.Signal();frame.Consume();frame.Signal();
    check(WaitForWorkerActivity(nullptr,frame.Handle(),0)==WakeReason::Capture,"arrival after pre-poll consumption survives");
    std::thread producer([&]{Sleep(20);frame.Signal();});
    auto wake=WaitForWorkerActivity(command.Handle(),frame.Handle(),2000);producer.join();
    check(wake==WakeReason::Capture,"frame arriving during wait wakes owner");
    frame.Signal();check(WaitForWorkerActivity(command.Handle(),nullptr,0)==WakeReason::Maintenance,"paused capture excluded from wake set");frame.Consume();
    const DWORD owner=GetCurrentThreadId();
    std::thread messenger([owner]{Sleep(20);PostThreadMessageW(owner,WM_APP+99,0,0);});
    wake=WaitForWorkerActivity(command.Handle(),frame.Handle(),2000);messenger.join();
    check(wake==WakeReason::Message,"window messages wake idle owner");
    check(PeekMessageW(&msg,nullptr,WM_APP+99,WM_APP+99,PM_NOREMOVE)!=FALSE,"message remains for owner pump");
    check(WaitForWorkerActivity(nullptr,nullptr,0)==WakeReason::Message,"previously observed messages still wake");
    PeekMessageW(&msg,nullptr,WM_APP+99,WM_APP+99,PM_REMOVE);
    auto lifetime=std::make_shared<WakeEvent>();std::weak_ptr<WakeEvent> weak=lifetime;
    auto callback=[retained=lifetime]{retained->Signal();};lifetime.reset();callback();
    check(!weak.expired() && WaitForWorkerActivity(nullptr,weak.lock()->Handle(),0)==WakeReason::Capture,"callback retains event after capture owner retires");
    bool failed=false;try{WaitForWorkerActivity(INVALID_HANDLE_VALUE,nullptr,0);}catch(const std::runtime_error&){failed=true;}
    check(failed,"invalid wait fails instead of spinning");
    std::cout<<"PASS native scheduler "<<checks<<" checks\n";return 0;
} catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
