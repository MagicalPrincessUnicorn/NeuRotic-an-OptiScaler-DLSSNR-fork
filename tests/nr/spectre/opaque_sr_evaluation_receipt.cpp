#include "../contracts/Test002Support.h"
#include <d3d12.h>
#include <iostream>
#if __has_include(<nr/lifecycle/OpaqueSrEvaluationReceipt.h>)
#define NR_SELECTED_SR_TESTING
#include <nr/lifecycle/OpaqueSrEvaluationReceipt.h>
#define HAS_OPAQUE_SR_RECEIPT 1
#endif

namespace C=Neurotic::Contracts;
using namespace NrTest002;

#ifdef HAS_OPAQUE_SR_RECEIPT
using R=Neurotic::Lifecycle::OpaqueSrEvaluationReceipt;
namespace Neurotic::Lifecycle
{
class SelectedSrTestAccess
{
  public:
    static bool Enroll(OpaqueSrEvaluationReceipt& receipt)
    {return receipt.EnrollNgxSuperSampling();}
};
}
static C::ResourceIdentityToken Identity(unsigned number,bool content)
{
    C::ResourceIdentityToken token;
    token.objectIncarnation=Known(C::StableIdentity{C::IdentityKind::ObjectIncarnation,Sym("object"),Sym("owner"),number});
    token.resourceIncarnation=Known(C::StableIdentity{C::IdentityKind::ResourceIncarnation,Sym("resource"),Sym("owner"),number});
    token.resourceViewIncarnation=Known(C::StableIdentity{C::IdentityKind::ResourceViewIncarnation,Sym("view"),Sym("owner"),number});
    token.resourceGeneration=Known(Gen(C::IdentityKind::ResourceGeneration,"resource",number));
    token.representationGeneration=Known(Gen(C::IdentityKind::RepresentationGeneration,"representation",number));
    if(content)token.contentRevision=Known(C::ContentRevision{number});
    return token;
}
static R::Entry Entry()
{
    R::Entry e;
    e.providerGeneration=10;e.coreGeneration=20;e.moduleGeneration=30;
    e.featureHandle=reinterpret_cast<void*>(0x1000);e.featureGeneration=11;
    e.nativeList=reinterpret_cast<ID3D12GraphicsCommandList*>(0x2000);e.recordingIncarnation=7;
    e.parameters=reinterpret_cast<void*>(0x3000);e.entrySequence=100;e.workOrdinal=5;e.completeCoverage=true;
    for(unsigned i=0;i<3;++i)e.inputs[i]={reinterpret_cast<ID3D12Resource*>(std::uintptr_t{0x4000}+i*0x100),Identity(i+1,true)};
    e.output={reinterpret_cast<ID3D12Resource*>(0x5000),Identity(9,false)};
    e.outputRegion={2,1,8,4};e.outputSubresource=0;
    e.reservedRevision=Known(C::ContentRevision{10});return e;
}
static R::Return Return(const R::Entry& e)
{
    R::Return result;
    result.featureHandle=e.featureHandle;result.featureGeneration=e.featureGeneration;
    result.nativeList=e.nativeList;result.recordingIncarnation=e.recordingIncarnation;
    result.parameters=e.parameters;result.inputs=e.inputs;result.output=e.output;
    result.outputRegion=e.outputRegion;result.outputSubresource=e.outputSubresource;
    result.returnSequence=e.entrySequence+1;result.workOrdinal=e.workOrdinal+1;
    result.completeCoverage=true;result.ngxResult=0;result.ngxSuccess=0;
    return result;
}
#endif

