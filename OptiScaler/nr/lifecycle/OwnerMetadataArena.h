#pragma once
#include <nr/context/ContextMetadata.h>
#include <charconv>
#include <cstddef>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

#define NR_OWNER_METADATA_RESERVATION_V1 1

namespace Neurotic::Lifecycle
{
class MetadataRefusal final:public std::exception
{
  public: const char* what()const noexcept override{return "bounded owner metadata publication refused";}
};
// Cold-allocated, append-only CPU metadata storage owned by one retained invocation.
// No reset/eviction API: the composition owner retains the arena while any finalizer,
// resource, provider or replayable recording references its publications. It owns
// no native object, descriptor, C14 identity or GPU completion state.
class OwnerMetadataArena
{
    enum class EntryState { Reserved, Cancelled, Published };
    struct Entry
    {
        const void* type=nullptr;void* value=nullptr;
        C::OwnerDomain owner=C::OwnerDomain::Unspecified;
        void (*destroy)(void*)noexcept=nullptr;
        EntryState state=EntryState::Cancelled;
    };
    // Tokens retain this marker only, never the arena or any published body.
    // Like Publish/Resolve, reservations require serialized owner access. This
    // marker prevents stale token access; it does not grant concurrent lifetime.
    struct Lifetime {OwnerMetadataArena* arena;};
    std::shared_ptr<Lifetime> lifetime_;
    C::RecordKey domain_;
    std::unique_ptr<std::byte[]> bytes_;
    std::unique_ptr<Entry[]> entries_;
    std::size_t capacity_=0,entryCapacity_=0,used_=0,count_=0;
    template<class T>static const void* Type()noexcept
    {static const char token=0;return &token;} // local type tag only, never serialized identity
    template<class T>C::MetadataRef<T> Publish(const T& value,C::OwnerDomain owner)
    {
        static_assert(alignof(T)<=alignof(std::max_align_t));
        if(owner==C::OwnerDomain::Unspecified||C::EnumName(owner).empty()||count_==entryCapacity_||
           !Context::ValidValues(value))throw MetadataRefusal{};
        const auto padding=(alignof(T)-used_%alignof(T))%alignof(T);
        if(padding>capacity_-used_||sizeof(T)>capacity_-used_-padding)throw MetadataRefusal{};
        auto* body=::new(static_cast<void*>(bytes_.get()+used_+padding))T(value);
        entries_[count_]={Type<T>(),body,owner,[](void* p)noexcept{static_cast<T*>(p)->~T();},EntryState::Published};
        used_+=padding+sizeof(T);++count_;
        C::MetadataRef<T> reference;reference.owner=owner;reference.record=domain_;
        reference.record.value=count_;reference.revision=1;return reference;
    }
  public:
    template<class T>class Reservation
    {
        friend class OwnerMetadataArena;
        std::shared_ptr<Lifetime> lifetime_;
        C::MetadataRef<T> reference_;
        std::size_t index_=0;
        bool active_=true;
        Reservation(std::shared_ptr<Lifetime> lifetime,C::MetadataRef<T> reference,std::size_t index)noexcept:
            lifetime_(std::move(lifetime)),reference_(reference),index_(index){}
      public:
        Reservation(const Reservation&)=delete;
        Reservation& operator=(const Reservation&)=delete;
        Reservation(Reservation&& other)noexcept:
            lifetime_(std::move(other.lifetime_)),reference_(other.reference_),index_(other.index_),
            active_(std::exchange(other.active_,false)){}
        Reservation& operator=(Reservation&& other)noexcept
        {
            if(this!=&other)
            {
                Cancel();lifetime_=std::move(other.lifetime_);reference_=other.reference_;index_=other.index_;
                active_=std::exchange(other.active_,false);
            }
            return *this;
        }
        ~Reservation(){Cancel();}
        const C::MetadataRef<T>& Reference()const noexcept{return reference_;}
        bool Cancel()noexcept
        {
            if(!std::exchange(active_,false)||!lifetime_||!lifetime_->arena)return false;
            auto& entry=lifetime_->arena->entries_[index_];
            if(entry.state!=EntryState::Reserved)return false;
            entry.state=EntryState::Cancelled;return true;
        }
        // Canonical value validation performs no allocation. A bad value or
        // validation exception consumes this token before any body is created.
        // Trivial, nothrow copying excludes allocating payload constructors.
        bool Commit(const T& value)noexcept
        {
            if(!std::exchange(active_,false)||!lifetime_||!lifetime_->arena)return false;
            auto& entry=lifetime_->arena->entries_[index_];
            if(entry.state!=EntryState::Reserved)return false;
            entry.state=EntryState::Cancelled;
            try{if(!Context::ValidValues(value))return false;}catch(...){return false;}
            ::new(entry.value)T(value);
            entry.destroy=[](void* p)noexcept{static_cast<T*>(p)->~T();};
            entry.state=EntryState::Published;return true;
        }
    };
  private:
    template<class T>std::optional<Reservation<T>> Reserve(C::OwnerDomain owner)noexcept
    {
        static_assert(std::is_trivially_copyable_v<T> && std::is_nothrow_copy_constructible_v<T> &&
                      std::is_trivially_destructible_v<T>,"Reserved metadata must be a non-allocating value payload");
        static_assert(alignof(T)<=alignof(std::max_align_t));
        if(owner==C::OwnerDomain::Unspecified||C::EnumName(owner).empty()||count_==entryCapacity_)return {};
        const auto padding=(alignof(T)-used_%alignof(T))%alignof(T);
        if(padding>capacity_-used_||sizeof(T)>capacity_-used_-padding)return {};
        const auto index=count_;
        entries_[index]={Type<T>(),bytes_.get()+used_+padding,owner,nullptr,EntryState::Reserved};
        used_+=padding+sizeof(T);++count_;
        C::MetadataRef<T> reference;reference.owner=owner;reference.record=domain_;
        reference.record.value=count_;reference.revision=1;
        return Reservation<T>(lifetime_,reference,index);
    }
  public:
    class Publisher
    {
        friend class OwnerMetadataArena;
        OwnerMetadataArena* arena_;C::OwnerDomain owner_;
        Publisher(OwnerMetadataArena& arena,C::OwnerDomain owner):arena_(&arena),owner_(owner){}
      public:
        template<class T>C::MetadataRef<T> Publish(const T& value)
        {return arena_->Publish(value,owner_);}
        template<class T>C::MetadataRef<T> Publish(const T& value,C::OwnerDomain owner)
        {if(owner!=owner_)throw MetadataRefusal{};return Publish(value);}
        // Reserve before effects. Cancelled/failed slots stay consumed so their
        // unpublished references can never resolve to a later publication.
        template<class T>std::optional<Reservation<T>> Reserve()noexcept
        {return arena_->Reserve<T>(owner_);}
        template<class T>Context::MetadataView<T> Resolve(const C::MetadataRef<T>& reference)const
        {return arena_->Resolve(reference);}
    };
    // publicationDomain is supplied by the Session owner's metadata journal. This
    // derives only a disjoint record namespace; it never mints a C14 identity.
    OwnerMetadataArena(const C::RecordKey& publicationDomain,std::size_t bytes,std::size_t entries):
        domain_(publicationDomain),capacity_(bytes),entryCapacity_(entries)
    {
        if(publicationDomain.Check()!=C::Error::None||!bytes||!entries)throw MetadataRefusal{};
        char suffix[24];const auto number=std::to_chars(suffix,suffix+sizeof(suffix),publicationDomain.value);
        if(number.ec!=std::errc{})throw MetadataRefusal{};
        const std::string scope=std::string(publicationDomain.nameSpace.View())+"."+std::string(suffix,number.ptr);
        if(!domain_.nameSpace.Assign(scope))throw MetadataRefusal{};
        bytes_=std::make_unique<std::byte[]>(bytes);entries_=std::make_unique<Entry[]>(entries);
        lifetime_=std::make_shared<Lifetime>(Lifetime{this});
    }
    ~OwnerMetadataArena()
    {
        lifetime_->arena=nullptr;
        for(std::size_t i=count_;i>0;--i)
            if(entries_[i-1].destroy)entries_[i-1].destroy(entries_[i-1].value);
    }
    OwnerMetadataArena(const OwnerMetadataArena&)=delete;
    OwnerMetadataArena& operator=(const OwnerMetadataArena&)=delete;
    Publisher ForOwner(C::OwnerDomain owner)
    {if(owner==C::OwnerDomain::Unspecified||C::EnumName(owner).empty())throw MetadataRefusal{};return {*this,owner};}
    template<class T>Context::MetadataView<T> Resolve(const C::MetadataRef<T>& reference)const
    {
        if(reference.record.nameSpace!=domain_.nameSpace||reference.record.issuer!=domain_.issuer||
           reference.record.value==0||reference.record.value>count_||reference.revision!=1||
           reference.schemaVersion!=C::SchemaVersion{})return {};
        const auto& entry=entries_[static_cast<std::size_t>(reference.record.value-1)];
        if(entry.state!=EntryState::Published||entry.type!=Type<T>()||entry.owner!=reference.owner)return {};
        return {reference,static_cast<const T*>(entry.value)};
    }
};
}
