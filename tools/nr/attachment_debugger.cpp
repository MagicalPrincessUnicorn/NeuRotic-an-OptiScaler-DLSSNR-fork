// Provider-free test process supervisor. Captures a second-chance exception
// before Windows error UI, plus remote stack and a diagnostic minidump.
#include <windows.h>
#include <dbghelp.h>
#include <cstdio>
#include <string>
#include <map>
#pragma comment(lib,"dbghelp.lib")
int wmain(int argc,wchar_t** argv)
{
    if(argc<3)return 2;
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    std::wstring command=L"\""+std::wstring(argv[1])+L"\"";
    for(int i=3;i<argc;++i)
    {
        // Regression arguments are opaque run IDs, never shell expressions.
        if(wcspbrk(argv[i],L"\"\\\r\n"))return 2;
        command+=L" \""+std::wstring(argv[i])+L"\"";
    }
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    startup.dwFlags=STARTF_USESTDHANDLES;
    startup.hStdInput=GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput=GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError=GetStdHandle(STD_ERROR_HANDLE);
    if(!CreateProcessW(argv[1],command.data(),nullptr,nullptr,TRUE,DEBUG_ONLY_THIS_PROCESS,
        nullptr,nullptr,&startup,&process)){std::printf("CREATE_ERROR=%lu\n",GetLastError());return 2;}
    SymSetOptions(SYMOPT_LOAD_LINES|SYMOPT_UNDNAME|SYMOPT_DEFERRED_LOADS);
    SymInitialize(process.hProcess,nullptr,FALSE);
    const auto start=GetTickCount64();int result=2;
    std::map<DWORD,HANDLE> threads;
    std::map<DWORD64,std::wstring> modules;
    const auto moduleLoaded=[&](HANDLE file,void* base){
        wchar_t path[32768]{};
        if(file)GetFinalPathNameByHandleW(file,path,32768,FILE_NAME_NORMALIZED);
        const auto address=reinterpret_cast<DWORD64>(base);modules[address]=path;
        const auto loaded=SymLoadModuleExW(process.hProcess,file,path,nullptr,address,0,nullptr,0);
        std::wprintf(L"MODULE base=%llx symbols=%llx path=%s\n",address,loaded,path);
    };
    for(;;)
    {
        DEBUG_EVENT event{};
        if(!WaitForDebugEvent(&event,1000))
        {if(GetTickCount64()-start<90000)continue;std::puts("TIMEOUT");TerminateProcess(process.hProcess,124);result=124;break;}
        DWORD action=DBG_CONTINUE;
        if(event.dwDebugEventCode==CREATE_PROCESS_DEBUG_EVENT)
        {
            threads[event.dwThreadId]=event.u.CreateProcessInfo.hThread;
            moduleLoaded(event.u.CreateProcessInfo.hFile,event.u.CreateProcessInfo.lpBaseOfImage);
            if(event.u.CreateProcessInfo.hFile)CloseHandle(event.u.CreateProcessInfo.hFile);
        }
        else if(event.dwDebugEventCode==CREATE_THREAD_DEBUG_EVENT)threads[event.dwThreadId]=event.u.CreateThread.hThread;
        else if(event.dwDebugEventCode==EXIT_THREAD_DEBUG_EVENT)
        {auto found=threads.find(event.dwThreadId);if(found!=threads.end()){CloseHandle(found->second);threads.erase(found);}}
        else if(event.dwDebugEventCode==LOAD_DLL_DEBUG_EVENT)
        {
            moduleLoaded(event.u.LoadDll.hFile,event.u.LoadDll.lpBaseOfDll);
            if(event.u.LoadDll.hFile)CloseHandle(event.u.LoadDll.hFile);
        }
        else if(event.dwDebugEventCode==EXCEPTION_DEBUG_EVENT)
        {
            const auto& ex=event.u.Exception.ExceptionRecord;
            if(ex.ExceptionCode!=EXCEPTION_BREAKPOINT)action=DBG_EXCEPTION_NOT_HANDLED;
            if(!event.u.Exception.dwFirstChance)
            {
                std::printf("SECOND_CHANCE code=0x%08lx address=%p parameters=%lu\n",ex.ExceptionCode,ex.ExceptionAddress,ex.NumberParameters);
                for(DWORD i=0;i<ex.NumberParameters;++i)std::printf("parameter[%lu]=0x%llx\n",i,static_cast<unsigned long long>(ex.ExceptionInformation[i]));
                CONTEXT context{};context.ContextFlags=CONTEXT_ALL;
                HANDLE thread=threads[event.dwThreadId];GetThreadContext(thread,&context);
                std::printf("RIP=%llx RSP=%llx RCX=%llx RDX=%llx R8=%llx R9=%llx\n",context.Rip,context.Rsp,context.Rcx,context.Rdx,context.R8,context.R9);
                auto faultModule=modules.upper_bound(context.Rip);
                if(faultModule!=modules.begin()){--faultModule;std::wprintf(L"FAULT_MODULE=%s RVA=0x%llx\n",faultModule->second.c_str(),context.Rip-faultModule->first);}
                EXCEPTION_RECORD record=ex;EXCEPTION_POINTERS pointers{&record,&context};
                MINIDUMP_EXCEPTION_INFORMATION info{event.dwThreadId,&pointers,FALSE};
                HANDLE dump=CreateFileW(argv[2],GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
                const BOOL written=dump!=INVALID_HANDLE_VALUE&&MiniDumpWriteDump(process.hProcess,process.dwProcessId,dump,
                    static_cast<MINIDUMP_TYPE>(MiniDumpWithThreadInfo|MiniDumpWithIndirectlyReferencedMemory),&info,nullptr,nullptr);
                std::printf("DUMP_WRITTEN=%d\n",written);if(dump!=INVALID_HANDLE_VALUE)CloseHandle(dump);
                STACKFRAME64 frame{};frame.AddrPC={context.Rip,0,AddrModeFlat};frame.AddrStack={context.Rsp,0,AddrModeFlat};frame.AddrFrame={context.Rbp,0,AddrModeFlat};
                for(unsigned i=0;i<48;++i)
                {
                    const DWORD64 address=frame.AddrPC.Offset;if(!address)break;
                    IMAGEHLP_MODULE64 module{};module.SizeOfStruct=sizeof(module);SymGetModuleInfo64(process.hProcess,address,&module);
                    char buffer[sizeof(SYMBOL_INFO)+1024]{};auto* symbol=reinterpret_cast<SYMBOL_INFO*>(buffer);symbol->SizeOfStruct=sizeof(SYMBOL_INFO);symbol->MaxNameLen=1023;DWORD64 displacement=0;
                    const bool named=SymFromAddr(process.hProcess,address,&displacement,symbol)!=FALSE;
                    IMAGEHLP_LINE64 line{};line.SizeOfStruct=sizeof(line);DWORD offset=0;const bool located=SymGetLineFromAddr64(process.hProcess,address,&offset,&line)!=FALSE;
                    std::printf("#%u %llx %s+0x%llx %s+0x%llx %s:%lu\n",i,address,module.ModuleName,address-module.BaseOfImage,named?symbol->Name:"?",displacement,located?line.FileName:"?",located?line.LineNumber:0);
                    if(!StackWalk64(IMAGE_FILE_MACHINE_AMD64,process.hProcess,thread,&frame,&context,nullptr,SymFunctionTableAccess64,SymGetModuleBase64,nullptr))break;
                }
                TerminateProcess(process.hProcess,ex.ExceptionCode);result=3;
                ContinueDebugEvent(event.dwProcessId,event.dwThreadId,DBG_CONTINUE);break;
            }
        }
        else if(event.dwDebugEventCode==EXIT_PROCESS_DEBUG_EVENT)
        {std::printf("PROCESS_EXIT=%lu\n",event.u.ExitProcess.dwExitCode);result=static_cast<int>(event.u.ExitProcess.dwExitCode);ContinueDebugEvent(event.dwProcessId,event.dwThreadId,action);break;}
        ContinueDebugEvent(event.dwProcessId,event.dwThreadId,action);
    }
    WaitForSingleObject(process.hProcess,5000);SymCleanup(process.hProcess);
    for(const auto& entry:threads)CloseHandle(entry.second);
    CloseHandle(process.hThread);CloseHandle(process.hProcess);return result;
}
