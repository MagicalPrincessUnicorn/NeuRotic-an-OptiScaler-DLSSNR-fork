// Included inside DllMain only by NR_ADAPTIVE_PUBLIC_PROVIDER_FIXTURE. Module
// paths and process-lifetime pinning above remain genuine. Provider binding and
// initialization are explicit launcher calls after LoadLibrary has returned.
#if !defined(NR_ADAPTIVE_PUBLIC_PROVIDER_FIXTURE) || NR_ADAPTIVE_PUBLIC_PROVIDER_FIXTURE != 1
#error Public product startup requires the explicit test build
#endif
    if(ul_reason_for_call==DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);
        dllModule=hModule;exeModule=GetModuleHandleW(nullptr);processId=GetCurrentProcessId();
    }
    return TRUE;
