#pragma once
#include <inputs/universal_feeder/providers/PreparedGuideObservationAdapter.h>
#include <inputs/universal_feeder/providers/NativeObservationOwner.h>
#include <objbase.h>
#include <atomic>
#include <cstdio>

namespace Neurotic::Lifecycle
{
// Uses the existing bounded journal, metadata arena and FEED publisher. This
// invocation has no C14/Native session authority, resource registry or finalizer.
// Prepared capture tokens remain claims; real-frame identity stays unknown.
class PreparedObservationPublication
{
    inline static std::atomic_uint live_{0};
    OwnerPublicationJournal journal_;
    std::unique_ptr<OwnerMetadataArena> arena_;
    std::unique_ptr<Feed::NativeObservationOwner> publication_;
    std::optional<C::ObservationSet> observations_;
    static C::Symbol Namespace()
    {
        GUID id{};if(FAILED(CoCreateGuid(&id)))throw MetadataRefusal{};
        char name[64]{};std::snprintf(name,sizeof(name),"prepared.%08lx%04x%04x%02x%02x%02x%02x%02x%02x%02x%02x",
            id.Data1,id.Data2,id.Data3,id.Data4[0],id.Data4[1],id.Data4[2],id.Data4[3],id.Data4[4],id.Data4[5],id.Data4[6],id.Data4[7]);
        C::Symbol result;if(!result.Assign(name))throw MetadataRefusal{};return result;
    }
    PreparedObservationPublication():journal_(Namespace(),C::OwnerDomain::Provider,1){}
public:
    ~PreparedObservationPublication(){--live_;}
    static std::shared_ptr<PreparedObservationPublication> Capture(const Context::PreparedGuideValues& normalized,
        const void* subject,const void* color,const void* depth,const void* motion,const void* distrust)
    {
        if(live_.fetch_add(1)>=8){--live_;return {};}
        std::shared_ptr<PreparedObservationPublication> result;
        bool constructed=false;
        try{
            auto* raw=new PreparedObservationPublication;constructed=true;result.reset(raw);
            auto& r=*result;const auto event=r.journal_.Event();
            r.arena_=std::make_unique<OwnerMetadataArena>(event.evidence.record,8*1024*1024,256);
            auto metadata=r.arena_->ForOwner(C::OwnerDomain::Provider);
            Feed::SourceContext source;source.seed.provider=r.journal_.Binding().subject;
            source.callback=event.evidence.record;source.lineageRoot=source.callback;source.evidence=event.evidence;
            source.seed.frame=metadata.Publish(C::FrameIdentity{});
            source.seed.masks=metadata.Publish(C::MaskSet{});source.generations=metadata.Publish(C::GenerationVector{});
            const Feed::SourceDescriptor descriptor{"NeuRotic.PreparedGuides",C::GraphicsApi::D3D12,"prepared",
                normalized.source.generation,C::SourceClass::External};
            // Physical resource retention is handled by the existing GpuSafety
            // action. No fabricated C03 ResourceView is projected into metadata.
            r.publication_=std::make_unique<Feed::NativeObservationOwner>(r.journal_,*r.arena_,source,descriptor,
                subject,Feed::NativeObservationOwner::Resources{});
            if(!r.publication_->Healthy())return {};
            {Feed::ObservationScope scope(*r.publication_);Feed::Callback callback(descriptor,subject);
             Feed::ObservePreparedGuides(callback,normalized,color,depth,motion,distrust);}
            r.observations_=r.publication_->Observations();
            if(!r.publication_->Healthy() || !r.observations_ || r.publication_->Candidates().empty())return {};
            return result;
        }catch(...){if(!constructed)--live_;return {};}
    }
    const Feed::NativeObservationOwner& Publication()const{return *publication_;}
    const OwnerMetadataArena& Metadata()const{return *arena_;}
};
}
