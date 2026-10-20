#include "../addons/prepared-guides/helper/SourceGuidedColor.h"
#include <cassert>
#include <limits>
int main(){
    std::vector<std::uint8_t> source(16*4),output(16*4);
    for(size_t i=0;i<16;++i){source[i*4]=40;source[i*4+1]=80;source[i*4+2]=120;source[i*4+3]=255;}
    output=source;auto identical=output;
    assert(nrpg::StabilizeColor(source,output,4,4,1.f) && output==identical);
    for(size_t i=0;i<16;++i){output[i*4]=70;output[i*4+1]=110;output[i*4+2]=150;}
    auto untouched=output;assert(nrpg::StabilizeColor(source,output,4,4,0.f) && output==untouched);
    assert(nrpg::StabilizeColor(source,output,4,4,1.f) && output==source);
    assert(!nrpg::StabilizeColor(source,output,5,4,1.f));
    assert(!nrpg::StabilizeColor(source,output,4,4,std::numeric_limits<float>::quiet_NaN()));
    output=untouched;std::vector<float> distrust(16,1.f);
    assert(nrpg::StabilizeColor(source,output,4,4,1.f,distrust) && output==source);
}
