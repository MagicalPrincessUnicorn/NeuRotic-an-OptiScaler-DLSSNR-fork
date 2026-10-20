// Test-only service bootstrap outside the loader lock. No provider, plugin,
// model, updater, capture service or game detection is started here.
extern "C" __declspec(dllexport) int NR_AdaptiveFixtureInitializeRuntime()
{
    static bool initialized=false;
    if(initialized)return 0;
    try{
        if(!dllModule||Neurotic::Runtime::BootstrapUnavailable())return 0;
        NtdllProxy::Init();KernelBaseProxy::Init();Kernel32Proxy::Init();
        if(!KernelBaseProxy::GetProcAddress_())return 0;
        // The fixture skips normal DLL startup, including D3D12 proxy setup.
        // Root creation hooks need its real decoder to observe provider layouts.
        // Retain the already loaded runtime; do not discover/load another one.
        static HMODULE d3d12Module=nullptr;
        if(!d3d12Module&&!GetModuleHandleExW(0,L"d3d12.dll",&d3d12Module))return 0;
        D3d12Proxy::Init(d3d12Module);
        if(D3d12Proxy::Module()!=d3d12Module||
            !D3d12Proxy::D3D12CreateVersionedRootSignatureDeserializer_())return 0;
        auto& config=*Config::Instance();
        config.MainDllPath.set_volatile_value(Util::DllPath().parent_path());
        config.CheckForUpdate.set_volatile_value(false);
        config.LogToFile.set_volatile_value(false);
        config.LogLevel.set_volatile_value(2);
        PrepareLogger();runtimeState=&State::Instance();initialized=true;return 1;
    }catch(...){return 0;}
}
