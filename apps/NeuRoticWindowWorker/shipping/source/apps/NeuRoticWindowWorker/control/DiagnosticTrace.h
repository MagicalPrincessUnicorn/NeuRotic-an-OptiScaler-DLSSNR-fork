// GPL-3.0. One producer / one consumer, fixed queue and file budgets. Metadata only.
#pragma once
#include <Windows.h>
#include <array>
#include <atomic>
#include <string>
#include <memory>
#include <thread>
#include <filesystem>
namespace nrw {
class TraceQueue {
    std::array<std::string,64> entries;
    std::atomic<uint64_t> head=0,tail=0;
public:
    std::atomic<uint64_t> dropped=0;
    bool Push(std::string line) {
        auto h=head.load(std::memory_order_relaxed);
        if(line.size()>8192 || h-tail.load(std::memory_order_acquire)>=entries.size()) {++dropped;return false;}
        entries[h%entries.size()]=std::move(line);
        head.store(h+1,std::memory_order_release);return true;
    }
    bool Pop(std::string& line) {
        auto t=tail.load(std::memory_order_relaxed);
        if(t==head.load(std::memory_order_acquire))return false;
        line=std::move(entries[t%entries.size()]);tail.store(t+1,std::memory_order_release);return true;
    }
};
class DiagnosticTrace {
    struct State {
        TraceQueue queue;
        HANDLE file=INVALID_HANDLE_VALUE;
        std::atomic<bool> stop=false,done=false;
        std::atomic<uint64_t> written=0,accepted=0;
        std::atomic<DWORD> error=0;
        ~State(){if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);}
    };
    std::shared_ptr<State> state=std::make_shared<State>();
    std::thread writer;
    std::wstring path;
public:
    explicit DiagnosticTrace(const std::wstring& requested={}) :path(requested) {
        if(path.empty())return;
        const auto drive=std::filesystem::path(path).root_name().wstring();
        if(!std::filesystem::path(path).is_absolute() || drive.size()!=2 || drive[1]!=L':') {state->error=ERROR_INVALID_NAME;return;}
        state->file=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,
                               FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if(state->file==INVALID_HANDLE_VALUE){state->error=GetLastError();return;}
        try {writer=std::thread([s=state] {
            for(;;) {
                std::string line;
                if(!s->queue.Pop(line)){
                    if(s->stop){if(!s->queue.Pop(line))break;}
                    else {Sleep(2);continue;}
                }
                line+='\n';DWORD written=0;
                if(!WriteFile(s->file,line.data(),DWORD(line.size()),&written,nullptr) || written!=line.size()) {
                    auto error=GetLastError();s->error=error?error:ERROR_WRITE_FAULT;break;
                }
                ++s->written;
            }
            s->done=true;
        });}catch(...){state->error=ERROR_NOT_ENOUGH_MEMORY;}
    }
    ~DiagnosticTrace() {
        state->stop=true;
        if(!writer.joinable())return;
        // A stalled diagnostic disk must not stall control/teardown or own GPU objects.
        for(int i=0;i<100 && !state->done;++i)Sleep(1);
        if(!state->done)CancelSynchronousIo(writer.native_handle());
        if(state->done)writer.join();else writer.detach(); // state/handle live until writer returns
    }
    bool Enabled()const{return !path.empty() && !state->error;}
    void Push(std::string line) {
        if(path.empty())return;
        if(state->error || state->accepted>=32768){++state->queue.dropped;return;}
        if(state->queue.Push(std::move(line)))++state->accepted;
    }
    uint64_t Dropped()const{return state->queue.dropped+(state->error?state->accepted.load()-state->written.load():0);}
    uint64_t Written()const{return state->written;}
    DWORD Error()const{return state->error;}
    const std::wstring& Path()const{return path;}
};
}
