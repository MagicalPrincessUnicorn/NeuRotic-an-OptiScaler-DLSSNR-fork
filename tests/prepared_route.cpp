#include <dlssnr/PreparedGuideRoute.h>
#include <cassert>
int main(int argc,char**)
{
    using namespace DlssNr::PreparedGuides;
    const bool selected=argc>1;
    assert(ExclusiveRoute()==selected);
    SetEnvironmentVariableW(L"NEUROTIC_PREPARED_GUIDES",selected?L"0":L"1");
    assert(ExclusiveRoute()==selected); // Selection is immutable after first use.
    for(unsigned route=0;route<5;++route)
        for(unsigned enabled=0;enabled<2;++enabled)
            for(unsigned optIn=0;optIn<2;++optIn)
                assert(CanDispatch(optIn!=0,enabled!=0,route)==
                    (optIn && enabled && (route==1||route==2)));
}
