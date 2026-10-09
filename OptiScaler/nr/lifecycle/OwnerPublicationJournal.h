#pragma once
#include "IdentityPrimitives.h"
#include <stdexcept>
#include <string>

namespace Neurotic::Lifecycle
{
class PreparedObservationPublication;
// Owner-local publication sequence only, never a C14 allocator or a game-frame
// counter. The composition root binds the subject once; its owner serializes
// calls. Overflow refuses without reusing any record/evidence identity.
class OwnerPublicationJournal
{
    friend class NativeOwnerSet;
    friend class PreparedObservationPublication;
    OwnerBinding binding_;
    std::uint64_t sequence_=0;
    static C::Symbol Symbol(std::string_view text)
    {C::Symbol value;if(!value.Assign(text))throw std::invalid_argument("owner publication namespace");return value;}
    C::RecordKey Next()
    {
        if(!ValidBinding(binding_)||sequence_==(std::numeric_limits<std::uint64_t>::max)())
            throw std::overflow_error("owner publication exhausted");
        return {binding_.evidenceNamespace,binding_.publisher.issuer,++sequence_};
    }
    OwnerPublicationJournal(const C::Symbol& process,C::OwnerDomain owner,std::uint64_t subject)
    {
        if(process.Empty()||!subject||owner==C::OwnerDomain::Unspecified||C::EnumName(owner).empty())
            throw std::invalid_argument("owner publication binding");
        const auto prefix=std::string(process.View());const auto issuer=Symbol(C::EnumName(owner));
        binding_={owner,{Symbol(prefix+".publisher"),issuer,subject},
            {Symbol(prefix+".subject"),issuer,subject},Symbol(prefix+".events."+std::to_string(subject))};
    }
  public:
    OwnerPublicationJournal()=default;
    OwnerPublicationJournal(const OwnerPublicationJournal&)=delete;
    OwnerPublicationJournal& operator=(const OwnerPublicationJournal&)=delete;
    OwnerPublicationJournal(OwnerPublicationJournal&& other)noexcept:binding_(other.binding_),sequence_(other.sequence_)
    {other.binding_={};other.sequence_=(std::numeric_limits<std::uint64_t>::max)();}
    OwnerPublicationJournal& operator=(OwnerPublicationJournal&& other)
    {
        if(this==&other)return *this;
        if(ValidBinding(binding_))throw std::logic_error("cannot replace a registered owner journal");
        binding_=other.binding_;sequence_=other.sequence_;
        other.binding_={};other.sequence_=(std::numeric_limits<std::uint64_t>::max)();return *this;
    }
    const OwnerBinding& Binding()const noexcept{return binding_;}
    OwnerEvent Event(){return {binding_.owner,binding_.publisher,binding_.subject,C::EvidenceRef{Next()}};}
    C::RecordHeader Header(C::ContractId contract,const C::ScopeRef& scope)
    {C::RecordHeader header;header.contract=contract;header.owner=binding_.owner;header.record=Next();header.revision=1;header.scope=scope;return header;}
};
}
