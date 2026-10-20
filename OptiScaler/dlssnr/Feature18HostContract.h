#pragma once
#include <cstdint>
#include <string_view>
#include <utility>
#define NR_FEATURE18_HOST_CONTRACT_V1 1
namespace DlssNr {
// Provider-independent NeuRotic host policy, selected by Josh for BYO DLLs.
// The host tracks real caller recordings and checked feature release. This is
// not a vendor retention promise or a qualification of arbitrary DLL internals.
// Preserve the v1 export ABI; the versioned identity distinguishes host policy.
inline constexpr char Feature18HostContractIdentity[] =
    "HostRecordedFeature18InvocationTerminal/2;NeuRotic.GuidesV4;SelectedDll";

enum class FeatureReleaseDisposition : std::uint32_t { Unavailable, Returned, Exceptional };
struct FeatureReleaseResult {
    std::uint32_t size=sizeof(FeatureReleaseResult);
    FeatureReleaseDisposition disposition=FeatureReleaseDisposition::Unavailable;
    std::int32_t nativeResult=0;
    std::uint32_t exceptionCode=0;
};
// Substate of an existing feature owner, transferred with its handle on retire.
// Once provider entry is possible, no outcome permits a second release attempt.
class FeatureReleaseAttempt {
    bool attempted_=false;
    FeatureReleaseResult result_;
  public:
    bool Succeeded()const {return attempted_&&result_.size==sizeof(result_)&&
        result_.disposition==FeatureReleaseDisposition::Returned&&result_.nativeResult==1;}
    const FeatureReleaseResult& Result()const{return result_;}
    template<class Call> bool Try(bool drained,Call&& call) noexcept {
        if(attempted_)return Succeeded();
        if(!drained)return false;
        attempted_=true;
        result_.disposition=FeatureReleaseDisposition::Exceptional;
        try {result_=std::forward<Call>(call)();}catch(...){}
        return Succeeded();
    }
};
}
