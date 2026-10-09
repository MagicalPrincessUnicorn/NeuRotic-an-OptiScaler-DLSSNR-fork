#pragma once
#include "RoutingRules.h"
#include <nr/config/IntentSemantics.h>

namespace Neurotic::Orchestration
{
// CFG owns parsing/defaults. This projection reads its immutable accepted snapshot only.
// Explicit observation mode is an integration-owner input; parser auto never selects Auto.
inline std::optional<RoutingIntent> ProjectLegacy(const ConfigIntent::IntentSnapshot& snapshot,
    const C::OwnerValueReference& source,const C::Symbol& observationMode)
{
    if(snapshot.status!=ConfigIntent::AnalysisStatus::Equivalent||!Context::ValidValues(source)||
       source.owner!=C::OwnerDomain::Configuration||source.recordType.View()!="NR.CFG.IntentSnapshot"||source.revision!=snapshot.revision||
       (observationMode.View()!="FixedLegacy"&&observationMode.View()!="Shadow"))return {};
    // Shadow controls observation, not persisted intent. Alpha routes remain hard fixed intent.
    RoutingIntent result;result.source=source;result.mode=Symbol("FixedLegacy");
    if(snapshot.alpha.route==0)
    {
        result.fixedStrategy=Symbol("NativeTemporal");
        result.fixedPlacement=snapshot.alpha.renderingMode==1?C::Placement::NativeBefore:C::Placement::NativeAfter;
    }
    else if(snapshot.alpha.route==1||snapshot.alpha.route==2)
    {
        result.fixedStrategy=Symbol("UnifiedPresent");result.fixedPlacement=C::Placement::UnifiedPresent;
        result.require.Push(Symbol(snapshot.alpha.route==2?"PresentEnhanced":"PresentImageOnly"));
    }
    else return {};
    if(snapshot.alpha.multipassEnabled)result.require.Push(Symbol("LegacySerial"));
    return result;
}
template<class Store>C::Symbol CompareLegacy(const RoutingInput& input,const ShadowDecision& decision,const Store& store)
{
    if(!decision.valid)return Symbol("InvalidDecision");
    if(!input.legacyEffective)return Symbol("LegacyUnknown");
    if(!decision.proposed)return Symbol("NoProposedPlan");
    return Symbol(SameAnchor(*input.legacyEffective,*decision.proposed,store)?"SamePlan":"DifferentPlan");
}
}
