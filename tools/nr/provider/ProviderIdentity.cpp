#include "../../../OptiScaler/dlssnr/CanonicalProviderIdentity.h"
#include <iostream>
// Metadata-only verifier. No LoadLibrary, provider entry, device or queue.
int wmain(int argc,wchar_t** argv) {
    const bool reference=argc==3&&std::wstring(argv[1])==L"--reference-package";
    if(argc!=2&&!reference){std::cerr<<"Usage: ProviderIdentity.exe [--reference-package] <absolute canonical nvngx_dlssnr.dll path>\n";return 2;}
    DlssNr::Canonical::Package package(reference?DlssNr::Canonical::VerificationScope::MatchedReference:
        DlssNr::Canonical::VerificationScope::DirectNr);
    const bool valid=package.Open(argv[reference?2:1]);
    std::cout<<package.Json()<<'\n';
    return valid?0:1;
}
