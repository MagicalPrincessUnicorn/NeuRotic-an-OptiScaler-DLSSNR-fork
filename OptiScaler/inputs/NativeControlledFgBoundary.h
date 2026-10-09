#pragma once
#include <nr/lifecycle/FinalRealFrameTypes.h>
#include <nr/contracts/NativeDelivery.h>
#include <nr/contracts/InterceptedSourceTransaction.h>

namespace Neurotic::Lifecycle
{
// Source-private construction for the actual controlled pre-HUD, pre-tonemap route.
// Copies owner-published identities; it creates no admission or lifetime authority.
inline FinalBoundaryObservation BuildControlledFgBoundary(const C::RecordHeader& owner,
    const C::NrExecutionRecipe& recipe,const C::MetadataRef<C::GenerationVector>& generations,
    const C::NativeOutputContentV1& content,const C::NativeStageDeliveryV1& stage,
    const C::InterceptedSourceTransactionV1& transaction,const C::NativeFinalConsumerContractV1& consumer)
{
    FinalBoundaryObservation boundary;
    boundary.owner=owner;
    const C::EvidenceRef evidence{boundary.owner.record};
    const auto known=[&]<class T>(T value){return C::OptionalFact<T>::FromKnown(value,evidence);};
    boundary.frame=recipe.frame;boundary.generations=generations;
    boundary.output=content.view;boundary.candidate=stage.executionResult;
    boundary.boundary.kind=known(C::BoundaryKind::BeforeFg);boundary.boundary.episode=known(transaction.episode);
    boundary.boundary.afterRequiredHostRendering=known(true);
    boundary.boundary.hudIncluded=known(false);boundary.boundary.toneMapped=known(false);
    boundary.requiredHudIncluded=consumer.hudIncluded;
    boundary.provider=consumer.provider;boundary.handoff=consumer.handoff;
    boundary.handoffContract=consumer.contract;boundary.version=consumer.contractVersion;
    boundary.color.domain=known(consumer.color);boundary.lineageIncludesCandidate=known(true);boundary.originalPreserved=known(false);
    return boundary;
}
}
