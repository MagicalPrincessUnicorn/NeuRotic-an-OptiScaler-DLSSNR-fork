#include "../../../OptiScaler/dlssnr/CanonicalProviderIdentity.h"
#include <iostream>

int wmain(int argc, wchar_t** argv) {
    if (argc != 4) return 2;
    const std::wstring mode=argv[1];
    const bool direct=mode.rfind(L"direct",0)==0;
    DlssNr::Canonical::Package identity(direct?DlssNr::Canonical::VerificationScope::DirectNr:
        DlssNr::Canonical::VerificationScope::MatchedReference);
    const std::filesystem::path provider=argv[2];
    HMODULE fixture=nullptr;
    if(mode==L"mixed" || mode==L"direct-unrelated" || mode==L"direct-conflict" || mode==L"direct-required-handle") {
        fixture=LoadLibraryW(argv[3]);
        if(!fixture)return 3;
    }
    bool ok=identity.Open(provider);
    if(mode==L"direct" || mode==L"direct-unpinned" || mode==L"direct-unrelated" || mode==L"direct-late-unrelated") {
        if(!ok || identity.ProviderObserved())return 20;
        if(mode==L"direct-late-unrelated") {
            fixture=LoadLibraryW(argv[3]);if(!fixture)return 21;
            if(!identity.Observe())return 22;
        }
        const auto evidence=identity.Json();
        if(evidence.find("\"verification_scope\":\"DIRECT_NR\"")==std::string::npos ||
           evidence.find("\"package_verified\":false")==std::string::npos ||
           evidence.find("\"selection_verified\":true")==std::string::npos)return 23;
        if(mode==L"direct-unpinned" &&
           (std::filesystem::file_size(provider)==DlssNr::Canonical::Members[4].bytes ||
            evidence.find("\"members\":[{\"path\":")==std::string::npos))return 37;
        if(mode!=L"direct" && mode!=L"direct-unpinned" &&
           evidence.find("\"required_for_route\":false")==std::string::npos)return 24;
        HANDLE writer=CreateFileW(provider.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,
                                  nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(writer!=INVALID_HANDLE_VALUE){CloseHandle(writer);return 25;}
        if(GetLastError()!=ERROR_SHARING_VIOLATION)return 26;
        HANDLE replacer=CreateFileW(provider.c_str(),DELETE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
                                    nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(replacer!=INVALID_HANDLE_VALUE){CloseHandle(replacer);return 35;}
        if(GetLastError()!=ERROR_SHARING_VIOLATION)return 36;
        if(identity.Open(provider.parent_path()/L"another"/L"nvngx_dlssnr.dll") ||
           identity.Error()!="PACKAGE_SWITCH_REFUSED")return 27;
        if(identity.Open(provider))return 28;
    } else if(mode==L"direct-late-conflict") {
        if(!ok)return 30;
        fixture=LoadLibraryW(argv[3]);if(!fixture)return 31;
        if(identity.Observe() || identity.Error().find("MIXED_MODULE")==std::string::npos)return 32;
        FreeLibrary(fixture);fixture=nullptr;
        if(identity.Observe() || identity.Open(provider))return 33;
    } else if(mode==L"direct-required-handle") {
        if(!ok || identity.Observe(fixture) || identity.Error()!="ACTUAL_PROVIDER_MODULE_MISSING")return 34;
    } else if(mode==L"direct-conflict") {
        if(ok || identity.Error().find("MIXED_MODULE")==std::string::npos)return 29;
    } else if(mode==L"positive") {
        if(!ok || !identity.Observe() || identity.ProviderObserved())return 4;
        HANDLE writer=CreateFileW(provider.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,
                                  nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(writer!=INVALID_HANDLE_VALUE){CloseHandle(writer);return 5;}
        if(GetLastError()!=ERROR_SHARING_VIOLATION)return 15;
        HANDLE replacer=CreateFileW(provider.c_str(),DELETE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
                                    nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(replacer!=INVALID_HANDLE_VALUE){CloseHandle(replacer);return 16;}
        if(GetLastError()!=ERROR_SHARING_VIOLATION)return 17;
        if(identity.Open(provider.parent_path()/L"other.dll"))return 6;
    } else if(mode==L"late-mixed") {
        if(!ok)return 7;
        fixture=LoadLibraryW(argv[3]);if(!fixture)return 8;
        if(identity.Observe() || identity.Error().find("MIXED_MODULE")==std::string::npos)return 9;
        FreeLibrary(fixture);fixture=nullptr;
        if(identity.Observe())return 10; // failure remains latched
    } else if(mode==L"mixed") {
        if(ok || identity.Error().find("MIXED_MODULE")==std::string::npos)return 11;
        const auto evidence=identity.Json();
        if(evidence.find(DlssNr::Canonical::Utf8(std::filesystem::path(argv[3]).filename().wstring()))==std::string::npos || evidence.find("loaded_modules")==std::string::npos)return 12;
    } else if(mode==L"tampered" || mode==L"direct-tampered") {
        if(ok || identity.Error().find("PACKAGE_IDENTITY")==std::string::npos)return 18;
    } else if(mode==L"missing" || mode==L"direct-missing") {
        if(ok)return 13;
    } else return 14;
    if(fixture)FreeLibrary(fixture);
    std::cout<<identity.Json()<<"\nTEST_PASS\n";
    return 0;
}
