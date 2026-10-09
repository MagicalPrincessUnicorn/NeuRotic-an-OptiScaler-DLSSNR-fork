#pragma once
#include "NativeProviderLifetime.h"
#include "NativeModelCreation.h"
#include "NativeRendererShutdownObservation.h"
#include <array>
#include <atomic>
#include <memory>
namespace DlssNr
{
bool Shutdown();
// Issued solely by the live renderer after its existing recording/provider
// gates, checked feature/model release and internal parameter destruction.
class NativeRendererRetirement
{
    friend bool Shutdown();
#ifdef NR_SPECTRE_SOURCE_TESTING
    friend class NativeRendererRetirementTestAccess;
#endif
    std::array<std::shared_ptr<NativeProviderRetirementOwner>,129> owners_;
    std::size_t count_=0;
    NativeRendererShutdownObservationV1 observation_;
    mutable std::atomic<bool> nextCreationIssued_{false};
    NativeRendererRetirement()=default;
    bool Add(const std::shared_ptr<NativeProviderRetirementOwner>& owner)
    {
        if(!owner)return true;
        for(std::size_t i=0;i<count_;++i)if(owners_[i]==owner)return true;
        if(count_==owners_.size())return false;owners_[count_++]=owner;return true;
    }
  public:
    bool Covers(const std::shared_ptr<NativeProviderRetirementOwner>& owner)const noexcept
    {
        if(!owner||observation_.succeeded!=1)return false;
        for(std::size_t i=0;i<count_;++i)if(owners_[i]==owner)return true;
        return false;
    }
    auto Observe()const noexcept{return observation_;}
    // A new epoch gets a different owner. The retired owner's sticky attempt
    // and revocation are never reset, including after failed opaque calls.
    std::unique_ptr<NativeModelCreation> NextModelCreation(
        const std::shared_ptr<NativeProviderRetirementOwner>& prior)const noexcept
    {
        if(observation_.succeeded!=1 || (prior ? !Covers(prior) : count_!=0))return {};
        try
        {
            auto next=std::make_unique<NativeModelCreation>();
            bool expected=false;
            if(!nextCreationIssued_.compare_exchange_strong(expected,true))return {};
            return next;
        }
        catch(...){return {};}
    }
};
std::shared_ptr<const NativeRendererRetirement> ObserveNativeRendererRetirement()noexcept;
NativeRendererShutdownObservationV1 ObserveNativeRendererShutdown()noexcept;
}
