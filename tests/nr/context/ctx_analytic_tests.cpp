#include "../oracles/Test002AnalyticCases.h"
#if __has_include(<nr/context/SemanticMath.h>)
#include <nr/context/SemanticMath.h>
#define NR_CTX_IMPLEMENTED 1
#endif
using namespace NrTest002;
static Analytic::Observed Probe(const Analytic::Input& in)
{
    Analytic::Observed out;
#ifdef NR_CTX_IMPLEMENTED
    namespace S = Neurotic::Context;
    switch (in.operation)
    {
    case Analytic::Operation::MotionToUv: out.vector = S::MotionToUv(in.vector, in.motion); break;
    case Analytic::Operation::Unjitter: out.vector = S::Unjitter(in.vector, in.motion); break;
    case Analytic::Operation::RasterPointToUv: out.vector = S::RasterPointToUv(in.vector, in.raster); break;
    case Analytic::Operation::DeviceToViewDepth: out.scalar = S::DeviceToViewDepth(in.scalar, in.depth); break;
    case Analytic::Operation::RawDepth: out.scalar = S::RawDepth(in.scalar, in.depth); break;
    case Analytic::Operation::UndoPreExposure: out.scalar = S::UndoPreExposure(in.scalar, in.exposure); break;
    case Analytic::Operation::ApplyExposure: out.scalar = S::ApplyExposure(in.scalar, in.exposure); break;
    }
#else
    (void)in; // No production callable yet: positive Known oracle cases must fail.
#endif
    return out;
}
int main(int argc, char** argv)
{
    return Guard([&] {
        Report report("ctx-analytic", argc == 2 ? std::string_view(argv[1]) : std::string_view{});
        for (const auto& test : Analytic::Cases()) Analytic::Compare(report, test, Probe);
        auto in=Analytic::MotionInput();in.motion.scale=Known(C::Vec2{2,3});in.vector={8,-4};
        Analytic::Compare(report,{"T11.scale-exactly-once","(8*2/800,-4*3/400)=(.02,-.03)",in,{true,true,{.02,-.03},0}},Probe);
        in.vector={std::numeric_limits<double>::infinity(),0};
        Analytic::Compare(report,{"T11.nonfinite-motion","nonfinite source has no numeric meaning",in,{true,false,{},0}},Probe);
        in=Analytic::MotionInput();in.motion.currentRaster.active=Known(C::Rectangle{0,0,0,400});in.vector={8,-4};
        Analytic::Compare(report,{"T11.zero-active-extent","zero width cannot be a denominator",in,{true,false,{},0}},Probe);
        in=Analytic::MotionInput();in.operation=Analytic::Operation::Unjitter;
        in.motion.units=Known(C::MotionUnits::ActiveRasterUV);in.motion.currentRaster.origin={};
        Analytic::Compare(report,{"T11.unjitter-missing-basis","missing current basis cannot establish canonical UV",in,{true,false,{},0}},Probe);
        in.motion.currentRaster=Analytic::MotionInput().motion.currentRaster;
        in.motion.previousRaster.axisSigns=Known(C::Vec2{1,-1});
        Analytic::Compare(report,{"T11.unjitter-opposed-basis","two different axis conventions are not canonical jitter samples",in,{true,false,{},0}},Probe);
        in=Analytic::Cases().at(10).input;
        in.depth.projectionConvention=Known(Sym("projection.row-major.device-0-1.positive-z"));
        Analytic::Compare(report,{"T12.production-projection-convention","declared row-major projection d=1.25-2.5/z at .75 gives z=5",in,{false,true,{},5}},Probe);
        return report.Finish();
    });
}
