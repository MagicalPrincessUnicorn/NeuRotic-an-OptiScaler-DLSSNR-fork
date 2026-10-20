#include <windows.h>
#include <string>
static HMODULE vanished=nullptr;
static unsigned failedLookups=0;
static DWORD WINAPI ModulePathWithUnload(HMODULE module,LPWSTR path,DWORD capacity) {
    if(module==vanished){++failedLookups;SetLastError(ERROR_MOD_NOT_FOUND);return 0;}
    return GetModuleFileNameW(module,path,capacity);
}
#define GetModuleFileNameW ModulePathWithUnload
#include "../../../OptiScaler/dlssnr/CanonicalProviderIdentity.h"
#undef GetModuleFileNameW
#include <iostream>
int wmain(int argc,wchar_t** argv) {
    if(argc!=4)return 2;
    const std::wstring mode=argv[1];
    const auto provider=LoadLibraryW(argv[2]);if(!provider)return 3;
    const auto optional=LoadLibraryW(argv[3]);if(!optional)return 4;
    DlssNr::Canonical::Package identity;
    if(!identity.Open(argv[2])||!identity.Observe(provider))return 5;
    vanished=mode==L"required"?provider:(mode==L"optional"?optional:GetModuleHandleW(L"kernel32.dll"));
    const bool observed=identity.Observe(provider);
    if(mode==L"required") {
        if(observed||failedLookups!=1||identity.Error()!="LOADED_MODULE_PATH")return 6;
        vanished=nullptr;if(identity.Observe(provider))return 7;
    } else {
        if(!observed||failedLookups!=0||!identity.ProviderObserved())return 8;
        if(identity.ObservedProviderModule()!=provider)return 9;
    }
    std::cout<<identity.Json()<<"\nMODULE_CHURN_TEST_PASS\n";
}
