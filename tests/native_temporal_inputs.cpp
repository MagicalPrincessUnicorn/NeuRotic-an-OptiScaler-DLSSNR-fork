#include <dlssnr/NativeTemporalInputs.h>
#include <cassert>
#include <cstdio>
#include <limits>
#include <menu/UiBrightness.h>

int main()
{
    if (Neurotic::UiBrightness::Apply(0xa5402010u, 0.25f) != 0xa5100804u)
    {
        std::puts("FAIL: quarter UI brightness must dim RGB while preserving alpha");
        return 1;
    }
    for (unsigned bits = 0; bits != 16; ++bits)
        assert(DlssNr::NativeTemporalInputs::Authoritative(bits & 1, bits & 2, bits & 4, bits & 8) == (bits == 7));
    using namespace DlssNr::NativeTemporalInputs;
    OutputEvaluation evaluation;
    assert(!evaluation.Succeeded() && !evaluation.RayReconstructed()); // API success alone has no effect
    unsigned calls=0;
    assert(!evaluation.Invoke(true,[&]{++calls;return false;}));
    assert(calls==1 && !evaluation.Succeeded() && !evaluation.RayReconstructed());
    assert(evaluation.Invoke(false,[&]{++calls;return true;}));
    assert(calls==2 && evaluation.Succeeded() && !evaluation.RayReconstructed());
    assert(evaluation.Invoke(true,[&]{++calls;return true;}));
    assert(calls==3 && evaluation.Succeeded() && evaluation.RayReconstructed());
    struct CreationParams {
        unsigned flags=0; bool known=true;
        int Get(std::string_view key,unsigned* value) {
            if(key=="DLSS.Feature.Create.Flags") {if(!known)return -1;*value=flags;return 0;}
            if(key=="OutWidth"){*value=3840;return 0;}
            if(key=="OutHeight"){*value=2160;return 0;}
            return -1;
        }
    } creation;
    auto captured=DlssNr::NativeNgxCreationParameters::Capture(&creation,0);
    const auto zero=FromCreation(captured,7,9);
    assert(zero && zero->flags==0 && zero->outputWidth==3840 && zero->outputHeight==2160);
    creation.flags=0x8fu;
    captured=DlssNr::NativeNgxCreationParameters::Capture(&creation,0);
    assert(FromCreation(captured,7,9)->flags==0x8fu);
    assert(!FromCreation(captured,0,9) && !FromCreation(captured,7,0));
    creation.known=false;
    assert(!FromCreation(DlssNr::NativeNgxCreationParameters::Capture(&creation,0),7,9));
    assert(!FromCreation({},7,9));
    assert(ResolveFlags(0x8fu, std::nullopt) == 0x8fu);
    assert(ResolveFlags(0x8fu, 0u) == 0x8fu);
    assert(ResolveFlags(0u, 0x8fu) == 0u);
    assert(ResolveFlags(std::nullopt, 0x8fu) == 0x8fu);
    const auto low = GuideRect(0, 0, 1280, 720, 1920, 1080, true);
    const auto high = GuideRect(16, 8, 1280, 720, 1920, 1080, false);
    assert(low.width == 1280 && low.height == 720 && Fits(low, 1280, 720));
    assert(high.x == 16 && high.y == 8 && high.width == 1920 && high.height == 1080);
    assert(Fits(high, 1936, 1088) && !Fits(high, 1920, 1080));
    assert(!Fits({0, 0, 0, 720}, 1280, 720));
    assert(!Fits({0xffffffffu, 0, 100, 100}, 1920, 1080));
    assert(!Fits({1, 0, 0xffffffffu, 100}, 1920, 1080));
    assert(Fits({0xffffffffu, 0, 2, 1}, 0x100000001ull, 1));
    assert(!Fits({0xffffffffu, 0, 2, 1}, 0x100000000ull, 1));
    namespace Geometry=Neurotic::Context::GuideGeometry;
    const auto complete=Geometry::ClampCompleteRequest({0,0},{1920,1080},{1280,720});
    assert(complete.width==1280&&complete.height==720&&Geometry::Fits(complete,{1280,720}));
    for(const auto requested:{Geometry::Extent32{0,600},Geometry::Extent32{800,0},Geometry::Extent32{0,0}})
    {
        const auto partial=Geometry::ClampCompleteRequest({0,0},requested,{1280,720});
        assert(partial.width==1280&&partial.height==720);
    }
    const auto origin=Geometry::ClampCompleteRequest({16,8},{1920,1080},{1920,1080});
    assert(origin.width==1920&&origin.height==1080&&!Geometry::Fits(origin,{1920,1080}));
    const auto independent=Geometry::Project({4,6},{2,3},{100,80},{200,160},false);
    assert(independent.depth.x==4&&independent.depth.y==6&&independent.depth.width==100&&independent.depth.height==80);
    assert(independent.motion.x==2&&independent.motion.y==3&&independent.motion.width==200&&independent.motion.height==160);
    for (unsigned alpha = 0; alpha != 256; ++alpha)
        for (unsigned component = 0; component != 256; ++component)
        {
            const uint32_t pixel = alpha << 24 | component << 16 | component << 8 | component;
            assert(Neurotic::UiBrightness::Apply(pixel, 1) == pixel);
            for (float gain : {0.25f, 0.5f, 0.75f})
            {
                const auto adjusted = Neurotic::UiBrightness::Apply(pixel, gain);
                assert((adjusted >> 24) == alpha);
                assert((adjusted & 255u) <= component);
                assert((adjusted & 255u) == ((adjusted >> 8) & 255u));
                assert((adjusted & 255u) == ((adjusted >> 16) & 255u));
            }
            for (float gain : {1.5f, 2.0f, 3.0f})
            {
                const auto adjusted = Neurotic::UiBrightness::Apply(pixel, gain);
                assert((adjusted >> 24) == alpha);
                assert((adjusted & 255u) >= component);
            }
            assert(Neurotic::UiBrightness::Apply(pixel, std::numeric_limits<float>::quiet_NaN()) == pixel);
        }
    std::puts("PASS native authority, immutable flags, low/high guide geometry, bounds and UI brightness/alpha invariants");
}
