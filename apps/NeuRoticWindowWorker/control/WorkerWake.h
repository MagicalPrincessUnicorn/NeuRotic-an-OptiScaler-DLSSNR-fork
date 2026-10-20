// GPL-3.0. Wakeups schedule owner-thread work; they never prove GPU completion.
#pragma once
#include <Windows.h>
#include <stdexcept>

namespace nrw {
class WakeEvent {
    HANDLE event_=CreateEventW(nullptr,FALSE,FALSE,nullptr);
public:
    WakeEvent(){if(!event_)throw std::runtime_error("Worker wake event creation failed");}
    ~WakeEvent(){CloseHandle(event_);}
    WakeEvent(const WakeEvent&)=delete;
    WakeEvent& operator=(const WakeEvent&)=delete;
    HANDLE Handle()const{return event_;}
    void Signal()const noexcept {SetEvent(event_);}
    void Consume()const {if(WaitForSingleObject(event_,0)==WAIT_FAILED)throw std::runtime_error("Worker wake event failed");}
};
enum class WakeReason { Command, Capture, Message, Maintenance };
inline WakeReason WaitForWorkerActivity(HANDLE command,HANDLE capture,DWORD maintenanceMs=50) {
    HANDLE handles[2]{};WakeReason reasons[2]{};DWORD count=0;
    if(command){handles[count]=command;reasons[count++]=WakeReason::Command;}
    if(capture){handles[count]=capture;reasons[count++]=WakeReason::Capture;}
    // Message-aware wait keeps Stop/hotkeys/window changes responsive without a
    // timer tick after every processed frame. Timeout checks external focus/size
    // changes and countdown/console cancellation even for a static source.
    const DWORD result=MsgWaitForMultipleObjectsEx(count,handles,maintenanceMs,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
    if(result>=WAIT_OBJECT_0 && result<WAIT_OBJECT_0+count)return reasons[result-WAIT_OBJECT_0];
    if(result==WAIT_OBJECT_0+count)return WakeReason::Message;
    if(result==WAIT_TIMEOUT)return WakeReason::Maintenance;
    throw std::runtime_error("Worker activity wait failed");
}
}
