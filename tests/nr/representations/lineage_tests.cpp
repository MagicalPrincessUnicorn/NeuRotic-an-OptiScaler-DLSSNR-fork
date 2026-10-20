#include "../context/ContextFixtureStore.h"
#include "../oracles/Test002AnalyticCases.h"
#if __has_include(<nr/context/RepresentationLineage.h>)
#include <nr/context/RepresentationLineage.h>
#define HAS_LINEAGE 1
#endif
using namespace NrTest002;
using NrContextTests::Store;
static C::TransformStep Step(Store& store,C::TransformKind kind,std::string_view name,double factor)
{
    C::TransformStep step;step.kind=kind;step.operation=Sym(name);step.version=1;
    step.sourceUnits=Known(Sym("linear"));step.targetUnits=Known(Sym("linear"));
    C::RecordReference qualification;qualification.contract=C::ContractId::C04;qualification.record=Key("qual");qualification.revision=1;
    qualification.recordType=Sym("QualificationCertificate");step.qualification=Known(qualification);
    step.alreadyApplied=Known(false);
    C::BoundedList<C::SemanticClaim,8> params;C::SemanticClaim p;
    p.field=Sym("factor");p.raw=Known(C::ScalarValue{factor});p.effective=p.raw;params.Push(p);
    step.parameters=NrContextTests::PublishList(store,params);return step;
}
int main(int argc,char** argv)
{
    return Guard([&]{
        Report r("representation-lineage",argc==2?std::string_view(argv[1]):std::string_view{});
#ifdef HAS_LINEAGE
        namespace S=Neurotic::Context;
        Store store;C::BoundedList<C::TransformStep,16> wanted,done;
        wanted.Push(Step(store,C::TransformKind::MotionScale,"scale",2));
        wanted.Push(Step(store,C::TransformKind::JitterRemoval,"unjitter",1));
        wanted.Push(Step(store,C::TransformKind::ExposureUndo,"undo-exposure",.5));
        wanted.Push(Step(store,C::TransformKind::ColorTransfer,"linear-color",1));
        for(std::size_t i=0;i<wanted.Size();++i)
        {
            auto step=*wanted.Get(i);step.alreadyApplied=Known(true);done.Push(step);
            const auto result=S::CompareLineage(done,wanted,store);
            r.Check(std::string("T14.once-")+std::to_string(i),result.compatible&&result.delta.Size()==3-i,true,"CompareLineage");
        }
        const auto complete=S::CompareLineage(done,wanted,store);
        double scalar=4;for(std::size_t i=0;i<complete.delta.Size();++i)if(complete.delta.Get(i)->kind==C::TransformKind::ExposureUndo)scalar*=.5;
        r.Check("T14.exposure-once",scalar==4,true,"CompareLineage");
        auto wrong=wanted;*wrong.Get(0)=Step(store,C::TransformKind::MotionScale,"scale",3);
        r.Check("T14.parameter-mismatch",S::CompareLineage(done,wrong,store).compatible,false,"CompareLineage");
        wrong=wanted;wrong.Get(0)->version=2;
        r.Check("T14.version-mismatch",S::CompareLineage(done,wrong,store).compatible,false,"CompareLineage");
        wrong=wanted;std::swap(*wrong.Get(0),*wrong.Get(1));
        r.Check("T14.order-mismatch",S::CompareLineage(done,wrong,store).compatible,false,"CompareLineage");
        wrong=done;wrong.Get(0)->alreadyApplied={};
        r.Check("T14.unknown-applied",S::CompareLineage(wrong,wanted,store).compatible,false,"CompareLineage");
        wrong=wanted;wrong.Get(0)->sourceUnits={};
        r.Check("T14.unknown-units",S::CompareLineage({},wrong,store).compatible,false,"CompareLineage");
        wrong=wanted;wrong.Get(0)->parameters.backing->revision=2;
        r.Check("T14.stale-parameters",S::CompareLineage({},wrong,store).compatible,false,"CompareLineage");
        S::FusionRule rule;rule.operation=Sym("affine.fused");rule.version=1;rule.steps=wanted;
        rule.mapping.source=Analytic::MotionInput().motion.currentRaster;rule.mapping.target=rule.mapping.source;
        rule.mapping.scale=Known(C::Vec2{1,1});rule.mapping.offset=Known(C::Vec2{0,0});
        C::RecordReference mappingEvidence;mappingEvidence.contract=C::ContractId::C04;mappingEvidence.record=Key("map");mappingEvidence.revision=1;
        mappingEvidence.recordType=Sym("QualificationCertificate");rule.mapping.mappingEvidence=Known(mappingEvidence);
        rule.precision=Sym("float32");
        const auto fused=S::MatchFusion(rule,wanted,rule.mapping,rule.precision,store);
        r.Check("T14.fused-preserves-steps",fused&&fused->constituents==wanted,true,"MatchFusion");
        auto map=rule.mapping;map.target.active=Known(C::Rectangle{0,0,400,400});
        r.Check("T14.fusion-raster",S::MatchFusion(rule,wanted,map,rule.precision,store).has_value(),false,"MatchFusion");
        r.Check("T14.fusion-precision",S::MatchFusion(rule,wanted,rule.mapping,Sym("float16"),store).has_value(),false,"MatchFusion");
        wrong=wanted;*wrong.Get(0)=Step(store,C::TransformKind::MotionScale,"scale",3);
        r.Check("T14.fusion-parameters",S::MatchFusion(rule,wrong,rule.mapping,rule.precision,store).has_value(),false,"MatchFusion");
        wrong=wanted;std::swap(*wrong.Get(0),*wrong.Get(1));
        r.Check("T14.fusion-order",S::MatchFusion(rule,wrong,rule.mapping,rule.precision,store).has_value(),false,"MatchFusion");
        S::FusionRule affine=rule;affine.steps={};
        affine.steps.Push(Step(store,C::TransformKind::Other,"multiply",2));
        affine.steps.Push(Step(store,C::TransformKind::Other,"add",3));
        const auto affineFused=S::MatchFusion(affine,affine.steps,affine.mapping,affine.precision,store);
        double sample=8;
        if(affineFused)for(const auto& effect:affineFused->constituents)
        {
            const C::BoundedList<C::SemanticClaim,8>* params=nullptr;
            if(!S::ResolveList(effect.parameters,store,params)||!params)throw std::runtime_error("affine fixture missing parameter");
            const auto number=std::get<double>(params->Get(0)->effective.KnownPart()->value);
            if(effect.operation.View()=="multiply")sample*=number;else if(effect.operation.View()=="add")sample+=number;
        }
        r.Check("T14.affine-fusion-relation",affineFused&&affineFused->constituents.Size()==2&&sample==19,true,"MatchFusion");
        auto unknown=affine;C::BoundedList<C::SemanticClaim,8> missing;C::SemanticClaim parameter;parameter.field=Sym("factor");missing.Push(parameter);
        unknown.steps.Get(0)->parameters=NrContextTests::PublishList(store,missing);
        r.Check("T14.fusion-unknown-parameter",S::MatchFusion(unknown,unknown.steps,unknown.mapping,unknown.precision,store).has_value(),false,"MatchFusion");
        wrong=wanted;wrong.Push(*wanted.Get(0));
        r.Check("T14.duplicate-request-refused",S::CompareLineage({},wrong,store).compatible,false,"CompareLineage");
#else
        Store store;(void)Step(store,C::TransformKind::MotionScale,"scale",2);
        r.Check("T14.owner-missing",false,true,"CompareLineage unavailable");
#endif
        return r.Finish();
    });
}
