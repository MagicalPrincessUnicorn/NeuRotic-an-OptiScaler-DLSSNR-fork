#pragma once
#include "PreparedHelperProtocol.h"
#include <Windows.h>
#include <filesystem>
#include <string>
#include <vector>
#include <span>
#include <memory>
#include <atomic>
#include <cstring>
namespace Neurotic::Feed::Prepared::Helper
{
struct ClientOptions {std::wstring executable,model,forwarder,adapterLuid,ngxPath;std::uint32_t timeoutMs=60000;
    float colorStrength=0;bool probeContent=false;};
// Explicit, one-request-at-a-time CPU transport. A timeout closes this session;
// there is never a game-queue wait on a helper-owned fence. Caller serializes use.
class Client {
    struct Handle {HANDLE value=nullptr;~Handle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}void Reset(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);value=nullptr;}};
    struct Operation {OVERLAPPED overlapped{};Handle event;std::vector<std::byte> bytes;
        explicit Operation(std::size_t size):bytes(size){event.value=CreateEventW(nullptr,TRUE,FALSE,nullptr);overlapped.hEvent=event.value;}};
    Handle input_,output_,process_;std::uint32_t timeoutMs_=60000;bool failed_=false;
    static std::wstring Quote(const std::wstring& value){
        std::wstring out=L"\"";std::size_t slashes=0;
        for(wchar_t c:value){if(c==L'\\'){++slashes;continue;}if(c==L'\"'){out.append(slashes*2+1,L'\\');out+=c;slashes=0;continue;}
            out.append(slashes,L'\\');slashes=0;out+=c;}out.append(slashes*2,L'\\');out+=L'\"';return out;
    }
    static bool Pipe(Handle& parent,Handle& child,bool parentWrites){
        static std::atomic<std::uint64_t> sequence{0};
        const auto name=L"\\\\.\\pipe\\NeuRotic.PreparedHelper."+std::to_wstring(GetCurrentProcessId())+L"."+
            std::to_wstring(GetTickCount64())+L"."+std::to_wstring(++sequence);
        parent.value=CreateNamedPipeW(name.c_str(),(parentWrites?PIPE_ACCESS_OUTBOUND:PIPE_ACCESS_INBOUND)|FILE_FLAG_OVERLAPPED|
            FILE_FLAG_FIRST_PIPE_INSTANCE,PIPE_TYPE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,65536,65536,0,nullptr);
        if(parent.value==INVALID_HANDLE_VALUE){parent.value=nullptr;return false;}
        SECURITY_ATTRIBUTES attributes{sizeof(attributes),nullptr,TRUE};
        child.value=CreateFileW(name.c_str(),parentWrites?GENERIC_READ:GENERIC_WRITE,0,&attributes,OPEN_EXISTING,0,nullptr);
        if(child.value==INVALID_HANDLE_VALUE){child.value=nullptr;return false;}return true;
    }
    bool Transfer(HANDLE handle,void* bytes,std::size_t size,bool writing,std::uint64_t deadline,std::string& reason){
        auto* cursor=static_cast<std::byte*>(bytes);
        while(size){const auto count=(std::min)(size,std::size_t(1024*1024));auto operation=std::make_unique<Operation>(count);
            if(!operation->event.value){reason="Helper I/O event unavailable";return false;}
            if(writing)std::memcpy(operation->bytes.data(),cursor,count);DWORD done=0;
            const auto started=writing?WriteFile(handle,operation->bytes.data(),static_cast<DWORD>(count),&done,&operation->overlapped):
                ReadFile(handle,operation->bytes.data(),static_cast<DWORD>(count),&done,&operation->overlapped);
            if(!started){if(GetLastError()!=ERROR_IO_PENDING){reason="Helper protocol pipe closed";return false;}
                const auto now=GetTickCount64();const auto remaining=now>=deadline?0:static_cast<DWORD>((std::min)(deadline-now,std::uint64_t(MAXDWORD-1)));
                if(WaitForSingleObject(operation->event.value,remaining)!=WAIT_OBJECT_0){
                    CancelIoEx(handle,&operation->overlapped);
                    if(WaitForSingleObject(operation->event.value,5000)!=WAIT_OBJECT_0)(void)operation.release();
                    reason="Helper protocol I/O timeout; session closed";return false;}
                if(!GetOverlappedResult(handle,&operation->overlapped,&done,FALSE)){reason="Helper protocol transfer failed";return false;}}
            if(!done||done>count){reason="Helper protocol made no bounded progress";return false;}
            if(!writing)std::memcpy(cursor,operation->bytes.data(),done);cursor+=done;size-=done;
        }
        return true;
    }
