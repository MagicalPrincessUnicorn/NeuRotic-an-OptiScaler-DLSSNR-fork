#pragma once
#include <inputs/universal_feeder/PreparedGuideContract.h>
#include <cstddef>
#include <cstdint>
#include <type_traits>
namespace nrpg::external {
inline constexpr std::uint32_t Version=1;
inline constexpr std::uint64_t FeederProducer=0x4e52464545440001ull;
// Same-process, same-bitness, ReShade API 20 only. No STL, COM smart pointers,
// handles to another process, or inferred ownership crosses this boundary.
// The producer retains its runtime, commands and source textures for the entire
// synchronous call. The receiver must finish source copyback before Delivered;
// unsafe work pins its own leases and never grants producer reuse by timeout.
#pragma pack(push,8)
struct Frame {
 std::uint32_t size=sizeof(Frame),version=Version,producerVersion=1,reshadeApi=20;
 Neurotic::Feed::Prepared::Descriptor descriptor;
 std::uint64_t runtime=0,commands=0,outputView=0;
 std::uint64_t resources[4]{}; // color, raw depth, normalized pixel motion, distrust
 std::uint32_t deriveMotion=0,reserved=0;
};
enum class Result:std::uint32_t {Refused=0,Delivered=1,Preparing=2,Unsafe=3};
struct Receipt {
 std::uint32_t size=sizeof(Receipt),version=Version;
 Result result=Result::Refused;std::uint32_t reserved=0;
 std::uint64_t capture=0,generation=0;
 char reason[192]{};
};
#pragma pack(pop)
using EvaluateFn=unsigned(__cdecl*)(const Frame*,Receipt*);
static_assert(sizeof(Frame)==328 && offsetof(Frame,descriptor)==16);
static_assert(sizeof(Receipt)==224 && std::is_trivially_copyable_v<Frame>);
inline bool HeaderValid(const Frame& f) noexcept {
 return f.size==sizeof(Frame)&&f.version==Version&&f.producerVersion==1&&f.reshadeApi==20&&
  !f.reserved&&f.deriveMotion<=1&&f.runtime&&f.commands&&f.outputView&&f.resources[0]&&f.resources[1]&&
  (f.deriveMotion||(f.resources[2]&&f.resources[3]))&&f.descriptor.producer==FeederProducer&&
  f.descriptor.size==sizeof(f.descriptor)&&f.descriptor.version==1;
}
}
