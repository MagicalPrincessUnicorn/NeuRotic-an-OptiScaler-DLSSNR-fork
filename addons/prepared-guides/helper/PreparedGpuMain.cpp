#include "PreparedGpuHost.h"
#include <dlssnr/NativeGuideProcessor.h>
#include <windows.h>
#include <filesystem>
#include <iostream>

namespace {
struct Renderer {
    struct Module {HMODULE value=nullptr;~Module(){if(value)FreeLibrary(value);}} module;
    DlssNr::NativeGuides::Processor processor;
    bool unsafe=false;
    bool claimed=false;
    using Claim=unsigned(__cdecl*)(std::uint32_t,std::uint32_t,std::uint64_t);
    using Retire=unsigned(__cdecl*)(std::uint32_t,std::uint64_t,std::uint32_t);
    Claim claim=nullptr;Retire retire=nullptr;
};
nrpg::gpu::Consumer MakeConsumer(const nrpg::gpu::PeerPolicy& peer,const nrpg::gpu::ManagedHostSpec& spec,std::string& reason){
    using namespace nrpg::gpu;
    if(spec.buildIdentity!=BuildIdentity){reason="Managed GPU helper source/version stamp mismatch";return {};}
    wchar_t own[MAX_PATH]{};
    if(!GetModuleFileNameW(nullptr,own,MAX_PATH)){reason="Helper executable location unavailable";return {};}
    std::error_code ec;
    const auto directory=std::filesystem::weakly_canonical(std::filesystem::path(own).parent_path(),ec);
    if(ec){reason="Managed helper directory unavailable";return {};}
    const auto core=std::filesystem::canonical(spec.coreModule,ec);
    // The control peer cannot turn GPU registration into an arbitrary DLL loader.
    // Setup installs this exact module beside the helper; symlink escapes fail.
    if(ec||core.parent_path()!=directory||core.filename()!=L"OptiScaler.dll"){
        reason="Direct NR requires the managed sibling OptiScaler.dll";return {};}
    auto renderer=std::make_shared<Renderer>();
    renderer->module.value=LoadLibraryExW(core.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if(!renderer->module.value||!GetProcAddress(renderer->module.value,"NeuRotic_PrepareGuidesV1")||
       !GetProcAddress(renderer->module.value,"NeuRotic_RecordGuidesV1")||!GetProcAddress(renderer->module.value,"NeuRotic_SealGuidesV1")){
       reason="Managed direct NR module lacks prepared ABI v1";return {};}
    renderer->claim=reinterpret_cast<Renderer::Claim>(GetProcAddress(renderer->module.value,"NeuRotic_ClaimPreparedConnectionV1"));
    renderer->retire=reinterpret_cast<Renderer::Retire>(GetProcAddress(renderer->module.value,"NeuRotic_RetirePreparedConnectionV1"));
    if(!renderer->claim||!renderer->retire){reason="Managed direct NR module lacks source-owner policy ABI";return {};}
    Consumer result;
    result.capabilities=PreparedExports;
    result.process=[renderer,luid=peer.adapterLuid](const P::Descriptor& descriptor,const nrpg::CpuMainlineClient::GpuInputs& inputs,
        bool derive,nrpg::CpuMainlineClient::GpuOutput& output,std::string& detail){
        constexpr std::uint64_t ownerToken=0x4e524832484f5354ULL;
        if(!renderer->claimed){if(!renderer->claim(3,1,ownerToken)){detail="Managed helper source/transport policy has not admitted direct NR";return false;}renderer->claimed=true;}
        DlssNr::NativeGuides::TextureCapture capture{descriptor,luid,inputs,derive};
        const auto outcome=renderer->processor.ProcessTextures(capture,output,detail);
        renderer->unsafe|=outcome==DlssNr::NativeGuides::Outcome::Unsafe;
        if(renderer->unsafe)renderer->retire(3,ownerToken,0);
        return outcome==DlssNr::NativeGuides::Outcome::Delivered;
    };
    result.requiresRestart=[renderer]{return renderer->unsafe;};
    return result;
}
}
int wmain(int argc,wchar_t** argv){
    if(argc!=3||std::wstring_view(argv[1])!=L"--gpu-bootstrap")return 2;
    wchar_t* end=nullptr;const auto raw=wcstoull(argv[2],&end,10);
    if(!raw||!end||*end||raw>UINTPTR_MAX)return 2;
    return nrpg::gpu::RunGpuHostBootstrap(reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(raw)),MakeConsumer);
}
