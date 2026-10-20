#include "RepresentationFixture.h"
#if __has_include(<nr/context/PreparedViewValidation.h>)
#include <nr/context/PreparedViewValidation.h>
#define HAS_PREPARED 1
#endif
using namespace NrRepresentationTests;
int main(int argc,char** argv)
{
    return Guard([&]{Report r("representation-prepared",argc==2?std::string_view(argv[1]):std::string_view{});
#ifdef HAS_PREPARED
        Fixture f;auto key=f.KeyFor();auto plan=f.Plan();auto view=f.Prepared(plan);
        auto content=S::BuildPreparedContentKey(key,f.source,f.target,f.frame,{},false,f.store);
        if(!content)throw std::runtime_error("content fixture incomplete");
        S::PreparedViewEvidence receipt{&view,&plan,&*content,{Sym(C::PreparedView::WireName),view.header.record,view.header.revision},plan.keys.semantic,plan.keys.plan};
        auto probe=[&]{return S::EvaluatePreparedViewCompatibility(receipt,key,f.source,f.frame,{},false,f.store).compatible;};
        r.Check("T13.exact-prepared",probe(),true,"EvaluatePreparedViewCompatibility");
        f.source.identity.contentRevision=Known(C::ContentRevision{2});
        r.Check("T13.prepared-old-content",probe(),false,"EvaluatePreparedViewCompatibility");
        f.source.identity=content->source.identity;++view.header.revision;
        r.Check("T13.prepared-receipt-revision",probe(),false,"EvaluatePreparedViewCompatibility");--view.header.revision;
        view.keys.plan=Key("other");r.Check("T13.prepared-plan-publication",probe(),false,"EvaluatePreparedViewCompatibility");view.keys=plan.keys;
        plan.source.objectIncarnation={};r.Check("T13.prepared-unknown-owner-source",probe(),false,"EvaluatePreparedViewCompatibility");plan.source=f.source.identity;
        auto replaced=f.target;replaced.identity.contentRevision=Known(C::ContentRevision{9});
        view.view=f.store.Publish(replaced,C::OwnerDomain::Resource);
        r.Check("T13.prepared-output-changed",probe(),false,"EvaluatePreparedViewCompatibility");
        view.view=f.store.Publish(f.target,C::OwnerDomain::Resource);
        view.appliedTransforms.count=1;r.Check("T14.prepared-unexecuted",probe(),false,"EvaluatePreparedViewCompatibility");view.appliedTransforms={};
        r.Check("T13.restored-exact",probe(),true,"EvaluatePreparedViewCompatibility");
        // Rights are an independent LIFE-owner result; this API has no rights parameter.
        for(int rights=0;rights<3;++rights)
        {
            r.Check(std::string("T13.rights-independent-")+std::to_string(rights),probe(),true,"EvaluatePreparedViewCompatibility");
            r.Check(std::string("T13.rights-owner-")+std::to_string(rights),SyntheticAdmission(probe(),rights),rights==0,"separate synthetic admission boundary");
        }
#else
        r.Check("T13.prepared-owner-missing",false,true,"Prepared view compatibility unavailable");
#endif
        return r.Finish();});
}
