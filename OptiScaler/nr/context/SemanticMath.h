#pragma once
// CTX-001 pure analytic interpretation. No texture preparation or operational rights.
#include "../contracts/SemanticDescriptions.h"
#include "../lifecycle/IdentityPrimitives.h"
#include <cmath>

namespace Neurotic::Context
{
namespace C = Contracts;
inline C::UnknownFact Missing(std::string_view code, C::UnknownReason reason = C::UnknownReason::OwnerUnpublished)
{
    C::UnknownFact result;
    result.reason.category = C::ReasonCategory::CoverageRestriction;
    if (code=="CTX.MalformedObservation" || code=="CTX.IncompatibleScope" || code=="CTX.NamespaceMismatch" ||
        code=="CTX.WrongIncarnationOrGeneration" || code=="CTX.ContradictoryEvidence" || code=="CTX.InvalidRasterBasis")
        result.reason.category=C::ReasonCategory::InvalidInvariant;
    else if (code=="CTX.UnsupportedSchema" || code=="CTX.UnsupportedSemanticType" || code=="CTX.UnsupportedConsumerPurpose")
        result.reason.category=C::ReasonCategory::MissingImplementation;
    else if (code=="CTX.AliasEvidence") result.reason.category=C::ReasonCategory::Degraded;
    if (reason==C::UnknownReason::OwnerUnpublished)
    {
        if (code=="CTX.MalformedObservation" || code=="CTX.InvalidRasterBasis") reason=C::UnknownReason::Malformed;
        else if (code=="CTX.ContradictoryEvidence") reason=C::UnknownReason::ContradictoryEvidence;
        else if (code=="CTX.UnsupportedSchema" || code=="CTX.UnsupportedSemanticType" || code=="CTX.UnsupportedConsumerPurpose") reason=C::UnknownReason::UnsupportedSchema;
        else if (code=="CTX.StaleContent" || code=="CTX.WrongIncarnationOrGeneration") reason=C::UnknownReason::Expired;
        else if (code=="CTX.InvalidFramePair" || code=="CTX.IncompatibleScope" || code=="CTX.NamespaceMismatch" || code=="CTX.AmbiguousAssociation") reason=C::UnknownReason::AssociationUnproven;
    }
    result.reason.code = reason;
    C::Symbol symbol;
    if (symbol.Assign(code)) result.reason.ownerCode = symbol;
    return result;
}
template<class T> C::OptionalFact<T> Reject(std::string_view code)
{
    return C::OptionalFact<T>::FromUnknown(Missing(code));
}
inline bool Finite(C::Vec2 v) { return std::isfinite(v.x) && std::isfinite(v.y); }
template<class T> bool Established(const C::OptionalFact<T>& fact)
{
    return fact.IsKnown() && Lifecycle::ValidEvidence(fact.KnownPart()->evidence);
}
inline bool ValidRaster(const C::RasterDescription& raster)
{
    if (!Established(raster.active) || !Established(raster.allocation) ||
        !Established(raster.origin) || !Established(raster.axisSigns) || raster.Check() != C::Error::None)
        return false;
    const auto rect = raster.active.KnownPart()->value;
    const auto signs = raster.axisSigns.KnownPart()->value;
    return rect.width > 0 && rect.height > 0 &&
        raster.origin.KnownPart()->value == C::RasterOrigin::TopLeft &&
        std::abs(signs.x) == 1 && std::abs(signs.y) == 1;
}
inline C::OptionalFact<C::Vec2> RasterPointToUv(C::Vec2 point, const C::RasterDescription& raster)
{
    if (!Finite(point) || !ValidRaster(raster)) return Reject<C::Vec2>("CTX.InvalidRasterBasis");
    const auto rect = raster.active.KnownPart()->value;
    if (point.x < rect.x || point.y < rect.y || point.x > double(rect.x) + rect.width ||
        point.y > double(rect.y) + rect.height) return Reject<C::Vec2>("CTX.InvalidRasterBasis");
    return C::OptionalFact<C::Vec2>::FromKnown(
        {(point.x - rect.x) / rect.width, (point.y - rect.y) / rect.height}, raster.active.KnownPart()->evidence);
}
inline bool ValidPair(const C::FramePair& pair)
{
    if (!Established(pair.current) || !Established(pair.previous) || pair.associationEvidence.Size() == 0)
        return false;
    for (const auto& evidence : pair.associationEvidence)
        if (!Lifecycle::ValidEvidence(evidence)) return false;
    const auto current = pair.current.KnownPart()->value.Describe();
    const auto previous = pair.previous.KnownPart()->value.Describe();
    return Lifecycle::ValidIdentity(current) && Lifecycle::ValidIdentity(previous) &&
        current.nameSpace == previous.nameSpace && current.issuer == previous.issuer && current.value != previous.value;
}
inline C::OptionalFact<C::Vec2> MotionToUv(C::Vec2 value, const C::MotionDescription& motion)
{
    if (!Finite(value) || !Established(motion.direction) ||
        motion.direction.KnownPart()->value != C::MotionDirection::CurrentToPrevious)
        return Reject<C::Vec2>("CTX.UnsupportedSemanticType");
    if (!ValidPair(motion.framePair)) return Reject<C::Vec2>("CTX.InvalidFramePair");
    if (!ValidRaster(motion.currentRaster) || !ValidRaster(motion.previousRaster) ||
        !Established(motion.units) || !Established(motion.scale) || !Established(motion.validCoverage))
        return Reject<C::Vec2>("CTX.InvalidRasterBasis");
    const auto current = motion.currentRaster.active.KnownPart()->value;
    const auto previous = motion.previousRaster.active.KnownPart()->value;
    // Different active origins/extents require a sampled-point mapping, not a guessed displacement scale.
    if (current != previous || motion.validCoverage.KnownPart()->value != current)
        return Reject<C::Vec2>("CTX.InvalidRasterBasis");
    const auto scale = motion.scale.KnownPart()->value;
    const auto signs = motion.currentRaster.axisSigns.KnownPart()->value;
    if (!Finite(scale) || motion.previousRaster.axisSigns.KnownPart()->value != signs)
        return Reject<C::Vec2>("CTX.InvalidRasterBasis");
    value.x *= scale.x * signs.x;
    value.y *= scale.y * signs.y;
    switch (motion.units.KnownPart()->value)
    {
    case C::MotionUnits::Pixels: value.x /= current.width; value.y /= current.height; break;
    case C::MotionUnits::NDC: value.x *= 0.5; value.y *= 0.5; break;
    case C::MotionUnits::ActiveRasterUV: break;
    default: return Reject<C::Vec2>("CTX.UnsupportedSemanticType");
    }
    if (!Finite(value)) return Reject<C::Vec2>("CTX.MalformedObservation");
    return C::OptionalFact<C::Vec2>::FromKnown(value, motion.scale.KnownPart()->evidence);
}
// Input displacement and both jitter samples must be explicitly in canonical active-raster UV.
inline C::OptionalFact<C::Vec2> Unjitter(C::Vec2 value, const C::MotionDescription& motion)
{
    if (!Finite(value) || !Established(motion.units) ||
        motion.units.KnownPart()->value != C::MotionUnits::ActiveRasterUV ||
        !Established(motion.direction) || motion.direction.KnownPart()->value != C::MotionDirection::CurrentToPrevious ||
        !ValidPair(motion.framePair) || !Established(motion.jitterConvention))
        return Reject<C::Vec2>("CTX.UnknownRequiredFact");
    if (!ValidRaster(motion.currentRaster) || !ValidRaster(motion.previousRaster) ||
        motion.currentRaster.axisSigns.KnownPart()->value!=C::Vec2{1,1} ||
        motion.previousRaster.axisSigns.KnownPart()->value!=C::Vec2{1,1})
        return Reject<C::Vec2>("CTX.InvalidRasterBasis");
    if (motion.jitterConvention.KnownPart()->value == C::JitterConvention::Jittered)
    {
        if (!Established(motion.currentJitter) || !Established(motion.previousJitter))
            return Reject<C::Vec2>("CTX.UnknownRequiredFact");
        const auto current = motion.currentJitter.KnownPart()->value;
        const auto previous = motion.previousJitter.KnownPart()->value;
        if (!Finite(current) || !Finite(previous)) return Reject<C::Vec2>("CTX.MalformedObservation");
        value.x -= previous.x - current.x;
        value.y -= previous.y - current.y;
    }
    else if (motion.jitterConvention.KnownPart()->value != C::JitterConvention::Unjittered)
        return Reject<C::Vec2>("CTX.UnsupportedSemanticType");
    if (!Finite(value)) return Reject<C::Vec2>("CTX.MalformedObservation");
    return C::OptionalFact<C::Vec2>::FromKnown(value, motion.jitterConvention.KnownPart()->evidence);
}
inline C::OptionalFact<double> RawDepth(double value, const C::DepthDescription& depth)
{
    if (!std::isfinite(value) || !Established(depth.kind) || depth.kind.KnownPart()->value != C::DepthKind::Device ||
        value < 0 || value > 1) return Reject<double>("CTX.UnsupportedSemanticType");
    return C::OptionalFact<double>::FromKnown(value, depth.kind.KnownPart()->evidence);
}
inline C::OptionalFact<double> DeviceToViewDepth(double value, const C::DepthDescription& depth)
{
    if (!RawDepth(value, depth).IsKnown() || !Established(depth.projection) ||
        !Established(depth.projectionConvention) || depth.projection.KnownPart()->value.Size() != 16)
        return Reject<double>("CTX.MissingProjection");
    // The frozen analytic tag is an explicit alias of this declared row-major convention.
    const auto convention=depth.projectionConvention.KnownPart()->value.View();
    if (convention!="projection.row-major.device-0-1.positive-z" && convention!="test.perspective.device-0-1.positive-z")
        return Reject<double>("CTX.UnsupportedSemanticType");
    const auto& p = depth.projection.KnownPart()->value;
    for (const double v : p) if (!std::isfinite(v)) return Reject<double>("CTX.MalformedObservation");
    // d = (P22*z + P23)/(P32*z + P33). Reversed-Z never selects a universal 1-d rule.
    if (*p.Get(8) != 0 || *p.Get(9) != 0 || *p.Get(12) != 0 || *p.Get(13) != 0)
        return Reject<double>("CTX.UnsupportedSemanticType");
    const double divisor = value * *p.Get(14) - *p.Get(10);
    if (divisor == 0) return Reject<double>("CTX.MissingProjection");
    const double result = (*p.Get(11) - value * *p.Get(15)) / divisor;
    if (!std::isfinite(result) || result <= 0) return Reject<double>("CTX.MalformedObservation");
    return C::OptionalFact<double>::FromKnown(result, depth.projection.KnownPart()->evidence);
}
inline C::OptionalFact<double> UndoPreExposure(double value, const C::ExposureDescription& exposure)
{
    if (!std::isfinite(value) || !Established(exposure.preExposure) ||
        !Established(exposure.preExposureRelation) ||
        exposure.preExposureRelation.KnownPart()->value.View() != "stored.preExposure.times.radiance")
        return Reject<double>("CTX.UnknownRequiredFact");
    const double factor = exposure.preExposure.KnownPart()->value;
    if (!std::isfinite(factor) || factor <= 0 || !std::isfinite(value / factor))
        return Reject<double>("CTX.MalformedObservation");
    return C::OptionalFact<double>::FromKnown(value / factor, exposure.preExposure.KnownPart()->evidence);
}
inline C::OptionalFact<double> ApplyExposure(double value, const C::ExposureDescription& exposure)
{
    if (!std::isfinite(value) || !Established(exposure.effectiveExposure))
        return Reject<double>("CTX.UnknownRequiredFact");
    const double factor = exposure.effectiveExposure.KnownPart()->value;
    if (!std::isfinite(factor) || factor < 0 || !std::isfinite(value * factor))
        return Reject<double>("CTX.MalformedObservation");
    return C::OptionalFact<double>::FromKnown(value * factor, exposure.effectiveExposure.KnownPart()->evidence);
}
} // namespace Neurotic::Context
