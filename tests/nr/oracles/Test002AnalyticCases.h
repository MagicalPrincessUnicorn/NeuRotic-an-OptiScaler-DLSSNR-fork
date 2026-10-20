#pragma once
// Complete TEST-owned vectors and comparison port. No production transform is implemented here.
// CTX-001 is the later owner of normalization; the default runner deliberately binds no probe.
#include "../contracts/Test002Support.h"
#include <vector>
namespace NrTest002::Analytic
{
// Probe operation labels belong only to the test catalog, not to C01-C16 or a runtime ABI.
enum class Operation { MotionToUv, Unjitter, RasterPointToUv, DeviceToViewDepth, RawDepth, UndoPreExposure, ApplyExposure };
struct Input
{
    Operation operation = Operation::MotionToUv;
    C::MotionDescription motion;
    C::RasterDescription raster;
    C::DepthDescription depth;
    C::ExposureDescription exposure;
    C::Vec2 vector {};
    double scalar = 0;
};
struct Expected
{
    bool vectorResult = true;
    bool known = true;
    C::Vec2 vector {};
    double scalar = 0;
};
struct Case
{
    const char* id;
    const char* derivation;
    Input input;
    Expected expected;
};
struct Observed
{
    C::OptionalFact<C::Vec2> vector;
    C::OptionalFact<double> scalar;
};
// Adapter must call the later production function as SUT. Expected values are NOT passed to it.
using Probe = Observed (*)(const Input&);
inline C::RasterDescription Raster(C::Rectangle active, C::Extent allocation)
{
    C::RasterDescription r;
    r.active = Known(active); r.allocation = Known(allocation);
    r.origin = Known(C::RasterOrigin::TopLeft); r.axisSigns = Known(C::Vec2{1, 1});
    r.pixelCenter = Known(C::Vec2{0.5, 0.5}); return r;
}
inline Input MotionInput()
{
    Input in;
    in.motion.units = Known(C::MotionUnits::Pixels);
    in.motion.direction = Known(C::MotionDirection::CurrentToPrevious);
    in.motion.jitterConvention = Known(C::JitterConvention::Unjittered);
    in.motion.scale = Known(C::Vec2{1, 1});
    in.motion.currentRaster = Raster({0, 0, 800, 400}, {1024, 512});
    in.motion.previousRaster = in.motion.currentRaster;
    in.motion.validCoverage = Known(C::Rectangle{0, 0, 800, 400});
    in.motion.framePair.current = Known(Id<C::BaseRealFrameId>("stream.main", 10));
    in.motion.framePair.previous = Known(Id<C::BaseRealFrameId>("stream.main", 9));
    if (!in.motion.framePair.associationEvidence.Push(Ev(81))) throw std::runtime_error("Pair evidence capacity");
    return in;
}
inline std::vector<Case> Cases()
{
    std::vector<Case> cases; cases.reserve(20);
    auto in = MotionInput(); in.vector = {8, -4};
    cases.push_back({"T11.oracle.pixel-to-uv", "(8/800,-4/400)=(0.01,-0.01); allocation is not denominator", in, {true, true, {0.01, -0.01}, 0}});
    in = MotionInput(); in.motion.units = Known(C::MotionUnits::NDC);
    in.motion.currentRaster.axisSigns = Known(C::Vec2{1, -1});
    in.motion.previousRaster.axisSigns = Known(C::Vec2{1, -1}); in.vector = {0.02, 0.02};
    cases.push_back({"T11.oracle.ndc-origin", "declared NDC x-right y-up: (dx/2,-dy/2)", in, {true, true, {0.01, -0.01}, 0}});
    in = MotionInput(); in.operation = Operation::Unjitter; in.motion.units = Known(C::MotionUnits::ActiveRasterUV);
    in.motion.jitterConvention = Known(C::JitterConvention::Jittered);
    in.motion.currentJitter = Known(C::Vec2{-0.0025, 0.0025});
    in.motion.previousJitter = Known(C::Vec2{0.0025, -0.0025}); in.vector = {0.02, -0.01};
    cases.push_back({"T11.oracle.jitter-sign", "m_u=m_j-(j_previous-j_current) in the same declared UV basis", in, {true, true, {0.015, -0.005}, 0}});
    in.motion.jitterConvention = Known(C::JitterConvention::Unjittered); in.vector = {0.015, -0.005};
    cases.push_back({"T11.oracle.no-double-jitter", "already unjittered displacement is unchanged", in, {true, true, {0.015, -0.005}, 0}});
    in.motion.jitterConvention = Known(C::JitterConvention::Jittered); in.motion.previousJitter = {};
    cases.push_back({"T11.oracle.missing-jitter", "required previous jitter is unavailable, not known zero", in, {true, false, {}, 0}});
    in = MotionInput(); in.motion.direction = Known(C::MotionDirection::PreviousToCurrent); in.vector = {8, -4};
    cases.push_back({"T11.oracle.forward-not-negation", "no inverse correspondence provided; forward flow cannot simply be negated at current pixel", in, {true, false, {}, 0}});
    in = MotionInput(); in.motion.framePair.previous = Known(Id<C::BaseRealFrameId>("other.stream", 9)); in.vector = {8, -4};
    cases.push_back({"T11.oracle.incompatible-pair", "independent stream without association is not the previous frame", in, {true, false, {}, 0}});
    in = MotionInput(); in.motion.framePair.current = Known(Id<C::BaseRealFrameId>("stream.main", 14));
    in.motion.framePair.previous = Known(Id<C::BaseRealFrameId>("stream.main", 10)); in.vector = {16, 0};
    cases.push_back({"T11.oracle.dropped-pair", "trusted explicit pair displacement 16/800; do not invent intermediate frames or divide by frame count", in, {true, true, {0.02, 0}, 0}});
    in = Input{}; in.operation = Operation::RasterPointToUv;
    in.raster = Raster({100, 50, 800, 400}, {1024, 512}); in.vector = {500, 250};
    cases.push_back({"T11.oracle.rect-offset", "continuous point coordinates: ((500-100)/800,(250-50)/400); no pixel-center addition", in, {true, true, {0.5, 0.5}, 0}});
    in.raster = Raster({100, 50, 400, 200}, {1024, 512}); in.vector = {300, 150};
    cases.push_back({"T11.oracle.drs", "changed active rect, unchanged allocation, center remains (0.5,0.5)", in, {true, true, {0.5, 0.5}, 0}});
    in = Input{}; in.operation = Operation::DeviceToViewDepth;
    in.depth.kind = Known(C::DepthKind::Device); in.depth.reversed = Known(false);
    in.depth.nearPlane = Known(2.0); in.depth.farPlane = Known(10.0);
    in.depth.projectionConvention = Known(Sym("test.perspective.device-0-1.positive-z"));
    // Fixture explicitly declares d=1.25-2.5/z, not a rule inferred from reversed-Z alone.
    C::BoundedList<double, 16> matrix;
    for (const double v : {1.,0.,0.,0., 0.,1.,0.,0., 0.,0.,1.25,-2.5, 0.,0.,1.,0.})
        if (!matrix.Push(v)) throw std::runtime_error("Projection literal capacity");
    in.depth.projection = Known(matrix); in.scalar = 0.75;
    cases.push_back({"T12.oracle.device-depth", "declared projection d=1.25-2.5/z; d=.75 implies z=5", in, {false, true, {}, 5}});
    in.depth.reversed = Known(true); in.scalar = 0.25;
    matrix = {};
    for (const double v : {1.,0.,0.,0., 0.,1.,0.,0., 0.,0.,-0.25,2.5, 0.,0.,1.,0.})
        if (!matrix.Push(v)) throw std::runtime_error("Reversed projection literal capacity");
    in.depth.projection = Known(matrix);
    cases.push_back({"T12.oracle.reversed-depth", "declared reversed d=-.25+2.5/z; d=.25 implies z=5, not linearized 1-d", in, {false, true, {}, 5}});
    in.depth.projection = {}; in.depth.nearPlane = {}; in.depth.farPlane = {}; in.depth.projectionConvention = {};
    cases.push_back({"T12.oracle.projection-unknown", "reversed metadata alone cannot establish view depth", in, {false, false, {}, 0}});
    in.operation = Operation::RawDepth; in.scalar = 0.25;
    cases.push_back({"T12.oracle.raw-depth-retained", "raw-depth consumer can retain .25 without invented near/far or view distance", in, {false, true, {}, 0.25}});
    in = Input{}; in.operation = Operation::UndoPreExposure;
    in.exposure.preExposure = Known(3.0); in.exposure.preExposureRelation = Known(Sym("stored.preExposure.times.radiance"));
    in.exposure.rawExposure = Known(2.0); in.exposure.effectiveExposure = Known(2.0);
    in.exposure.calibration = Known(5.0); in.scalar = 24;
    cases.push_back({"T12.oracle.pre-exposure", "stored=3*radiance; stored24 gives radiance8; calibration5 is not pre-exposure", in, {false, true, {}, 8}});
    in.operation = Operation::ApplyExposure; in.scalar = 8;
    cases.push_back({"T12.oracle.exposure-distinct", "already radiance8 times effective exposure2 =16; no second division by pre-exposure", in, {false, true, {}, 16}});
    in.operation = Operation::UndoPreExposure; in.scalar = 24; in.exposure.preExposure = {};
    cases.push_back({"T12.oracle.missing-pre-exposure", "missing multiplier is not neutral1", in, {false, false, {}, 0}});
    in.exposure.preExposure = Known(0.0);
    cases.push_back({"T12.oracle.zero-pre-exposure", "known zero is represented but cannot be a divisor", in, {false, false, {}, 0}});
    return cases;
}
inline void Compare(Report& report, const Case& c, Probe probe)
{
    if (!probe)
    {
        report.Pending(c.id, c.derivation, "NR-CTX-001 test-only binding; no production function supplied");
        return;
    }
    const auto actual = probe(c.input); // Expected is deliberately absent from this API.
    const bool known = c.expected.vectorResult ? actual.vector.IsKnown() : actual.scalar.IsKnown();
    bool matches = known == c.expected.known;
    if (matches && known)
    {
        if (c.expected.vectorResult)
        {
            const auto v = actual.vector.KnownPart()->value;
            matches = std::isfinite(v.x) && std::isfinite(v.y) &&
                std::abs(v.x-c.expected.vector.x) <= 1e-9 && std::abs(v.y-c.expected.vector.y) <= 1e-9;
        }
        else
        {
            const auto v = actual.scalar.KnownPart()->value;
            matches = std::isfinite(v) && std::abs(v-c.expected.scalar) <= 1e-9;
        }
    }
    // Numeric raw values, presence and independent target are emitted, including failed actuals.
    const auto actualText = !known ? "Unknown" : c.expected.vectorResult
        ? Number(actual.vector.KnownPart()->value.x)+","+Number(actual.vector.KnownPart()->value.y)
        : Number(actual.scalar.KnownPart()->value);
    const auto expectedText = !c.expected.known ? "Unknown" : c.expected.vectorResult
        ? Number(c.expected.vector.x)+","+Number(c.expected.vector.y) : Number(c.expected.scalar);
    report.Raw(c.id, actualText, expectedText, matches, "explicitly supplied production probe", c.derivation);
}
} // namespace NrTest002::Analytic
