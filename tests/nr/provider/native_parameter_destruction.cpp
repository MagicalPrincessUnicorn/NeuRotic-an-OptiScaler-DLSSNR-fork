#include <iostream>
#include <stdexcept>
#if __has_include(<dlssnr/NativeParameterDestruction.h>)
#include <dlssnr/NativeParameterDestruction.h>
int main()
{
    unsigned checks=0,failed=0;auto check=[&](bool value,const char* message){++checks;if(!value){++failed;std::cerr<<message<<'\n';}};
    for(unsigned scenario=0;scenario<3;++scenario)
    {
        int storage=0;int* parameters=&storage;unsigned calls=0;DlssNr::NativeParameterDestruction owner;
        auto destroy=[&](int* exact)->std::uint32_t{++calls;check(exact==&storage,"exact parameter identity");
            if(scenario==0)throw std::runtime_error("destroy uncertain");return scenario==1?0xbad00001u:1u;};
        check(owner.Destroy(parameters,destroy)==(scenario==2),"only native success completes destruction");
        auto result=owner.Observe();check(result.attempted==1&&result.parameters==reinterpret_cast<std::uintptr_t>(&storage),"observed exact attempted map");
        check(result.disposition==(scenario==0?2u:1u),"exception and returned outcome distinct");
        check(result.nativeResult==(scenario==0?0u:scenario==1?0xbad00001u:1u),"exact native result retained");
        check((parameters==nullptr)==(scenario==2),"uncertain pointer retained; successful pointer cleared");
        check(owner.Destroy(parameters,destroy)==(scenario==2)&&calls==1,"no second destruction after any actual attempt");
    }
    DlssNr::NativeParameterDestruction empty;int* absent=nullptr;unsigned calls=0;
    check(empty.Destroy(absent,[&](int*){++calls;return 1u;})&&calls==0&&!empty.Observe().attempted,"absent map has no invented destruction call");
    std::cout<<"Native internal parameter destruction: "<<checks<<" checks, "<<failed<<" failures; CPU only\n";
    return failed?1:0;
}
#else
int main(){std::cerr<<"checked internal parameter owner absent\n";return 1;}
#endif
