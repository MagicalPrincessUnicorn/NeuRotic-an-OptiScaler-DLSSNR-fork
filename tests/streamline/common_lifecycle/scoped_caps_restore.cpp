// Public field-restoration control. This fixture does not claim provider ABI or
// lifetime qualification; the real SDK-managed callback supplies that interval.
#include <array>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <utility>
namespace VendorId {enum Value : std::uint32_t {Nvidia=0x10de,Amd=0x1002,Intel=0x8086};}
struct CapsAdapter
{
    VendorId::Value vendor=VendorId::Nvidia;
    std::uint32_t architecture=0,deviceId=0;
    void* nativeInterface=nullptr;
};
struct Caps
{
    std::uint32_t gpuCount=2,driverVersionMajor=555,driverVersionMinor=7;
    CapsAdapter adapters[8]{};
    bool hwsSupported=false,laptopDevice=true;
};
#ifdef NR_LEGACY_CAPS_RESTORE_CONTROL
#include "legacy_caps_set_arch.inc"
#else
#include "../../../OptiScaler/hooks/StreamlineScopedCaps.h"
#endif
static unsigned checks=0;
static void Check(bool value,const char* message)
{++checks;if(!value)throw std::runtime_error(message);std::printf("PASS %s\n",message);}
static Caps Mixed()
{
    Caps c;c.adapters[0]={VendorId::Nvidia,0x170,17,reinterpret_cast<void*>(std::uintptr_t(1))};
    c.adapters[1]={VendorId::Amd,0,29,reinterpret_cast<void*>(std::uintptr_t(2))};return c;
}
template<class F>static decltype(auto) WithSpoof(Caps* caps,std::uint32_t arch,bool driver,F&& original)
{
#ifdef NR_LEGACY_CAPS_RESTORE_CONTROL
    std::uint32_t highest=0;
    if(caps)for(const auto& adapter:caps->adapters)if(adapter.architecture>highest)highest=adapter.architecture;
    LegacySetArch(caps,arch,driver);
    const auto result=std::forward<F>(original)();
    LegacySetArch(caps,highest,driver);
    return result;
#else
    Neurotic::Hooks::StreamlineScopedCaps<Caps> scope(caps,arch,VendorId::Nvidia,driver);
    return std::forward<F>(original)();
#endif
}
static void Restored(const Caps& c,const Caps& before)
{
    Check(c.adapters[0].architecture==before.adapters[0].architecture&&c.adapters[1].architecture==before.adapters[1].architecture,
          "each mixed adapter architecture restored exactly, not aggregate maximum");
    Check(c.adapters[0].vendor==before.adapters[0].vendor&&c.adapters[1].vendor==before.adapters[1].vendor,
          "each original vendor restored exactly");
    Check(c.driverVersionMajor==before.driverVersionMajor&&c.hwsSupported==before.hwsSupported,
          "actual prior driver major and HAGS values restored");
    Check(c.adapters[0].nativeInterface==before.adapters[0].nativeInterface&&c.adapters[1].nativeInterface==before.adapters[1].nativeInterface&&
          c.adapters[0].deviceId==before.adapters[0].deviceId&&c.adapters[1].deviceId==before.adapters[1].deviceId&&c.laptopDevice,
          "borrowed native-interface pointers and untouched adapter fields unchanged");
}
int main()
{
    try
    {
        Caps c=Mixed();const Caps before=c;Caps* published=&c;Caps* privateCommon=&c;
        std::uint32_t supportedAdapters=0;bool needNGX=false,needDX11On12=false,needDRS=false;
        auto realHelper=[&]{
            // Source-derived alias/side-effect witness: Common reads its OWN
            // object, not a replacement pointer from the parameter map.
            for(std::uint32_t i=0;i<privateCommon->gpuCount;++i)
                if(privateCommon->adapters[i].architecture==0xffffffffu&&privateCommon->adapters[i].vendor==VendorId::Nvidia)
                    supportedAdapters|=1u<<i;
            needNGX=needDX11On12=needDRS=supportedAdapters!=0;
        };
        const auto result=WithSpoof(published,0xffffffffu,false,[&]{
            Check(published==privateCommon&&published->adapters[0].architecture==0xffffffffu&&published->adapters[1].architecture==0xffffffffu,
                  "feature and actual Common helper read the SAME scoped object");
            realHelper();c.driverVersionMinor=9;return true;
        });
        Check(result,"original true result preserved");Restored(c,before);
        Check(supportedAdapters==3&&needNGX&&needDX11On12&&needDRS&&c.driverVersionMinor==9,
              "original helper side effects and unrelated original field updates survive restoration");
        Check(!WithSpoof(&c,0xffffffffu,true,[]{return false;}),"original false result preserved");Restored(c,before);
        bool threw=false;
        try{WithSpoof(&c,0xffffffffu,true,[]()->bool{throw std::runtime_error("original exception");});}
        catch(const std::runtime_error&){threw=true;}
        Check(threw,"original C++ exception propagates");Restored(c,before);
        WithSpoof(&c,0x300,true,[&]{
            const Caps outer=c;
            WithSpoof(&c,0xffffffffu,false,[]{return true;});
            Restored(c,outer);return true;
        });Restored(c,before);
        Caps replacement=Mixed();replacement.adapters[0].architecture=0x444;
        const auto replacementBefore=replacement;published=&replacement;
        WithSpoof(published,0xffffffffu,true,[]{return true;});Restored(replacement,replacementBefore);Restored(c,before);
        Check(WithSpoof(nullptr,0xffffffffu,true,[]{return true;}),"missing caps leaves original operation callable");
#ifndef NR_LEGACY_CAPS_RESTORE_CONTROL
        Caps invalid=Mixed();invalid.gpuCount=9;const Caps invalidBefore=invalid;
        {
            Neurotic::Hooks::StreamlineScopedCaps<Caps> guard(&invalid,0xffffffffu,VendorId::Nvidia,true);
            Check(!guard.Applied(),"invalid count beyond declared API array withholds ONLY scoped write");
        }
        Restored(invalid,invalidBefore);
#endif
        std::printf("SCOPED_CAPS checks=%u PASS privateProvider=0 GPU=0 lifetimeFromManagedCallbackContract=1\n",checks);return 0;
    }
    catch(const std::exception& error){std::fprintf(stderr,"SCOPED_CAPS FAIL: %s\n",error.what());return 1;}
}
