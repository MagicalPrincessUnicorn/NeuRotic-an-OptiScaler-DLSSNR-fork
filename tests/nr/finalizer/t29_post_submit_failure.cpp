#include "../contracts/Test002Support.h"
#if __has_include(<nr/lifecycle/FinalRealFrameReset.h>)
#include "finalizer_fixture.h"
#define HAS_FINALIZER 1
#endif
using namespace NrTest002;
int main(int argc,char** argv)
{
    return Guard([&]{Report r("finalizer-reset",argc==2?std::string_view(argv[1]):std::string_view{});
#ifdef HAS_FINALIZER
        FinalizerTests::Resets(r);
#else
        r.Check("T29.reset-production",false,true,"ProjectFinalizationReset");
#endif
        return r.Finish();});
}
