#include <windows.h>
static BOOL WINAPI MissingSelfModule(DWORD,LPCWSTR,HMODULE*) {SetLastError(ERROR_MOD_NOT_FOUND);return FALSE;}
static bool denySidecar=false;
static HANDLE WINAPI SidecarFile(LPCWSTR name,DWORD access,DWORD share,LPSECURITY_ATTRIBUTES security,
                                DWORD disposition,DWORD flags,HANDLE templateFile) {
    if(denySidecar&&wcsstr(name,L"\\NeuRotic-NR-admission.json")){SetLastError(ERROR_ACCESS_DENIED);return INVALID_HANDLE_VALUE;}
    return CreateFileW(name,access,share,security,disposition,flags,templateFile);
}
#define GetModuleHandleExW MissingSelfModule
#define CreateFileW SidecarFile
#include "../../../OptiScaler/dlssnr/forwarder/dlssnr_forwarder.cpp"
#undef GetModuleHandleExW
#undef CreateFileW
#include <fstream>
#include <iostream>
int wmain(int argc,wchar_t** argv) {
    if(argc!=2)return 2;
    SetLastError(4321);
    ReportAdmission("PROVIDER_IDENTITY_REFUSED",argv[1]);
    if(GetLastError()!=4321)return 3;
    if(g_admissionJson.find("PROVIDER_IDENTITY_REFUSED")==std::string::npos)return 4;
    if(g_admissionJson.find("\"self_lookup_error\":126")==std::string::npos)return 5;
    const auto file=std::filesystem::path(argv[1]).parent_path()/L"NeuRotic-NR-admission.json";
    std::ifstream input(file,std::ios::binary);
    const std::string persisted((std::istreambuf_iterator<char>(input)),{});
    if(persisted!=g_admissionJson)return 6;
    denySidecar=true;
    ReportAdmission("LOAD_LIBRARY_FAILED",argv[1],193);
    wchar_t temp[32768]{};if(!GetTempPathW(32768,temp))return 7;
    const auto fallback=std::filesystem::path(temp)/(L"NeuRotic-NR-admission-"+std::to_wstring(GetCurrentProcessId())+L".json");
    std::ifstream backup(fallback,std::ios::binary);
    const std::string copied((std::istreambuf_iterator<char>(backup)),{});
    backup.close();DeleteFileW(fallback.c_str());
    if(copied!=g_admissionJson)return 8;
    std::cout<<"SELF_LOOKUP_AND_SIDECAR_FAILURE_RETAINS_ADMISSION_PASS\n";
}
