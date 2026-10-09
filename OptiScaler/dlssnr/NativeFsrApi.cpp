#include "NativeFsr3D3D12.h"
#include <proxies/FfxApi_Proxy.h>
namespace DlssNr::NativeFg {
bool LoadFsrApi(FsrApi& api,std::string& reason){
    if(!FfxApiProxy::InitFfxDx12()||!FfxApiProxy::IsFGReady(false)){reason="Bundled FSR frame-generation provider unavailable";return false;}
    api={FfxApiProxy::D3D12_CreateContext,FfxApiProxy::D3D12_DestroyContext,FfxApiProxy::D3D12_Configure,FfxApiProxy::D3D12_Dispatch};
    return true;
}
}
