#pragma once
#include <cstdint>
#include <cmath>
#include <bit>
namespace nrpg {
inline std::uint32_t NativeRow(std::uint32_t row,std::uint32_t height,bool bottomLeft){return bottomLeft?height-1-row:row;}
inline std::uint32_t Pitch(std::uint32_t width,std::uint32_t bytes){return (width*bytes+255)&~255u;}
inline float Half(std::uint16_t value){
    const auto sign=std::uint32_t(value&0x8000)<<16;auto exponent=(value>>10)&31u;auto fraction=value&1023u;
    if(exponent==31)return std::bit_cast<float>(sign|0x7f800000u|(fraction<<13));
    float out=exponent?std::ldexp(float(1024+fraction),int(exponent)-25):std::ldexp(float(fraction),-24);
    return sign?-out:out;
}
}
