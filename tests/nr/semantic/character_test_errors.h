#pragma once
#include <windows.h>
#include <werapi.h>
#include <cstdio>
#pragma comment(lib,"wer.lib")
inline LONG WINAPI CharacterTestUnhandled(EXCEPTION_POINTERS* details){
    std::fprintf(stderr,"FAIL native test exception 0x%08lX\n",details->ExceptionRecord->ExceptionCode);
    std::fflush(stderr);return EXCEPTION_EXECUTE_HANDLER;
}
inline void CharacterTestQuietFailures(){
    // Test-process scope only: never change user/system error reporting policy.
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    WerSetFlags(WER_FAULT_REPORTING_NO_UI);
    SetUnhandledExceptionFilter(CharacterTestUnhandled);
}
