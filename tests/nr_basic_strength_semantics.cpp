#include "../OptiScaler/dlssnr/DlssNr_BasicMultipass.h"
#include <cassert>
#include <cmath>
struct ConfigFixture { NrOptional<DlssNr::BasicMultipass::Profile> DlssNrBasicMultipass{{}}; };
int main() {
    namespace B = DlssNr::BasicMultipass;
    B::Profile p; p.maximum=5; p.model=2.3f; p.detail=4.7f;
    assert(B::Count(p)==5);
    assert(B::Strength(p.model,0)==1 && B::Strength(p.model,1)==1);
    assert(std::abs(B::Strength(p.model,2)-0.3f)<0.0001f);
    assert(B::Strength(p.model,3)==0);
    assert(std::abs(B::Strength(p.detail,4)-0.7f)<0.0001f);
    ConfigFixture c; c.DlssNrBasicMultipass=p;
    const auto generation=NrConfigSynchronization::ProfileGeneration();
    assert(B::CommitEdit(c,p,generation,&B::Profile::model,1.0f));
    auto reset=c.DlssNrBasicMultipass.value_or_default();
    assert(reset.model==1.0f && reset.detail==4.7f && reset.maximum==5);
    assert(!B::CommitEdit(c,p,generation,&B::Profile::detail,1.0f));
    assert(B::CommitEdit(c,reset,generation,&B::Profile::detail,1.0f));
    reset=c.DlssNrBasicMultipass.value_or_default();
    assert(B::Count(reset)==1);
    reset.model=reset.detail=0; assert(B::Count(reset)==0);
    reset.model=5; reset.detail=0; assert(B::Count(reset)==5);
    reset.model=10; assert(B::Normalize(reset).model==5);
}