int main()
{
#ifndef HAS_OPAQUE_SR_RECEIPT
    std::cerr<<"FAIL opaque SR evaluation receipt is absent\n";return 1;
#else
    int count=0,failures=0;
    auto check=[&](bool value,const char* label){++count;if(!value){++failures;std::cerr<<"FAIL "<<label<<'\n';}};
    const auto e=Entry();
    {auto selected=e;selected.invocationPublication=Key("actual.callback.publication");
        for(auto& input:selected.inputs)input.identity.contentRevision={};
        R receipt(selected);
        check(Neurotic::Lifecycle::SelectedSrTestAccess::Enroll(receipt),"selected SR accepts exact callback provenance with unknown physical input versions");
        receipt.MarkEntered();
        check(receipt.Seal(Return(selected))==R::Class::RecordedOpaqueWrite&&receipt.PublishedRevision()==selected.reservedRevision,
            "selected SR success commits only its reserved caller output revision");
        check(!receipt.Original().inputs[0].identity.contentRevision.IsKnown(),"selected SR never invents game input content revisions");}
    {R receipt(e);receipt.MarkEntered();
        check(!Neurotic::Lifecycle::SelectedSrTestAccess::Enroll(receipt),"adapter cannot enroll retrospectively after entry");}
    {R receipt(e);check(receipt.Classification()==R::Class::RefusedBeforeEntry,"reservation is not provider entry");
        check(receipt.ReservedRevision()==e.reservedRevision,"pre-entry reservation retained");}
    {R receipt(e);check(receipt.MarkEntered(),"real boundary can mark one entry");
        check(receipt.Classification()==R::Class::RecordedPossibleEffect,"entry begins possible effect");
        auto returned=Return(e);returned.workOrdinal=e.workOrdinal;
        check(receipt.Seal(returned)==R::Class::EnteredNoRecordedWork,"success without observed work has no revision");
        check(!receipt.PublishedRevision().has_value(),"no-work success cannot publish");}
    {R receipt(e);receipt.MarkEntered();auto returned=Return(e);returned.workOrdinal=e.workOrdinal;returned.ngxResult=1;
        receipt.Seal(returned);
        check(receipt.PossibleEffects()&&receipt.Classification()==R::Class::FailedAfterPossibleEffect,
            "failure without hook activity preserves possible unobserved effects");}
    {R receipt(e);receipt.MarkEntered();
        check(receipt.Seal(Return(e))==R::Class::RecordedPossibleEffect,"generic work and success do not authenticate caller Output write");
        check(!receipt.PublishedRevision(),"opaque activity cannot publish reserved Resource revision");
        check(!receipt.Seal(Return(e)).has_value(),"duplicate seal refused");}
    {R receipt(e);receipt.MarkEntered();auto returned=Return(e);returned.ngxResult=1;
        check(receipt.Seal(returned)==R::Class::FailedAfterPossibleEffect,"failure after work preserves possible effect");
        check(!receipt.PublishedRevision(),"failed call cannot publish");
        check(receipt.EvaluationInvoked()&&receipt.PossibleEffects()&&
              receipt.ObservedReturn()->ngxResult==1,"failure retains entry, effects and exact return result");}
    {R receipt(e);receipt.MarkEntered();auto returned=Return(e);returned.output.native=reinterpret_cast<ID3D12Resource*>(0x6000);
        check(receipt.Seal(returned)==R::Class::BindingChanged,"Output mutation rejected");
        check(receipt.PossibleEffects(),"binding mutation does not erase possible effects");}
    {R receipt(e);receipt.MarkEntered();auto returned=Return(e);returned.outputRegion.x++;
        check(receipt.Seal(returned)==R::Class::BindingChanged,"rectangle mutation rejected");}
    {R receipt(e);receipt.MarkEntered();auto returned=Return(e);returned.inputs[0].identity.contentRevision=Known(C::ContentRevision{99});
        check(receipt.Seal(returned)==R::Class::BindingChanged,"source version mismatch rejected");}
    {R receipt(e);receipt.MarkEntered();auto returned=Return(e);returned.recordingIncarnation++;
        check(receipt.Seal(returned)==R::Class::RecordingIdentityChanged,"Reset/incarnation change rejected");}
    {R receipt(e);receipt.MarkEntered();auto returned=Return(e);returned.completeCoverage=false;
        check(receipt.Seal(returned)==R::Class::RecordedPossibleEffect,"unknown hook coverage retains possible effect");}
    {auto incomplete=e;incomplete.inputs[0].identity.contentRevision={};R receipt(incomplete);
        check(receipt.Classification()==R::Class::Unavailable,"unknown input Resource revision unavailable");
        check(receipt.MarkEntered(),"boundary entry is observed even when provenance is unavailable");
        check(receipt.Seal(Return(incomplete))==R::Class::Unavailable,
            "entry with unknown provenance cannot publish");}
    std::cout<<"Opaque SR evaluation receipt: "<<count<<" checks, "<<failures<<" failures; synthetic identities only\n";
    return failures?1:0;
#endif
}
