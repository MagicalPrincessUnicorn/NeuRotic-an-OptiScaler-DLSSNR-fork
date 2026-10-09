#pragma once
#include <nr/contracts/InterceptedSourceTransaction.h>
#include <nr/contracts/C03_Consumption.h>
#include <dlssnr/StreamlineSourceScope.h>
#include <mutex>
#include <functional>
#include "SourceTransactionExclusion.h"
#define NR_NATIVE_SELECTED_OUTPUT_READ_V1 1
#define NR_NATIVE_FINAL_OUTPUT_REFRESH_V1 1

struct ID3D12Resource;

namespace DlssNr { class NativeDx12Source; }
namespace Neurotic::Lifecycle
{
// Observation transport only. No admission, C03 capability, primary claim or
// GPU/provider lifetime ownership is implemented by this retained CPU record.
enum class SourceAssociationReason {Missing,Pending,Failed,Abandoned,OutputUnavailable,
    EnclosingPending,EnclosingFailed,MissingConsumerRights,Ambiguous,AlreadyObserved,Capacity};
inline const char* SourceAssociationReasonName(SourceAssociationReason reason)noexcept
{
    switch(reason)
    {
    case SourceAssociationReason::Missing:return "MissingAssociation";
    case SourceAssociationReason::Pending:return "SelectedSrPending";
    case SourceAssociationReason::Failed:return "SelectedSrFailed";
    case SourceAssociationReason::Abandoned:return "SelectedSrAbandoned";
    case SourceAssociationReason::OutputUnavailable:return "SelectedOutputWriteUnavailable";
    case SourceAssociationReason::EnclosingPending:return "EnclosingStreamlinePending";
    case SourceAssociationReason::EnclosingFailed:return "EnclosingStreamlineFailedOrAbandoned";
    case SourceAssociationReason::MissingConsumerRights:return "FsrResourceLineageAndC03Unavailable";
    case SourceAssociationReason::Ambiguous:return "AmbiguousLiveCallbackKey";
    case SourceAssociationReason::AlreadyObserved:return "CallbackAlreadyObserved";
    case SourceAssociationReason::Capacity:return "SourceAssociationCapacity";
    }
    return "Unknown";
}
class NativeSourceTransactionObservation
{
    friend class NativeProcessBootstrap;
    friend class DlssNr::NativeDx12Source;
#ifdef NR_SOURCE_TRANSACTION_TESTING
    friend struct SourceTransactionTestAccess;
#endif
  public:
    struct SelectedWrite
    {
        Contracts::ResourceView view;
        std::uint64_t recordingIncarnation=0,workOrdinal=0;
    };
    // A pin of the existing Resource publication. This supplies currentness
    // and exact content identity only, never C03 or provider-use permission.
    class SelectedOutputRead
    {
      public:
        virtual ~SelectedOutputRead()=default;
        virtual bool Current()const noexcept=0;
        virtual ID3D12Resource* Native()const noexcept=0;
        virtual const Contracts::ResourceView& View()const noexcept=0;
    };
  private:
    const Contracts::InterceptedSourceTransactionV1 subject_;
    const std::shared_ptr<const DlssNr::StreamlineSourceScope::Observation> enclosing_;
    mutable std::mutex mutex_;
    enum class Return {Pending,Succeeded,Failed,Abandoned};
    Return returned_=Return::Pending;
    bool excluded_=false;
    std::optional<SelectedWrite> selected_;
    using ReadFactory=std::function<std::unique_ptr<SelectedOutputRead>(ID3D12Resource*)>;
    ReadFactory selectedRead_;
    NativeSourceTransactionObservation(Contracts::InterceptedSourceTransactionV1 subject,
        std::shared_ptr<const DlssNr::StreamlineSourceScope::Observation> enclosing)
        :subject_(std::move(subject)),enclosing_(std::move(enclosing)){}
    void Returned(bool succeeded)noexcept
    {std::lock_guard lock(mutex_);if(returned_==Return::Pending)returned_=succeeded?Return::Succeeded:Return::Failed;}
    void Abandon()noexcept
    {std::lock_guard lock(mutex_);if(returned_==Return::Pending)returned_=Return::Abandoned;}
    void SelectedOutput(const Contracts::ResourceView& view,std::uint64_t recording,std::uint64_t ordinal,
        ReadFactory read={})
    {
        std::lock_guard lock(mutex_);
        if(returned_!=Return::Pending||selected_||!recording||!ordinal)return;
        selected_=SelectedWrite{view,recording,ordinal};
        selectedRead_=std::move(read);
    }
    // The real Native After owner advances this only after its authenticated
    // output pass has committed a later revision on the same caller recording.
    bool AdvanceSelectedOutput(const Contracts::ResourceView& view,std::uint64_t recording,
        std::uint64_t ordinal,ReadFactory read)
    {
        std::lock_guard lock(mutex_);
        if(returned_!=Return::Pending||!selected_||!read||
           recording!=selected_->recordingIncarnation||ordinal<=selected_->workOrdinal||
           !view.identity.contentRevision.IsKnown()||
           view.identity.contentRevision==selected_->view.identity.contentRevision)return false;
        auto structure=view.identity;structure.contentRevision=selected_->view.identity.contentRevision;
        if(structure!=selected_->view.identity)return false;
        selected_=SelectedWrite{view,recording,ordinal};selectedRead_=std::move(read);return true;
    }
  public:
    const Contracts::InterceptedSourceTransactionV1& Subject()const noexcept{return subject_;}
    SourceTransactionExclusion ExcludeTransaction()
    {
        std::lock_guard lock(mutex_);excluded_=true;
        return SourceTransactionExclusion(subject_);
    }
    std::optional<SelectedWrite> SelectedOutput()const
    {std::lock_guard lock(mutex_);return selected_;}
    std::unique_ptr<SelectedOutputRead> AcquireSelectedOutputRead(ID3D12Resource* exact)const noexcept
    try
    {
        ReadFactory acquire;
        {
            std::lock_guard lock(mutex_);
            if(excluded_||returned_!=Return::Succeeded||!selected_||!selectedRead_)return {};
            if(enclosing_&&enclosing_->Status()!=DlssNr::StreamlineSourceScope::ReturnStatus::Succeeded)return {};
            acquire=selectedRead_;
        }
        // Resource acquisition can retain COM objects. Never enter it while
        // holding the observation mutex or fabricate a guard from metadata.
        auto read=acquire(exact);
        return read&&read->Current()?std::move(read):nullptr;
    }
    catch(...){return {};}
    SourceAssociationReason Status()const noexcept
    {
        std::lock_guard lock(mutex_);
        if(returned_==Return::Failed)return SourceAssociationReason::Failed;
        if(returned_==Return::Abandoned)return SourceAssociationReason::Abandoned;
        if(returned_==Return::Pending)return SourceAssociationReason::Pending;
        if(enclosing_)
        {
            using R=DlssNr::StreamlineSourceScope::ReturnStatus;
            if(enclosing_->Status()==R::Pending)return SourceAssociationReason::EnclosingPending;
            if(enclosing_->Status()!=R::Succeeded)return SourceAssociationReason::EnclosingFailed;
        }
        if(!selected_)return SourceAssociationReason::OutputUnavailable;
        // A selected SR write still grants neither callback read rights nor
        // proof that FSR's presentColor is this exact version/region.
        return SourceAssociationReason::MissingConsumerRights;
    }
};
// Stack-only causal carrier at the real intercepted SR caller. Every nested
// invocation installs a barrier, including unselected/unqualified invocations.
class NativeSourceTransactionScope
{
    friend class DlssNr::NativeDx12Source;
#ifdef NR_SOURCE_TRANSACTION_TESTING
    friend struct SourceTransactionTestAccess;
#endif
    inline static thread_local NativeSourceTransactionScope* current_=nullptr;
    NativeSourceTransactionScope* const previous_;
    const std::size_t depth_;
    std::shared_ptr<NativeSourceTransactionObservation> observation_;
    explicit NativeSourceTransactionScope(std::shared_ptr<NativeSourceTransactionObservation> source={})noexcept
        :previous_(current_),depth_(previous_?previous_->depth_+1:1),observation_(depth_<=8?std::move(source):nullptr)
    {current_=this;}
  public:
    ~NativeSourceTransactionScope(){current_=previous_;}
    NativeSourceTransactionScope(const NativeSourceTransactionScope&)=delete;
    NativeSourceTransactionScope& operator=(const NativeSourceTransactionScope&)=delete;
    static std::shared_ptr<NativeSourceTransactionObservation> Current()noexcept
    {return current_?current_->observation_:nullptr;}
};
struct SourceAssociationObservation
{
    SourceAssociationReason reason=SourceAssociationReason::Missing;
    std::shared_ptr<const NativeSourceTransactionObservation> source;
};
}
