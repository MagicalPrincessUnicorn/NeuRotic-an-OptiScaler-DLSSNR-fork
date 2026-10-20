#include <cstdint>
#include <cstdio>
#include <optional>
#include <stdexcept>
struct Caps
{
    struct Adapter {std::uint32_t architecture=0;} adapters[8]{};
    std::uint32_t gpuCount=0;
};
#ifdef NR_OLD_CAPS_ARCHITECTURE_READER
#include "old_caps_architecture_reader.inc"
#else
#include "../../../OptiScaler/hooks/StreamlineCapsArchitecture.h"
static auto ReadArchitecture(const Caps* caps){return Neurotic::Hooks::HighestUsedArchitecture(caps);}
#endif
static unsigned checks=0;
static void Check(bool ok,const char* text)
{++checks;if(!ok)throw std::runtime_error(text);std::printf("PASS %s\n",text);}
int main()
{
    try
    {
        Caps caps;caps.gpuCount=8;caps.adapters[0].architecture=0x130;caps.adapters[7].architecture=0xffffffffu;
        Check(ReadArchitecture(&caps)==0xffffffffu,"highest active adapter found when all declared entries are active");
        caps.gpuCount=1;
        Check(ReadArchitecture(&caps)==0x130u,"inactive high architecture cannot veto actual active-adapter policy");
        caps.gpuCount=2;caps.adapters[1].architecture=0x170;
        Check(ReadArchitecture(&caps)==0x170u,"maximum comes from the selected active prefix only");
        caps.gpuCount=0;
        Check(ReadArchitecture(&caps)==0u,"no active adapters produce no observed architecture; fallback stays caller policy");
        caps.gpuCount=9;
        Check(!ReadArchitecture(&caps),"invalid declared-array count leaves optional read unavailable without traversal");
        Check(!ReadArchitecture(nullptr),"null publication leaves optional read unavailable");
        Check(caps.gpuCount==9&&caps.adapters[0].architecture==0x130&&caps.adapters[7].architecture==0xffffffffu,
            "architecture observation never clears inactive state or rewrites publication");
        std::printf("CAPS_ARCHITECTURE_USED_COUNT checks=%u PASS authoredCaps=1 privateProvider=0 GPU=0\n",checks);return 0;
    }
    catch(const std::exception& e){std::fprintf(stderr,"CAPS_ARCHITECTURE_USED_COUNT FAIL: %s\n",e.what());return 1;}
}
