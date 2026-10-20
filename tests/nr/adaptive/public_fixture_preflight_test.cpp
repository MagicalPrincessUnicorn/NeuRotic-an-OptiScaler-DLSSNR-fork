#include "public_fixture_binary_preflight.h"
#include <cstdio>
#include <cwchar>
using namespace DlssNr::AdaptivePublicFixture;
int wmain(int argc,wchar_t** argv)
{
    if(argc<3){std::fputs("reject-ordinary binary.dll | accept-fixture binary.dll receipt.txt manifest-sha256 product|provider\n",stderr);return 2;}
    const bool reject=std::wcscmp(argv[1],L"reject-ordinary")==0;
    if((reject&&argc!=3)||(!reject&&(std::wcscmp(argv[1],L"accept-fixture")||argc!=6)))return 2;
    if(GetModuleHandleW(argv[2])){std::fputs("FAIL target already executable-loaded\n",stderr);return 2;}
    try{
        BinaryPreflight binary(argv[2]);
        const bool provider=!reject&&std::wcscmp(argv[5],L"provider")==0;
        if(!reject&&!provider&&std::wcscmp(argv[5],L"product")!=0)return 2;
        binary.RequireExports(provider?std::span<const char* const>(ProviderFixtureExports):std::span<const char* const>(ProductFixtureExports));
        if(reject){std::fputs("FAIL ordinary artifact has test-product exports\n",stderr);return 1;}
        std::string manifest;for(const wchar_t* p=argv[4];*p;++p){if(*p>127)return 2;manifest+=char(*p);}
        binary.RequireProvenance(argv[3],provider?"provider":"product",manifest);
        if(GetModuleHandleW(binary.Path()))return 1;
        std::puts("RESULT fixture data-only preflight accepted; executableLoads=0");return 0;
    }catch(const std::exception& error){
        if(GetModuleHandleW(argv[2]))return 1;
        const bool expected=reject&&std::strcmp(error.what(),"required fixture export absent; executable loading refused")==0;
        std::printf("%s data-only preflight: %s; executableLoads=0\n",expected?"PASS ordinary refused":"FAIL preflight",error.what());
        return expected?0:1;
    }
}
