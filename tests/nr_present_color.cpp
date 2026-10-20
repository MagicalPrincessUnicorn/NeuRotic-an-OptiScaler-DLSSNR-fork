#include "dlssnr/PresentColor.h"
#include <cassert>
#include <iostream>
using namespace DlssNr;
int main()
{
    auto& r = HdrObservation::Registry::Instance();
    int chain;
    auto s = r.Register(&chain, DXGI_FORMAT_R10G10B10A2_UNORM);
    assert(PresentColor::Select(s, s.format).profile == PresentColor::Profile::Sdr709);
    s = r.RecordColorSpace(&chain, DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020, S_OK);
    auto d = PresentColor::Select(s, s.format);
    assert(d.profile == PresentColor::Profile::Hdr10Pq2020 && d.hdr && d.whitePoint == 2.5f);
    assert(d.workingFormat == DXGI_FORMAT_R16G16B16A16_FLOAT && d.recipe == 1);
    const auto identity = s.identityGeneration;
    s = r.RecordColorSpace(&chain, s.colorSpace, S_OK);
    assert(s.identityGeneration == identity);
    s = r.RecordColorSpace(&chain, DXGI_COLOR_SPACE_CUSTOM, E_FAIL);
    assert(s.identityGeneration == identity && PresentColor::Select(s, s.format).Supported());
    assert(!PresentColor::Select(s, DXGI_FORMAT_R8G8B8A8_UNORM).Supported());
    assert(!PresentColor::Select(r.BeginResize(&chain), s.format).Supported());
    s = r.CompleteResize(&chain, S_OK, DXGI_FORMAT_R16G16B16A16_FLOAT);
    assert(!PresentColor::Select(s, s.format).Supported()); // explicit PQ is not linear float
    s = r.RecordColorSpace(&chain, DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709, S_OK);
    assert(PresentColor::Select(s, s.format).profile == PresentColor::Profile::ScRgb709);
    s = r.RecordColorSpace(&chain, DXGI_COLOR_SPACE_YCBCR_FULL_GHLG_TOPLEFT_P2020, S_OK);
    assert(!PresentColor::Select(s, s.format).Supported());
    r.Unregister(&chain);
    assert(!PresentColor::Select(r.Read(&chain), DXGI_FORMAT_R8G8B8A8_UNORM).Supported());
    s = r.Register(&chain, DXGI_FORMAT_UNKNOWN);
    assert(s.colorSpace == DXGI_COLOR_SPACE_CUSTOM && !PresentColor::Select(s, s.format).Supported());
    s = r.CompleteResize(&chain, S_OK, DXGI_FORMAT_R16G16B16A16_FLOAT);
    assert(!s.colorSpaceObserved && PresentColor::Select(s, s.format).profile == PresentColor::Profile::ScRgb709);
    s = r.CompleteResize(&chain, E_FAIL, DXGI_FORMAT_R8G8B8A8_UNORM);
    assert(PresentColor::Select(s, s.format).profile == PresentColor::Profile::ScRgb709);
    s = r.CompleteResize(&chain, S_OK, DXGI_FORMAT_R8G8B8A8_UNORM);
    assert(!s.colorSpaceObserved && PresentColor::Select(s, s.format).profile == PresentColor::Profile::Sdr709);
    r.Unregister(&chain);
    std::cout << "PASS: explicit color selection, defaults, contradictions, failed/repeated calls and resize\n";
}
