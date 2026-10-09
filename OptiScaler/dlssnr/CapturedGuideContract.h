#pragma once
#include <cstdint>
namespace DlssNr {
// Capture-owner identity, never an authenticated engine/provider frame token.
struct CapturedGuideContract {
 std::uint64_t device=0,deviceGeneration=0,swapchain=0,swapchainGeneration=0,acquisition=0,image=0;
 std::uint64_t stream=0,capture=0,previous=0;
 unsigned width=0,height=0;bool completed=false;
 bool Matches(std::uint64_t d,std::uint64_t dg,std::uint64_t s,std::uint64_t sg,
              std::uint64_t a,std::uint64_t i,unsigned w,unsigned h)const noexcept {
  return completed&&device&&deviceGeneration&&swapchain&&swapchainGeneration&&acquisition&&image&&stream&&capture&&
   previous<capture&&width&&height&&device==d&&deviceGeneration==dg&&swapchain==s&&swapchainGeneration==sg&&
   acquisition==a&&image==i&&width==w&&height==h;
 }
};
}
