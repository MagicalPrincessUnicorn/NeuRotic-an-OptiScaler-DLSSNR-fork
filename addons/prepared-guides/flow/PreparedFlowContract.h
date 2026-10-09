#pragma once
#include "PreparedFlowD3D12.h"
#include <inputs/universal_feeder/PreparedGuideContract.h>

namespace Neurotic::PreparedFlow
{
// Update only flow-derived fields of the actual capture descriptor. The caller
// supplies device/producer/session identity and depth facts from their owners.
inline bool ApplyCompletedFlow(const Output& output, Neurotic::Feed::Prepared::Descriptor& descriptor)
{
    namespace P = Neurotic::Feed::Prepared;
    namespace C = Neurotic::Contracts;
    if (!output.motion || !output.distrust || !output.ownership || !output.producer.Ready() ||
        descriptor.capture != output.pair.capture || descriptor.previousCapture != output.pair.previousCapture ||
        descriptor.stream != output.pair.stream || descriptor.generation != output.pair.generation) return false;
    const auto shape = output.motion->GetDesc();
    descriptor.motion = {static_cast<std::uint32_t>(shape.Width), shape.Height, 0, 0,
        static_cast<std::uint32_t>(shape.Width), shape.Height};
    descriptor.distrust = descriptor.motion;
    descriptor.motionOrigin = C::SourceClass::Derived; descriptor.maskOrigin = C::SourceClass::Derived;
    descriptor.motionUnits = C::MotionUnits::Pixels;
    descriptor.motionDirection = C::MotionDirection::CurrentToPrevious;
    descriptor.motionGridWidth = descriptor.motionGridHeight = 1;
    descriptor.scaleX = descriptor.scaleY = 1;
    descriptor.maskCapture = output.pair.capture;
    descriptor.flags |= P::HasDistrust;
    if (output.explicitReset || output.sceneCut || output.warmup) descriptor.flags |= P::Reset;
    return true;
}
}
