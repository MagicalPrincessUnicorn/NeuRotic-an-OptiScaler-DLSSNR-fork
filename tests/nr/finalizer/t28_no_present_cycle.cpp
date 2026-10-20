#include "../contracts/Test002Support.h"
#if __has_include(<nr/lifecycle/FinalRealFrameFinalizer.h>)
#include "finalizer_fixture.h"
#define HAS_FINALIZER 1
#endif
using namespace NrTest002;
int main(int argc,char** argv)
{
    return Guard([&]{Report r("finalizer-order",argc==2?std::string_view(argv[1]):std::string_view{});
#ifdef HAS_FINALIZER
        FinalizerTests::Ordering(r);
#else
        r.Check("T28.finalizer-production",false,true,"FinalRealFrameFinalizer");
#endif
        return r.Finish();});
}
