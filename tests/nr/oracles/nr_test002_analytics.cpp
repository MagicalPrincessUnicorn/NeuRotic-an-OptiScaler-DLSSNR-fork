#include "Test002AnalyticCases.h"
#include <dlssnr/NativeTemporalInputs.h>
#ifdef _MSC_VER
#pragma warning(push)
// The inherited GPU constant-buffer structure intentionally has explicit alignment.
#pragma warning(disable: 4324)
#endif
#include <shaders/dlssnr/DlssNr_Common.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif
using namespace NrTest002;
int main(int argc, char** argv)
{
    return Guard([&] {
        Report r("raster-input-oracles", argc == 2 ? std::string_view(argv[1]) : std::string_view{});
        namespace N = DlssNr::NativeTemporalInputs;
        const auto scale = DlssNrWorkingMotionScale(3000, 2000, 1000, 500);
        r.Near("T11.scale.x", scale.x, 1.0/3.0, 2e-7, "DlssNrWorkingMotionScale");
        r.Near("T11.scale.y", scale.y, 0.25, 2e-7, "DlssNrWorkingMotionScale");
        const auto identity = DlssNrWorkingMotionScale(800, 400, 800, 400);
        r.Near("T11.scale.identity-x", identity.x, 1, 0, "DlssNrWorkingMotionScale");
        const auto zero = DlssNrWorkingMotionScale(0, 0, 100, 100);
        r.Near("T11.legacy.zero-fallback", zero.x, 1, 0, "DlssNrWorkingMotionScale legacy-only behavior");
        r.Check("T11.fits.offset", N::Fits({100, 50, 800, 400}, 1024, 512), true, "NativeTemporalInputs::Fits");
        r.Check("T11.fits.outside", N::Fits({300, 50, 800, 400}, 1024, 512), false, "NativeTemporalInputs::Fits");
        r.Check("T11.fits.zero", N::Fits({0, 0, 0, 400}, 1024, 512), false, "NativeTemporalInputs::Fits");
        r.Check("T11.fits.overflow", N::Fits({4294967295U, 0, 2, 1}, 4294967295ULL, 1), false, "NativeTemporalInputs::Fits");
        const auto low = N::GuideRect(100, 50, 800, 400, 1920, 1080, true);
        const auto high = N::GuideRect(100, 50, 800, 400, 1920, 1080, false);
        r.Check("T11.guide.low", low.x == 100 && low.y == 50 && low.width == 800 && low.height == 400, true, "NativeTemporalInputs::GuideRect");
        r.Check("T11.guide.high", high.x == 100 && high.y == 50 && high.width == 1920 && high.height == 1080, true, "NativeTemporalInputs::GuideRect");
        r.Equal("T04.legacy.flag-precedence", N::ResolveFlags(0U, 7U), 0, "NativeTemporalInputs::ResolveFlags legacy-only behavior");
        C::RasterDescription raster;
        raster.allocation = Known(C::Extent{1024, 512}); raster.active = Known(C::Rectangle{100, 50, 800, 400});
        r.Error("T11.raster.valid", raster.Check(), C::Error::None, "RasterDescription::Check");
        raster.active = Known(C::Rectangle{300, 50, 800, 400});
        r.Error("T11.raster.bounds", raster.Check(), C::Error::Malformed, "RasterDescription::Check");
        raster.active = Known(C::Rectangle{0, 0, 0, 400});
        r.Error("T11.raster.empty-descriptive", raster.Check(), C::Error::None, "RasterDescription::Check descriptive-only");
        raster.allocation = {};
        const auto rasterWire = C::Codec::Encode(raster);
        const auto rasterRead = C::Codec::Decode<C::RasterDescription>(rasterWire.text);
        r.Check("T11.raster.allocation-unknown", rasterRead.record && !rasterRead.record->Value().allocation.IsKnown(), true,
                "Codec::Decode<RasterDescription>");
        C::DepthDescription depth;
        depth.kind = Known(C::DepthKind::Device); depth.reversed = Known(true);
        const auto depthText = C::Codec::Encode(depth);
        const auto depthRead = C::Codec::Decode<C::DepthDescription>(depthText.text);
        r.Check("T12.depth.no-guessed-projection", depthRead.record && !depthRead.record->Value().projection.IsKnown() &&
                !depthRead.record->Value().nearPlane.IsKnown() && !depthRead.record->Value().farPlane.IsKnown(), true,
                "Codec::Decode<DepthDescription>");
        r.Check("T12.depth.reversed-retained", depthRead.record && depthRead.record->Value().reversed.IsKnown() &&
                depthRead.record->Value().reversed.KnownPart()->value, true, "Codec::Decode<DepthDescription>");
        C::DepthKind kind = C::DepthKind::Device;
        r.Check("T12.depth.radial-kind", C::ParseEnum("RadialDistance", kind) && kind == C::DepthKind::RadialDistance, true, "ParseEnum<DepthKind>");
        r.Check("T12.depth.proxy-kind", C::ParseEnum("Proxy", kind) && kind == C::DepthKind::Proxy, true, "ParseEnum<DepthKind>");
        C::ExposureDescription exposure;
        exposure.rawExposure = Known(0.0); exposure.preExposure = {};
        const auto exp = C::Codec::Decode<C::ExposureDescription>(C::Codec::Encode(exposure).text);
        r.Check("T12.exposure.zero-vs-unknown", exp.record && exp.record->Value().rawExposure.IsKnown() &&
                exp.record->Value().rawExposure.KnownPart()->value == 0 && !exp.record->Value().preExposure.IsKnown(),
                true, "Codec::Decode<ExposureDescription>");
        // A null probe is deliberate, not a placeholder implementation. No CTX algorithm exists
        // in the accepted ARCH/LIFE candidates. Exact later-owner binding remains a gate obligation.
        constexpr Analytic::Probe productionProbe = nullptr;
        for (const auto& c : Analytic::Cases()) Analytic::Compare(r, c, productionProbe);
        return r.Finish();
    });
}
