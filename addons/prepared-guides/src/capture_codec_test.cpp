#include "capture_codec.h"
#include <cassert>
#include <cmath>
int main(){assert(nrpg::Half(0x3c00)==1);assert(nrpg::Half(0xbc00)==-1);assert(nrpg::Half(0)==0);assert(std::isinf(nrpg::Half(0x7c00)));assert(std::isnan(nrpg::Half(0x7e00)));assert(nrpg::Pitch(257,4)==1280);assert(nrpg::Pitch(64,4)==256);
 assert(nrpg::NativeRow(0,3,true)==2);assert(nrpg::NativeRow(2,3,true)==0);assert(nrpg::NativeRow(0,3,false)==0);
 assert(nrpg::Half(1)==std::ldexp(1.f,-24));assert(nrpg::Half(0x0400)==std::ldexp(1.f,-14));
}
