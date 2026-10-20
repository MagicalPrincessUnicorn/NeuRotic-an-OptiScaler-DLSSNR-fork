#include "../representations/RepresentationFixture.h"
#include <nr/lifecycle/NativeOwnerSet.h>
#include <iostream>
#if __has_include(<nr/lifecycle/NativeContextKeyInterner.h>)
#include <nr/lifecycle/NativeContextKeyInterner.h>
#define NR_CONTEXT_INTERNER 1
#endif
using namespace NrTest002;
namespace L=Neurotic::Lifecycle;
namespace S=Neurotic::Context;
int main()
{
    int count=0,failed=0;const auto check=[&](bool pass,const char* name){++count;if(!pass){++failed;std::cerr<<"FAIL "<<name<<'\n';}};
#ifdef NR_CONTEXT_INTERNER
    auto owners=std::make_unique<L::NativeOwnerSet>(Sym("native.interner.fixture"));
    auto fixture=std::make_unique<NrRepresentationTests::Fixture>();auto key=fixture->KeyFor();
    auto table=std::make_unique<L::NativeContextKeyInterner>(owners->ContextJournal(),4);
    const auto semantic=table->Intern(key.semantic),plan=table->Intern(key);
    check(semantic.Check()==C::Error::None&&plan.Check()==C::Error::None&&semantic!=plan,
          "typed semantic and plan keys have distinct Context publications");
    const auto before=owners->ContextJournal().Event().evidence.record;
    check(table->Intern(key)==plan&&table->Intern(key.semantic)==semantic,"equal complete keys reuse retained publications");
    const auto after=owners->ContextJournal().Event().evidence.record;
    check(after.value==before.value+1,"equal lookup issues no publication event");
    auto other=key;other.purpose=Sym("OutputModelTarget");
    check(table->Intern(other)!=plan,"consumer purpose changes full plan identity");
    S::AllocationRequirementKey allocation{fixture->frame.sessionId.KnownPart()->value,
        fixture->source.device.KnownPart()->value,fixture->target.descriptor,fixture->caps,Sym("model.target")};
    const auto allocationRef=table->Intern(allocation);
    check(allocationRef!=semantic&&allocationRef!=plan&&table->Used()==4,"allocation keys remain a separate typed domain");
    bool exhausted=false;other.precision=Sym("different");
    try{table->Intern(other);}catch(const L::MetadataRefusal&){exhausted=true;}
    check(exhausted&&table->Used()==4&&table->Intern(key)==plan,"capacity refusal retains existing full keys without recycle");
    auto partial=std::make_unique<L::NativeContextKeyInterner>(owners->ContextJournal(),2);
    auto retainedJournal=std::move(owners->ContextJournal());bool refused=false;
    try{partial->Intern(key);}catch(const std::exception&){refused=true;}
    check(refused&&partial->Used()==1,"publication failure consumes reserved key slot");
    owners->ContextJournal()=std::move(retainedJournal);
    const auto first=owners->ContextJournal().Event().evidence.record;refused=false;
    try{partial->Intern(key);}catch(const L::MetadataRefusal&){refused=true;}
    const auto last=owners->ContextJournal().Event().evidence.record;
    check(refused&&partial->Used()==1&&last.value==first.value+1,"failed key cannot retry after journal recovery");
    check(partial->Intern(other).Check()==C::Error::None&&partial->Used()==2,"different key may use remaining bounded capacity");
    bool wrongOwner=false;
    try{L::NativeContextKeyInterner invalid(owners->SourceJournal(),2);}catch(const std::exception&){wrongOwner=true;}
    check(wrongOwner,"non-Context journal cannot issue Context key records");
    check(!std::is_copy_constructible_v<L::NativeContextKeyInterner>,"retained key authority cannot be copied");
#else
    check(false,"bounded Context-owned retained key interner exists");
#endif
    std::cout<<"Native Context interner: "<<count<<" checks, "<<failed<<" failures; CPU key publications only\n";
    return failed?1:0;
}
