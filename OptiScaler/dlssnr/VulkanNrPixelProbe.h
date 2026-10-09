#pragma once
#include <vulkan/vulkan.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>

namespace DlssNr::PixelProbe {
constexpr uint32_t GridSide=4, SampleCount=GridSide*GridSide;
using Pixel=std::array<float,4>;
inline uint32_t PixelBytes(VkFormat format) noexcept {
 switch(format){
 case VK_FORMAT_R16G16B16A16_SFLOAT:return 8;
 case VK_FORMAT_R32G32B32A32_SFLOAT:return 16;
 case VK_FORMAT_R8G8B8A8_UNORM:case VK_FORMAT_R8G8B8A8_SRGB:
 case VK_FORMAT_B8G8R8A8_UNORM:case VK_FORMAT_B8G8R8A8_SRGB:return 4;
 default:return 0;}
}
inline float Half(uint16_t bits) noexcept {
 const uint32_t exponent=(bits>>10)&31u,mantissa=bits&1023u;
 float value=exponent==31u ? (mantissa?std::numeric_limits<float>::quiet_NaN():std::numeric_limits<float>::infinity()) :
  exponent ? std::ldexp(1.f+float(mantissa)/1024.f,int(exponent)-15) : std::ldexp(float(mantissa),-24);
 return bits&0x8000u?-value:value;
}
// Storage-domain values: no transfer-function conversion or alpha interpretation.
inline std::optional<Pixel> Decode(VkFormat format,const void* bytes) noexcept {
 if(!bytes||!PixelBytes(format))return {};
 Pixel result{};
 if(format==VK_FORMAT_R32G32B32A32_SFLOAT)std::memcpy(result.data(),bytes,sizeof(result));
 else if(format==VK_FORMAT_R16G16B16A16_SFLOAT){std::array<uint16_t,4> raw{};std::memcpy(raw.data(),bytes,sizeof(raw));for(size_t i=0;i<4;++i)result[i]=Half(raw[i]);}
 else {const auto* raw=static_cast<const unsigned char*>(bytes);for(size_t i=0;i<4;++i)result[i]=float(raw[i])/255.f;
  if(format==VK_FORMAT_B8G8R8A8_UNORM||format==VK_FORMAT_B8G8R8A8_SRGB)std::swap(result[0],result[2]);}
 return result;
}
inline uint32_t Coordinate(uint32_t extent,uint32_t cell) noexcept {
 return extent?std::min(extent-1,static_cast<uint32_t>((uint64_t(2*cell+1)*extent)/(2*GridSide))):0;
}
struct Summary {
 uint32_t finiteRgb=0,finiteAlpha=0;
 float rgbMin=std::numeric_limits<float>::infinity(),rgbMax=-std::numeric_limits<float>::infinity();
 float alphaMin=std::numeric_limits<float>::infinity(),alphaMax=-std::numeric_limits<float>::infinity();
 void Add(const Pixel& p) noexcept {
  for(size_t c=0;c<3;++c)if(std::isfinite(p[c])){++finiteRgb;rgbMin=std::min(rgbMin,p[c]);rgbMax=std::max(rgbMax,p[c]);}
  if(std::isfinite(p[3])){++finiteAlpha;alphaMin=std::min(alphaMin,p[3]);alphaMax=std::max(alphaMax,p[3]);}
 }
};
struct Difference {
 uint32_t finiteChannels=0;double sum=0,maximum=0;
 void Add(const Pixel& a,const Pixel& b) noexcept {for(size_t c=0;c<3;++c)if(std::isfinite(a[c])&&std::isfinite(b[c])){
  const double delta=std::abs(double(a[c])-double(b[c]));sum+=delta;maximum=std::max(maximum,delta);++finiteChannels;}}
 double Mean() const noexcept{return finiteChannels?sum/finiteChannels:std::numeric_limits<double>::quiet_NaN();}
};
// Count admitted attempts before allocation: an allocation failure still backs off.
struct Budget {
 static constexpr uint32_t MaximumAttempts=32;
 uint32_t attempts=0;uint64_t next=0;
 bool Try(uint64_t now,bool pending) noexcept {
  if(pending||attempts>=MaximumAttempts||now<next)return false;
  ++attempts;next=now+1000;return true;
 }
};
}
