#include "public_composition_oracle.h"
#include <cstdio>
int main()
{
    using namespace DlssNr::AdaptivePublicFixture;
    unsigned checks=0,failures=0;
    const auto check=[&](bool pass){++checks;if(!pass)++failures;};
    // Independent high-precision scalar reference, recorded in the JSON
    // evidence; do not construct expected values with the implementation.
    check(std::abs(ExpectedEditedChannel(.125f)-.08315414296f)<1e-6f);
    check(std::abs(ExpectedEditedChannel(.25f)-.12839221412f)<1e-6f);
    for(const auto& sample:CompositionSamples){
        const auto reference=[](float input){return input==.125f?.08315414296f:.12839221412f;};
        std::array<float,4> expected{reference(sample.original[0]),reference(sample.original[1]),reference(sample.original[2]),1};
        check(MatchesCompositionSample(expected,sample));
        check(!MatchesCompositionSample({sample.original[0],sample.original[1],sample.original[2],1},sample));
        check(!MatchesCompositionSample({0,0,0,1},sample));
        check(!MatchesCompositionSample({NAN,expected[1],expected[2],1},sample));
        auto wrong=expected;wrong[0]+=.01f;check(!MatchesCompositionSample(wrong,sample));
    }
    std::printf("RESULT composition-oracle checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
