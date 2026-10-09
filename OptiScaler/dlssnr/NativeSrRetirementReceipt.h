#pragma once
#include "NativeFeatureLifetime.h"
#include <memory>
#include <vector>
#include <algorithm>
namespace DlssNr
{
class NativeSelectedSrLifetimeLedger;
// SR-only terminal authority. Other renderer, SDK and algorithm registrations
// are deliberately absent and must be discharged by their respective owners.
class NativeSrRetirementReceipt
{
    friend class NativeSelectedSrLifetimeLedger;
    template<class Feature> friend class NativeFeatureRegistry;
    struct Scope {std::atomic<bool> unknown{false};};
    std::shared_ptr<const Scope> scope_;
    std::vector<std::shared_ptr<const NativeFeatureLifetime>> features_;
    NativeSrRetirementReceipt(std::shared_ptr<const Scope> scope,
        std::vector<std::shared_ptr<const NativeFeatureLifetime>> features)
        :scope_(std::move(scope)),features_(std::move(features)){}
    bool Covers(const std::shared_ptr<const NativeFeatureLifetime>& feature)const noexcept
    {return scope_&&!scope_->unknown&&std::find(features_.begin(),features_.end(),feature)!=features_.end();}
  public:
    bool Matches(const NativeSelectedSrLifetimeLedger&)const noexcept;
};
}
