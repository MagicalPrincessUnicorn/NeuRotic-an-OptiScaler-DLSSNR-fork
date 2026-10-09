#pragma once
#include "NativeFeatureLifetime.h"
#include "NativeSrRetirementReceipt.h"
#include "NrGpuSafety.h"
#include <nr/lifecycle/OpaqueSrEvaluationReceipt.h>
#include <algorithm>
#include <mutex>
#include <vector>

namespace Neurotic::Lifecycle { class NativeProcessBootstrap; }
namespace DlssNr
{
class NativeSelectedSrLifetimeLedger
{
    friend class Neurotic::Lifecycle::NativeProcessBootstrap;
    friend class NativeSrRetirementReceipt;
#ifdef NR_SELECTED_SR_LIFETIME_TESTING
    friend struct NativeSelectedSrLifetimeTestAccess;
#endif
    using Receipt=Neurotic::Lifecycle::OpaqueSrEvaluationReceipt;
    std::shared_ptr<NativeSrRetirementReceipt::Scope> scope_=std::make_shared<NativeSrRetirementReceipt::Scope>();
    mutable std::mutex mutex_;
    bool closed_=false;
    struct Feature {std::shared_ptr<const NativeFeatureLifetime> lifetime;std::uint64_t entries=0;};
    struct Entry
    {
        std::shared_ptr<const NativeFeatureLifetime> feature;
        Receipt::Entry original;
        GpuSafety::Ticket recording;
        std::shared_ptr<const void> capture;
        bool returned=false;
        std::optional<GpuSafety::TerminalRecordingEvidence> terminal;
    };
    std::vector<Feature> features_;
    mutable std::vector<Entry> entries_;
    void PollLocked()const
    {
        for(auto& entry:entries_)if(!entry.terminal)
            entry.terminal=GpuSafety::InspectTerminalRecording(entry.recording);
    }
  public:
    class Registration
    {
        friend class NativeSelectedSrLifetimeLedger;
        std::shared_ptr<const NativeSrRetirementReceipt::Scope> scope_;
        std::size_t index_;
        Registration(std::shared_ptr<const NativeSrRetirementReceipt::Scope> scope,std::size_t index):scope_(std::move(scope)),index_(index){}
    };
    NativeSelectedSrLifetimeLedger()=default;
    NativeSelectedSrLifetimeLedger(const NativeSelectedSrLifetimeLedger&)=delete;
    NativeSelectedSrLifetimeLedger& operator=(const NativeSelectedSrLifetimeLedger&)=delete;
    void MarkUnknown()noexcept{scope_->unknown=true;}
    void CloseAdmission()noexcept{std::lock_guard lock(mutex_);closed_=true;}
    // Capture the recording owner's terminal fact before renderer shutdown may
    // rotate its observation epoch. No caller can provide a completed boolean.
    void Poll()const{std::lock_guard lock(mutex_);PollLocked();}
    bool RetainsTerminal(const Registration& registration)const
    {
        std::lock_guard lock(mutex_);
        if(registration.scope_!=scope_||registration.index_>=entries_.size())return false;
        PollLocked();const auto& entry=entries_[registration.index_];
        return entry.returned&&entry.terminal.has_value()&&entry.capture&&entry.recording;
    }
  private:
    bool TrackFeature(std::shared_ptr<const NativeFeatureLifetime> lifetime)
    {
        std::lock_guard lock(mutex_);
        if(closed_||!lifetime){MarkUnknown();return false;}
        if(std::none_of(features_.begin(),features_.end(),[&](const auto& item){return item.lifetime==lifetime;}))
        {
            if(features_.size()==16){MarkUnknown();return false;}
            features_.push_back({std::move(lifetime),0});
        }
        return true;
    }
    std::optional<Registration> Register(std::shared_ptr<const NativeFeatureLifetime> lifetime,
        const Receipt::Entry& original,GpuSafety::Ticket recording,std::shared_ptr<const void> capture)
    {
        if(!TrackFeature(lifetime))return {};
        std::lock_guard lock(mutex_);
        if(closed_||entries_.size()==64||!capture||!recording||!original.completeCoverage||!original.entrySequence||
           !original.recordingIncarnation||!original.featureHandle||!original.parameters||
           original.featureGeneration!=lifetime->Generation()||!original.output.native||
           original.output.identity.Check()!=Neurotic::Contracts::Error::None||
           !original.output.identity.objectIncarnation.IsKnown()||
           !original.output.identity.resourceIncarnation.IsKnown()||
           !original.output.identity.resourceGeneration.IsKnown()||
           !GpuSafety::MatchesLocalRecording(recording,original.nativeList,original.output.native)||
           std::any_of(entries_.begin(),entries_.end(),[&](const auto& item){return item.original.entrySequence==original.entrySequence;}))
        {MarkUnknown();return {};}
        const auto index=entries_.size();
        entries_.push_back({lifetime,original,std::move(recording),std::move(capture),false,{}});
        for(auto& feature:features_)if(feature.lifetime==lifetime){++feature.entries;break;}
        return Registration(scope_,index);
    }
    bool ObserveReturn(const Registration& registration,const Receipt::Return& observed)
    {
        std::lock_guard lock(mutex_);
        if(registration.scope_!=scope_||registration.index_>=entries_.size()){MarkUnknown();return false;}
        auto& entry=entries_[registration.index_];const auto& original=entry.original;
        if(entry.returned||!observed.completeCoverage||observed.featureHandle!=original.featureHandle||
           observed.featureGeneration!=original.featureGeneration||observed.nativeList!=original.nativeList||
           observed.recordingIncarnation!=original.recordingIncarnation||observed.parameters!=original.parameters||
           observed.output!=original.output||observed.inputs!=original.inputs||
           observed.outputRegion!=original.outputRegion||observed.outputSubresource!=original.outputSubresource||
           !observed.parameterValuesMatch||observed.returnSequence<=original.entrySequence||
           observed.workOrdinal<original.workOrdinal)
        {MarkUnknown();return false;}
        // API failure is still a tracked possible effect. Only release and
        // terminal recording observations below can discharge its retention.
        entry.returned=true;return true;
    }
  public:
    std::optional<NativeSrRetirementReceipt> Retirement()const
    {
        std::lock_guard lock(mutex_);
        if(!closed_||scope_->unknown||features_.empty())return {};
        PollLocked();
        for(const auto& feature:features_)
        {
            const auto state=feature.lifetime->Inspect();
            if(!state.released||!state.admissionClosed||state.activeCallbacks||state.coverageUnknown||
               state.opaqueEntries!=feature.entries)return {};
        }
        for(const auto& entry:entries_)
            if(!entry.returned||!entry.terminal)return {};
        std::vector<std::shared_ptr<const NativeFeatureLifetime>> features;
        for(const auto& feature:features_)features.push_back(feature.lifetime);
        return NativeSrRetirementReceipt(scope_,std::move(features));
    }
    bool ReleaseRetiredCaptures(const NativeSrRetirementReceipt& receipt)
    {
        if(!receipt.Matches(*this)||!Retirement())return false;
        std::vector<std::shared_ptr<const void>> captures;
        std::vector<GpuSafety::Ticket> tickets;
        {
            std::lock_guard lock(mutex_);if(!receipt.Matches(*this))return false;
            captures.reserve(entries_.size());tickets.reserve(entries_.size());
            for(auto& entry:entries_)
            {captures.push_back(std::move(entry.capture));tickets.push_back(std::move(entry.recording));}
        }
        // A last COM/provider reference may reenter; destruction is outside
        // this ledger's mutex. Terminal tombstones stay to prevent reuse.
        return true;
    }
    bool CoversOutput(const NativeSrRetirementReceipt& receipt,ID3D12Resource* output,
        const GpuSafety::Ticket& recording,const Neurotic::Contracts::ResourceIdentityToken& identity)const
    {
        std::lock_guard lock(mutex_);if(!receipt.Matches(*this)||!output||!recording)return false;
        return std::any_of(entries_.begin(),entries_.end(),[&](const auto& entry)
        {
            auto expected=entry.original.output.identity;expected.contentRevision=identity.contentRevision;
            return entry.original.output.native==output&&entry.recording==recording&&expected==identity&&entry.terminal&&entry.returned;
        });
    }
};
inline bool NativeSrRetirementReceipt::Matches(const NativeSelectedSrLifetimeLedger& ledger)const noexcept
{return scope_==ledger.scope_&&scope_&&!scope_->unknown;}
}
