#include <windows.h>
#include <tlhelp32.h>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
int wmain(int argc,wchar_t** argv) {
    if(argc!=5)return 2;
    auto relocated=LoadLibraryW(argv[3]);if(!relocated)return 3;
    auto module=LoadLibraryW(argv[1]);if(!module)return 4;
    using Probe=int(__cdecl*)(const wchar_t*);
    using Copy=unsigned long long(__cdecl*)(char*,unsigned long long);
    auto probe=reinterpret_cast<Probe>(GetProcAddress(module,"dlssnr_call_probe_d3d12"));
    auto copy=reinterpret_cast<Copy>(GetProcAddress(module,"dlssnr_call_canonical_identity"));
    if(!probe||!copy)return 5;
    const bool same=std::wstring(argv[4])==L"same";
    const int result=probe(argv[2]);
    if(result!=(same?31:0)){std::cerr<<"probe="<<result<<'\n';return 6;}
    auto size=copy(nullptr,0);if(!size||size>1024*1024)return 7;
    std::vector<char> bytes(static_cast<size_t>(size));copy(bytes.data(),size);
    const std::string json(bytes.data());
    if(json.find(same?"\"refused\":false":"MIXED_MODULE:nvngx_dlssnr.dll")==std::string::npos)return 8;
    if(same) {
        if(json.find("\"provider_loaded\":true")==std::string::npos)return 9;
        // Both the selected file and the relocated matching file remain locked.
        for(int i=2;i<=3;++i){auto writer=CreateFileW(argv[i],GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,
            nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
            if(writer!=INVALID_HANDLE_VALUE){CloseHandle(writer);return 10;}}
        unsigned count=0;auto snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,GetCurrentProcessId());
        MODULEENTRY32W entry{};entry.dwSize=sizeof(entry);
        if(!Module32FirstW(snapshot,&entry))return 11;
        do {if(_wcsicmp(entry.szModule,L"nvngx_dlssnr.dll")==0)++count;}while(Module32NextW(snapshot,&entry));
        CloseHandle(snapshot);if(count!=1)return 12;
        FreeLibrary(relocated);relocated=nullptr; // forwarder retains its own existing loader reference
        if(probe(argv[2])!=31)return 13;
        using Contract=const char*(__cdecl*)();
        auto contract=reinterpret_cast<Contract>(GetProcAddress(module,"dlssnr_call_host_recorded_contract_v1"));
        if(!contract||contract()!=nullptr)return 14;
    }
    std::cout<<json<<"\nRELOCATION_"<<(same?"REUSE":"CONFLICT_REFUSAL")<<"_PASS\n";
    // Fixture symbols are only queried; no provider Init/Create/Evaluate executes.
}
