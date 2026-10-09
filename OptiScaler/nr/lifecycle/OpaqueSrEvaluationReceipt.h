#pragma once

#include <nr/contracts/SemanticDescriptions.h>
#include <d3d12.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <limits>
#include <optional>

namespace Neurotic::Lifecycle
{
// CPU operation provenance at the selected public NGX D3D12 call boundary.
// This value does not issue a Resource revision, certify pixels, or prove GPU
// completion. Its reserved revision comes from the existing Resource owner.
class OpaqueSrEvaluationReceipt
{
    friend class NativeProcessBootstrap;
#ifdef NR_SELECTED_SR_TESTING
    friend class SelectedSrTestAccess;
#endif
  public:
    enum class Class
    {
        RefusedBeforeEntry, EnteredNoRecordedWork, RecordedPossibleEffect,
        RecordedOpaqueWrite, FailedAfterPossibleEffect, BindingChanged,
        RecordingIdentityChanged, Unavailable
    };
    struct ResourceBinding
    {
        ID3D12Resource* native=nullptr; // retained by the callback's resource owner
        Contracts::ResourceIdentityToken identity;
        bool operator==(const ResourceBinding&)const=default;
    };
    struct Entry
    {
        std::optional<std::uint64_t> providerGeneration,coreGeneration,moduleGeneration;
        const void* featureHandle=nullptr;
        std::uint64_t featureGeneration=0;
        ID3D12GraphicsCommandList* nativeList=nullptr;
        std::uint64_t recordingIncarnation=0;
        const void* parameters=nullptr;
        // Color, Depth and MotionVectors are required. Exposure and bias mask
        // are optional; if bound, their Resource content versions are required.
        std::array<ResourceBinding,5> inputs{};
        // Exact source-owner callback publication. It describes the inputs
        // read by this invocation; it never supplies their physical versions.
        std::optional<Contracts::RecordKey> invocationPublication;
        ResourceBinding output;
        Contracts::Rectangle outputRegion;
        std::uint32_t outputSubresource=0;
        std::uint64_t entrySequence=0,workOrdinal=0;
        bool completeCoverage=false;
        Contracts::OptionalFact<Contracts::ContentRevision> reservedRevision;
    };
    struct Return
    {
        const void* featureHandle=nullptr;
        std::uint64_t featureGeneration=0;
        ID3D12GraphicsCommandList* nativeList=nullptr;
        std::uint64_t recordingIncarnation=0;
        const void* parameters=nullptr;
        std::array<ResourceBinding,5> inputs{};
        ResourceBinding output;
        Contracts::Rectangle outputRegion;
        std::uint32_t outputSubresource=0;
        std::uint64_t returnSequence=0,workOrdinal=0;
        bool completeCoverage=false;
        std::uint32_t ngxResult=0,ngxSuccess=0;
        bool parameterValuesMatch=true;
    };
  private:
    Entry entry_;
    std::optional<Return> returned_;
    Class classification_=Class::Unavailable;
    bool entered_=false,sealed_=false,possibleEffects_=false,selectedSr_=false;
    static bool Structural(const Contracts::ResourceIdentityToken& id)noexcept
    {
        return id.Check()==Contracts::Error::None&&id.objectIncarnation.IsKnown()&&
            id.resourceIncarnation.IsKnown()&&id.resourceViewIncarnation.IsKnown()&&
            id.resourceGeneration.IsKnown()&&id.representationGeneration.IsKnown();
    }
    static bool Input(const ResourceBinding& binding,bool required,bool invocation)noexcept
    {
        if(!binding.native)return !required;
        return Structural(binding.identity)&&(invocation||(binding.identity.contentRevision.IsKnown()&&
            binding.identity.contentRevision.KnownPart()->value.value!=0));
    }
    static bool Valid(const Entry& e,bool selected=false)noexcept
    {
        if(!e.featureHandle||!e.featureGeneration||!e.nativeList||!e.recordingIncarnation||
           !e.parameters||!e.entrySequence||!e.completeCoverage||!e.output.native||
           !Structural(e.output.identity)||!e.outputRegion.width||!e.outputRegion.height||
           !e.reservedRevision.IsKnown()||!e.reservedRevision.KnownPart()->value.value)return false;
        const bool invocation=selected&&e.invocationPublication&&e.invocationPublication->Check()==Contracts::Error::None;
        for(std::size_t i=0;i<e.inputs.size();++i)if(!Input(e.inputs[i],i<3,invocation))return false;
        for(const auto& generation:{e.providerGeneration,e.coreGeneration,e.moduleGeneration})
            if(generation&&!*generation)return false;
        return true;
    }
    // Only the actual SuperSampling source boundary may enroll this adapter.
    // Public NGX SR defines the operation's Color input and Output destination.
    // We still require stable bindings, the same recording and observed work;
    // no generic opaque-call receipt can acquire this meaning after entry.
    bool EnrollNgxSuperSampling()noexcept
    {
        if(entered_||sealed_||selectedSr_||!Valid(entry_,true))return false;
        selectedSr_=true;classification_=Class::RefusedBeforeEntry;return true;
    }
  public:
    explicit OpaqueSrEvaluationReceipt(Entry entry):entry_(std::move(entry))
    {classification_=Valid(entry_)?Class::RefusedBeforeEntry:Class::Unavailable;}
    OpaqueSrEvaluationReceipt(const OpaqueSrEvaluationReceipt&)=delete;
    OpaqueSrEvaluationReceipt& operator=(const OpaqueSrEvaluationReceipt&)=delete;
    static std::uint64_t NextSequence()noexcept
    {
        static std::atomic<std::uint64_t> sequence{0};
        auto prior=sequence.load(std::memory_order_relaxed);
        while(prior!=(std::numeric_limits<std::uint64_t>::max)())
            if(sequence.compare_exchange_weak(prior,prior+1,std::memory_order_relaxed))return prior+1;
        return 0;
    }
    Class Classification()const noexcept{return classification_;}
    const Entry& Original()const noexcept{return entry_;}
    const std::optional<Return>& ObservedReturn()const noexcept{return returned_;}
    bool EvaluationInvoked()const noexcept{return entered_;}
    bool PossibleEffects()const noexcept{return possibleEffects_;}
    const Contracts::OptionalFact<Contracts::ContentRevision>& ReservedRevision()const noexcept
    {return entry_.reservedRevision;}
    std::optional<Contracts::OptionalFact<Contracts::ContentRevision>> PublishedRevision()const noexcept
    {
        if(selectedSr_&&sealed_&&classification_==Class::RecordedOpaqueWrite)return entry_.reservedRevision;
        return {};
    }
    bool MarkEntered()noexcept
    {
        if(entered_||sealed_)return false;
        entered_=true;
        possibleEffects_=true;
        if(classification_==Class::RefusedBeforeEntry)classification_=Class::RecordedPossibleEffect;
        return true;
    }
    std::optional<Class> Seal(const Return& observed)noexcept
    {
        if(sealed_)return {};
        sealed_=true;
        returned_=observed;
        if(!entered_)return classification_;
        if(classification_==Class::Unavailable)return classification_;
        if(observed.nativeList!=entry_.nativeList||
           observed.recordingIncarnation!=entry_.recordingIncarnation||
           observed.workOrdinal<entry_.workOrdinal)
            classification_=Class::RecordingIdentityChanged;
        else if(observed.featureHandle!=entry_.featureHandle||
                observed.featureGeneration!=entry_.featureGeneration||
                observed.parameters!=entry_.parameters||observed.inputs!=entry_.inputs||
                observed.output!=entry_.output||observed.outputRegion!=entry_.outputRegion||
                observed.outputSubresource!=entry_.outputSubresource||!observed.parameterValuesMatch)
            classification_=Class::BindingChanged;
        else if(!observed.completeCoverage||observed.returnSequence<=entry_.entrySequence)
            classification_=Class::RecordedPossibleEffect;
        else if(observed.ngxResult!=observed.ngxSuccess)
            classification_=Class::FailedAfterPossibleEffect;
        else if(observed.workOrdinal==entry_.workOrdinal)
            classification_=Class::EnteredNoRecordedWork;
        else if(selectedSr_)classification_=Class::RecordedOpaqueWrite;
        else classification_=Class::RecordedPossibleEffect;
        return classification_;
    }
};
}
