#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

// Probe local fixtures only. Never call provider Init/Create/Evaluate.
int wmain(int argc,wchar_t** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOOPENFILEERRORBOX);
    if(argc!=5)return 2;
    auto module=LoadLibraryW(argv[1]);if(!module)return 3;
    using Probe=int(__cdecl*)(const wchar_t*);
    using Copy=unsigned long long(__cdecl*)(char*,unsigned long long);
    auto probe=reinterpret_cast<Probe>(GetProcAddress(module,"dlssnr_call_probe_d3d12"));
    auto copy=reinterpret_cast<Copy>(GetProcAddress(module,"dlssnr_call_admission_diagnostic_v1"));
    if(!probe||!copy)return 4;
    const auto expected=std::stoi(argv[4]);
    if(probe(argv[2])!=expected)return 5;
    const auto size=copy(nullptr,0);if(!size||size>1024*1024)return 6;
    char sentinel='#';if(copy(&sentinel,1)!=size||sentinel!='#')return 7;
    std::vector<char> bytes(static_cast<size_t>(size));
    if(copy(bytes.data(),bytes.size())!=size||bytes.back()!=0)return 8;
    const std::string json(bytes.data());
    std::string stage;for(const auto c:std::wstring(argv[3]))stage+=static_cast<char>(c);
    if(json.find("\"stage\":\""+stage+"\"")==std::string::npos||
       json.find("\"init_entered\":false")==std::string::npos||
       json.find("\"create_entered\":false")==std::string::npos||
       json.find("r32-provider-scope")==std::string::npos)return 9;
    if(stage=="LOAD_LIBRARY_FAILED"&&json.find("\"win32_error\":193")==std::string::npos)return 10;
    if(stage=="PROVIDER_IDENTITY_REFUSED"&&json.find("FILE_LOCK:")==std::string::npos)return 11;
    const auto file=std::filesystem::path(argv[2]).parent_path()/L"NeuRotic-NR-admission.json";
    std::ifstream in(file,std::ios::binary);
    const std::string persisted((std::istreambuf_iterator<char>(in)),{});
    if(persisted!=json)return 12;
    std::cout<<json<<"\nADMISSION_DIAGNOSTIC_PASS\n";
    return 0;
}
