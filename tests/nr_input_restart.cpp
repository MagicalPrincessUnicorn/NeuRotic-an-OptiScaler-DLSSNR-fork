#include "../OptiScaler/dlssnr/NrInputRestart.h"
#include <cassert>
int main() {
    using DlssNr::InputSelection;
    constexpr InputSelection selected{0,0,true};
    static_assert(!DlssNr::InputRestartRequired(selected,selected));
    static_assert(DlssNr::InputRestartRequired({2,0,true},selected));
    static_assert(DlssNr::InputRestartRequired({0,1,true},selected));
    static_assert(!DlssNr::InputRestartRequired({0,0,false},selected));
    InputSelection requested=selected;
    requested.source=3; assert(DlssNr::InputRestartRequired(requested,selected));
    requested=selected; assert(!DlssNr::InputRestartRequired(requested,selected));
    requested.transport=2; requested.allowCpuFallback=false;
    assert(DlssNr::InputRestartRequired(requested,selected));
    requested.transport=0; assert(!DlssNr::InputRestartRequired(requested,selected));
    requested.allowCpuFallback=true; assert(!DlssNr::InputRestartRequired(requested,selected));
}
