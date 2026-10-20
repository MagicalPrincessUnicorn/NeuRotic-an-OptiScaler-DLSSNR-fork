#include "../../apps/NeuRoticWindowWorker/nr/NrPolicy.h"
#include <cmath>
#include <fstream>
#include <iostream>
int main()
{
    int failures = 0, checks = 0;
    auto check = [&](bool v, const char *n) {
        ++checks;
        if (!v)
        {
            ++failures;
            std::cerr << "FAIL " << n << '\n';
        }
    };
    nrw::NrOptions o;
    o.modelPath = L"C:\\Models\\nvngx_dlssnr.dll";
    o.forwarderPath = L"C:\\Worker\\nvngx.dll_dlssnr.dll";
    std::string reason;
    check(nrw::detail::ValidOptions(o, reason), "explicit baseline options accepted");
    check(nrw::detail::WorkingExtent(o,3839,2159)==std::pair<uint32_t,uint32_t>{3839,2159},"100 percent preserves exact native odd extent");
    o.modelStyle=2;check(nrw::detail::ValidOptions(o,reason),"Cinematic provider style admitted");
    o.modelStyle=3;check(!nrw::detail::ValidOptions(o,reason),"Unknown provider style refused");o.modelStyle=0;
    o.nrScalePercent=50;
    check(nrw::detail::WorkingExtent(o,3840,2160)==std::pair<uint32_t,uint32_t>{1920,1080},"half-size NR processes one quarter of 4K pixels");
    check(nrw::detail::WorkingExtent(o,1920,1080)==std::pair<uint32_t,uint32_t>{960,544},"resolution follows resize with Present alignment");
    o.nrScalePercent=67;
    check(nrw::detail::WorkingExtent(o,3840,2160)==std::pair<uint32_t,uint32_t>{2576,1448},"balanced uses the same aligned Present resolution policy");
    o.nrScalePercent=24;check(!nrw::detail::ValidOptions(o,reason),"undersized scale refused");
    o.nrScalePercent=101;check(!nrw::detail::ValidOptions(o,reason),"supersampling outside Anything scope refused");
    o.nrScalePercent=50;o.workWidth=1280;o.workHeight=720;check(!nrw::detail::ValidOptions(o,reason),"conflicting dimensions refused");
    o.nrScalePercent=100;o.workWidth=o.workHeight=0;
    o.transferStrength = NAN;
    check(!nrw::detail::ValidOptions(o, reason), "nonfinite strength refused");
    o.transferStrength = 1;
    o.workWidth = 640;
    check(!nrw::detail::ValidOptions(o, reason), "partial work shape refused");
    o.workWidth = 0;
    nrw::FrameStamp f{2, 3, 5, 2, 2, false}, previous{2, 2, 4, 2, 2, false};
    check(!nrw::detail::ResetHistory(f, previous), "contiguous history retained");
    auto cut = f;
    cut.session++;
    check(nrw::detail::ResetHistory(cut, previous), "session change resets");
    cut = f;
    cut.sequence += 2;
    check(nrw::detail::ResetHistory(cut, previous), "dropped frame resets");
    cut = f;
    cut.width++;
    check(nrw::detail::ResetHistory(cut, previous), "resize resets");
    cut = f;
    cut.reset = true;
    check(nrw::detail::ResetHistory(cut, previous), "explicit reset retained");
    nrw::GuideResult g;
    g.stamp = f;
    g.width = g.height = 2;
    g.depthCompleted = g.motionCompleted = true;
    g.depth = {.1f, .2f, .3f, .4f};
    g.motionXY.resize(8);
    check(!nrw::detail::UseGuides(o, f, &g), "off ignores estimates");
    o.guides = nrw::GuideMode::Shadow;
    check(!nrw::detail::UseGuides(o, f, &g), "shadow ignores estimates");
    o.guides = nrw::GuideMode::Experimental;
    check(nrw::detail::UseGuides(o, f, &g), "explicit matching estimates accepted");
    for(auto member:{&nrw::FrameStamp::streamEpoch,&nrw::FrameStamp::geometryEpoch,&nrw::FrameStamp::configEpoch}){g.stamp=f;++(g.stamp.*member);check(!nrw::detail::UseGuides(o,f,&g),"same-size epoch mismatch refuses guide");}
    g.stamp=f;
    g.stamp.sequence--;
    check(!nrw::detail::UseGuides(o, f, &g), "stale estimates refused");
    g.stamp = f;
    g.stamp.timestampQpc++;
    check(!nrw::detail::UseGuides(o, f, &g), "wrong timestamp refused");
    g.stamp = f;
    g.depth[0] = NAN;
    check(!nrw::detail::UseGuides(o, f, &g), "nan estimates refused");
    g.depth[0] = .1f;
    g.motionXY.pop_back();
    check(!nrw::detail::UseGuides(o, f, &g), "partial vector extent refused");
    g.motionXY.resize(8);
    g.estimated = false;
    check(!nrw::detail::UseGuides(o, f, &g), "invented native provenance refused");
    f.width = 1920;
    f.height = 1080;
    g.stamp = f;
    g.width = 518;
    g.height = 294;
    g.estimated = true;
    g.depth.assign(518 * 294, 32.0f);
    g.motionXY.assign(518 * 294 * 2, 1.0f);
    check(nrw::detail::UseGuides(o, f, &g), "actual depth producer 518x294 accepted for 1920x1080 capture");
    o.workWidth = 1280;
    o.workHeight = 720;
    check(nrw::detail::UseGuides(o, f, &g), "guide raster remains independent of downsampled model shape");
    g.depthCompleted = false;
    check(!nrw::detail::UseGuides(o, f, &g), "motion-only first frame remains baseline");
    check(nrw::detail::AdmitShape(1280, 720, 640, 360, reason), "ordinary owned transport fits budget");
    check(nrw::detail::AdmitShape(3840, 2160, 3840, 2160, reason),
          "4K full-size owned transport fits budget");
    check(!nrw::detail::AdmitShape(16384, 16384, 16384, 16384, reason),
          "oversized resource closure rejected before allocation");
    check(!nrw::detail::AdmitShape(7680, 4320, 7680, 4320, reason),
          "8K full-size resource closure exceeds budget");
    check(!nrw::detail::AdmitShape(960, 540, 1280, 720, reason),
          "work shape outside resized capture refused");
    auto fixture = std::filesystem::absolute(L"builds/window-worker/nr/core-fixture");
    std::filesystem::create_directories(fixture);
    check(nrw::detail::CoreAtRoot(fixture).empty(), "missing installed core has no arbitrary DLL fallback");
    {
        std::ofstream stream(fixture / L"nvngx.dll");
        stream << "owned test fixture, never loaded";
    }
    check(nrw::detail::CoreAtRoot(fixture) == fixture / L"nvngx.dll",
          "installed nvngx.dll filename is admitted");
    {
        std::ofstream stream(fixture / L"_nvngx.dll");
        stream << "owned test fixture, never loaded";
    }
    check(nrw::detail::CoreAtRoot(fixture) == fixture / L"nvngx.dll",
          "deterministic driver-core filename selection");
    std::filesystem::remove(fixture / L"nvngx.dll");
    check(nrw::detail::CoreAtRoot(fixture) == fixture / L"_nvngx.dll",
          "installed legacy _nvngx.dll fallback is admitted within same root");
    std::filesystem::remove(fixture / L"_nvngx.dll");
    std::filesystem::remove(fixture);
    std::cout << checks - failures << '/' << checks << " checks passed\n";
    return failures ? 1 : 0;
}
