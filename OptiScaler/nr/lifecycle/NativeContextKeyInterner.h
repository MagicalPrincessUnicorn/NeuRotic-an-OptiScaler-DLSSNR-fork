#pragma once
#include "OwnerPublicationJournal.h"
#include "OwnerMetadataArena.h"
#include <nr/context/RepresentationCacheKeys.h>
#include <array>
#include <memory>
#include <variant>

namespace Neurotic::Lifecycle
{
// Retained, owner-serialized C12 key publications. Exact typed values are kept;
// no digest is an equality shortcut and no key carries content or C03 rights.
// The canonical Context journal remains the sole publication sequence owner.
class NativeContextKeyInterner
{
    using Value=std::variant<Context::SemanticRepresentationKey,Context::RepresentationPlanKey,Context::AllocationRequirementKey>;
    struct Entry
    {
        Value value;
        std::optional<C::RecordKey> publication;
        template<class T>explicit Entry(const T& key):value(std::in_place_type<T>,key){}
    };
    static constexpr std::size_t Maximum=32;
    OwnerPublicationJournal& journal_;
    const OwnerBinding binding_;
    const std::size_t capacity_;
    std::array<std::unique_ptr<Entry>,Maximum> entries_;
    std::size_t used_=0;
    template<class T>C::RecordKey Put(const T& value)
    {
        for(std::size_t i=0;i<used_;++i)
            if(const auto* key=std::get_if<T>(&entries_[i]->value);key&&*key==value)
            {
                if(!entries_[i]->publication)throw MetadataRefusal{};
                return *entries_[i]->publication;
            }
        if(used_==capacity_)throw MetadataRefusal{};
        auto entry=std::make_unique<Entry>(value);
        auto& retained=entries_[used_++];retained=std::move(entry);
        // Consume this key/slot before publication. Interrupted issuance cannot
        // retry after recovery or repurpose the same retained record identity.
        if(journal_.Binding()!=binding_)throw MetadataRefusal{};
        retained->publication=journal_.Event().evidence.record;
        return *retained->publication;
    }
  public:
    explicit NativeContextKeyInterner(OwnerPublicationJournal& journal,std::size_t capacity=Maximum):
        journal_(journal),binding_(journal.Binding()),capacity_(capacity)
    {
        if(!ValidBinding(binding_)||binding_.owner!=C::OwnerDomain::Context||!capacity||capacity>Maximum)throw MetadataRefusal{};
    }
    NativeContextKeyInterner(const NativeContextKeyInterner&)=delete;
    NativeContextKeyInterner& operator=(const NativeContextKeyInterner&)=delete;
    C::RecordKey Intern(const Context::SemanticRepresentationKey& key){return Put(key);}
    C::RecordKey Intern(const Context::RepresentationPlanKey& key){return Put(key);}
    C::RecordKey Intern(const Context::AllocationRequirementKey& key){return Put(key);}
    std::size_t Used()const noexcept{return used_;}
};
}