public:
    ~Client(){Stop();}Client()=default;Client(const Client&)=delete;Client& operator=(const Client&)=delete;
    bool Alive()const{return process_.value&&!failed_&&WaitForSingleObject(process_.value,0)==WAIT_TIMEOUT;}
    void Stop(){input_.Reset();output_.Reset();process_.Reset();failed_=true;}
    bool Start(const ClientOptions& options,std::string& reason){
        if(process_.value){reason="Helper client already owns a process";return false;}
        if(options.timeoutMs<1||options.timeoutMs>120000||options.adapterLuid.empty()||!std::isfinite(options.colorStrength)||
           options.colorStrength<0||options.colorStrength>1||
           !std::filesystem::path(options.executable).is_absolute()||!std::filesystem::path(options.model).is_absolute()||
           !std::filesystem::path(options.forwarder).is_absolute()||(!options.ngxPath.empty()&&!std::filesystem::path(options.ngxPath).is_absolute())){
            reason="Explicit absolute helper/model/forwarder paths, adapter LUID and bounded timeout required";return false;}
        Handle childInput,childOutput,childError;
        if(!Pipe(input_,childInput,true)||!Pipe(output_,childOutput,false)){Stop();reason="Helper inherited pipes unavailable";return false;}
        auto error=GetStdHandle(STD_ERROR_HANDLE);
        if(!error||error==INVALID_HANDLE_VALUE||!DuplicateHandle(GetCurrentProcess(),error,GetCurrentProcess(),&childError.value,0,TRUE,DUPLICATE_SAME_ACCESS)){
            SECURITY_ATTRIBUTES attributes{sizeof(attributes),nullptr,TRUE};childError.value=CreateFileW(L"NUL",GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&attributes,OPEN_EXISTING,0,nullptr);}
        if(!childError.value||childError.value==INVALID_HANDLE_VALUE){Stop();reason="Helper diagnostics handle unavailable";return false;}
        std::wstring command=Quote(options.executable)+L" --model "+Quote(options.model)+L" --forwarder "+Quote(options.forwarder)+L" --adapter-luid "+Quote(options.adapterLuid);
        if(!options.ngxPath.empty())command+=L" --ngx "+Quote(options.ngxPath);
        if(options.colorStrength!=0)command+=L" --color-strength "+std::to_wstring(options.colorStrength);
        if(options.probeContent)command+=L" --probe-content 1";
        SIZE_T attributeSize=0;InitializeProcThreadAttributeList(nullptr,1,0,&attributeSize);std::vector<std::byte> attributes(attributeSize);
        auto* attributeList=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
        if(!InitializeProcThreadAttributeList(attributeList,1,0,&attributeSize)){Stop();reason="Helper handle restriction unavailable";return false;}
        struct AttributeCleanup{LPPROC_THREAD_ATTRIBUTE_LIST list;~AttributeCleanup(){DeleteProcThreadAttributeList(list);}} cleanup{attributeList};
        HANDLE inherit[]{childInput.value,childOutput.value,childError.value};
        if(!UpdateProcThreadAttribute(attributeList,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,inherit,sizeof(inherit),nullptr,nullptr)){
            Stop();reason="Helper handle inheritance restriction refused";return false;}
        STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);startup.lpAttributeList=attributeList;
        startup.StartupInfo.dwFlags=STARTF_USESTDHANDLES;startup.StartupInfo.hStdInput=childInput.value;
        startup.StartupInfo.hStdOutput=childOutput.value;startup.StartupInfo.hStdError=childError.value;
        PROCESS_INFORMATION process{};
        if(!CreateProcessW(options.executable.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|EXTENDED_STARTUPINFO_PRESENT,
            nullptr,nullptr,&startup.StartupInfo,&process)){Stop();reason="Explicit helper process creation refused";return false;}
        CloseHandle(process.hThread);process_.value=process.hProcess;timeoutMs_=options.timeoutMs;failed_=false;reason.clear();return true;
    }
    bool Process(const Request& request,std::span<const std::byte> packed,Reply& reply,std::vector<std::uint8_t>& output,std::string& reason){
        output.clear();reply={};
        if(!Alive()||!Validate(request)||packed.size()!=request.header.payloadBytes){reason="Helper frame/session admission refused";return false;}
        const auto deadline=GetTickCount64()+timeoutMs_;auto copy=request;
        if(!Transfer(input_.value,&copy,sizeof(copy),true,deadline,reason)||
           !Transfer(input_.value,const_cast<std::byte*>(packed.data()),packed.size(),true,deadline,reason)||
           !Transfer(output_.value,&reply,sizeof(reply),false,deadline,reason)){Stop();return false;}
        if(reply.header.magic!=Magic||reply.header.version!=Version||reply.header.bytes!=sizeof(Reply)||reply.header.kind!=2||reply.reserved||
           (reply.diagnostics&~(DistrustDiagnosticOnly|DlaaEvaluated|DistrustPublicDlaa))||
           reply.header.payloadBytes>MaxPayloadBytes||reply.result.identity!=Ipc::IdentityOf(request.guides)||!Ipc::Valid(reply.result)){
            Stop();reason="Helper response envelope/identity mismatch";return false;}
        if(reply.result.state!=Ipc::Outcome::CompletedSuccess){
            if(reply.header.payloadBytes){Stop();reason="Failed helper response carried output";return false;}
            reason="Helper returned explicit failed/cancelled outcome";return false;}
        if(!CompletedReply(reply,request)){Stop();reason="Helper successful result/readback identity invalid";return false;}
        output.resize(static_cast<std::size_t>(reply.header.payloadBytes));
        if(!Transfer(output_.value,output.data(),output.size(),false,deadline,reason)){output.clear();Stop();return false;}
        reason.clear();return true;
    }
};
}
