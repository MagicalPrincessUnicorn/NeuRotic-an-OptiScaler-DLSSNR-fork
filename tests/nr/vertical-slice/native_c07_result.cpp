#include "../finalizer/finalizer_fixture.h"
#if __has_include(<nr/protocol/NativeEvaluationResult.h>)
#include <nr/protocol/NativeEvaluationResult.h>
#define NR_NATIVE_RESULT 1
#endif
using namespace NrTest002;
int main(int argc,char**argv)
{
 return Guard([&]{Report r("native-result",argc==2?std::string_view(argv[1]):std::string_view{});
#ifdef NR_NATIVE_RESULT
    namespace P=Neurotic::Protocol;
    auto fixture=std::make_unique<FinalizerTests::Fixture>();
    P::NativeExecutionFacts facts;
    const auto make=[&]{return P::BuildNativeEvaluationResult(fixture->request.recipe,fixture->candidate.result.header,facts,fixture->store);};
    auto result=make();
    r.Check("T29.native-not-attempted",result&&result->stage==C::OutcomeStage::NotAttempted,true,"BuildNativeEvaluationResult");
    facts.modelAttempted=true;facts.modelSucceeded=true;facts.commandsRecorded=true;
    result=make();
    r.Check("T29.native-model-not-output",result&&result->stage==C::OutcomeStage::Recorded&&!result->output.IsKnown(),true,"BuildNativeEvaluationResult");
    r.Check("T29.native-opaque-may-advance",result&&result->historyAdvancement.IsKnown()&&result->historyAdvancement.KnownPart()->value==C::HistoryAdvancement::MayHaveAdvanced,true,"BuildNativeEvaluationResult");
    facts.composition=C::CompositionStatus::Failed;facts.failure=Sym("Native.ResolveFailed");result=make();
    r.Check("T29.native-composition-failed",result&&result->stage==C::OutcomeStage::Failed&&!result->output.IsKnown(),true,"BuildNativeEvaluationResult");
    facts.submitted=Known(true);result=make();
    r.Check("T29.native-submitted-not-canceled",result&&result->stage==C::OutcomeStage::Interrupted,true,"BuildNativeEvaluationResult");
    facts.failure={};facts.composition=C::CompositionStatus::Accepted;
    facts.output=fixture->candidate.result.output;facts.originalPreserved=Known(false);result=make();
    r.Check("T30.native-produced",result&&result->stage==C::OutcomeStage::Produced&&result->output==facts.output,true,"BuildNativeEvaluationResult");
    facts.modelAttempted=false;
    r.Check("T30.native-contradiction-refused",!make(),true,"BuildNativeEvaluationResult");
    facts={};facts.legacyFallback=true;result=make();
    r.Check("T30.native-fallback-not-produced",result&&result->stage==C::OutcomeStage::Bypassed,true,"BuildNativeEvaluationResult");
    facts.modelAttempted=true;
    r.Check("T29.native-no-fallback-after-attempt",!make(),true,"BuildNativeEvaluationResult");
    facts={};auto header=fixture->candidate.result.header;header.owner=C::OwnerDomain::Diagnostics;
    r.Check("T30.native-strategy-owner",!P::BuildNativeEvaluationResult(fixture->request.recipe,header,facts,fixture->store),true,"BuildNativeEvaluationResult");
#else
    r.Check("T29.native-result-production",false,true,"BuildNativeEvaluationResult");
#endif
    return r.Finish();});
}
