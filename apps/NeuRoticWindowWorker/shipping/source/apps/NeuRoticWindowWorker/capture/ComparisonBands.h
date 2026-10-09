#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>
#include <utility>
namespace nrw {
// Original-image spans over a full enhanced base; same immutable frame pair.
inline std::vector<std::pair<uint32_t,uint32_t>> ComparisonBands(uint32_t width,float split,int stripes) {
 std::vector<std::pair<uint32_t,uint32_t>> result;
 if(stripes>=2&&stripes<=32) {
  for(int i=0;i<stripes;i+=2){auto l=uint32_t(uint64_t(width)*i/stripes),r=uint32_t(uint64_t(width)*(i+1)/stripes);if(r>l)result.emplace_back(l,r);}
 }else {auto right=uint32_t(width*std::clamp(split,0.f,1.f));if(right)result.emplace_back(0,right);}
 return result;
}
}
