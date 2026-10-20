#include <windows.h>
#include <vector>
#include <string>
#include <iostream>
#include <cstring>
#include <dlssnr/Feature18HostContract.h>

// Loads only our freshly built forwarder. The deliberately missing package must
// refuse before the proprietary model DLL can load, including on a later retry.
int wmain(int argc,wchar_t** argv) {
    if(argc!=4||GetModuleHandleW(L"nvngx_dlssnr.dll"))return 2;
    HMODULE forwarder=LoadLibraryW(argv[1]);if(!forwarder)return 3;
    using Probe=int(__cdecl*)(const wchar_t*);
    using Copy=unsigned long long(__cdecl*)(char*,unsigned long long);
    auto probe=reinterpret_cast<Probe>(GetProcAddress(forwarder,"dlssnr_call_probe_d3d12"));
    auto copy=reinterpret_cast<Copy>(GetProcAddress(forwarder,"dlssnr_call_canonical_identity"));
    if(!probe||!copy)return 4;
    const bool compatible=std::wstring(argv[2])==L"--unmatched-compatible";
    if(std::wstring(argv[2])==L"--unmatched" || compatible) {
        // Admission accepts a differently hashed file. One benign fixture
        // lacks the exports; the other supplies all required API symbols.
        if(probe(argv[3])!=(compatible?31:0)||!GetModuleHandleW(L"nvngx_dlssnr.dll"))return 10;
        using Contract=const char*(__cdecl*)();
        auto contract=reinterpret_cast<Contract>(GetProcAddress(forwarder,"dlssnr_call_host_recorded_contract_v1"));
        if(!contract)return 14;
        const auto* policy=contract();
        if(compatible?(!policy||std::strcmp(policy,DlssNr::Feature18HostContractIdentity)):policy!=nullptr)return 14;
        auto size=copy(nullptr,0);if(!size||size>1024*1024)return 11;
        std::vector<char> selected(static_cast<size_t>(size));
        if(copy(selected.data(),selected.size())!=size)return 12;
        const std::string evidence(selected.data());
        if(evidence.find("\"selection_verified\":true")==std::string::npos||
           evidence.find("\"provider_loaded\":true")==std::string::npos||
           evidence.find("\"refused\":false")==std::string::npos)return 13;
        std::cout<<evidence<<"\nFORWARDER_UNMATCHED_PASS\n";
        FreeLibrary(forwarder);return 0;
    }
    if(probe(argv[2])!=0||GetModuleHandleW(L"nvngx_dlssnr.dll"))return 5;
    auto required=copy(nullptr,0);if(!required||required>1024*1024)return 6;
    char untouched='#';if(copy(&untouched,1)!=required||untouched!='#')return 7;
    std::vector<char> bytes(static_cast<size_t>(required));
    if(copy(bytes.data(),bytes.size())!=required||bytes.back()!=0)return 8;
    const std::string evidence(bytes.data());
    if(evidence.find("\"refused\":true")==std::string::npos||evidence.find("\"provider_loaded\":false")==std::string::npos)return 9;
    if(probe(argv[3])!=0||GetModuleHandleW(L"nvngx_dlssnr.dll"))return 10;
    std::cout<<evidence<<"\nFORWARDER_IDENTITY_PASS\n";
    FreeLibrary(forwarder);return 0;
}
