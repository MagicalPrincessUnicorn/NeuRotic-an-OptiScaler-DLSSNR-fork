#include <dlssnr/NativeTemporalInputs.h>
#include <cassert>
#include <cstdio>
#include <limits>
#include <menu/UiBrightness.h>

int main()
{
    for (unsigned bits = 0; bits != 16; ++bits)
        assert(DlssNr::NativeTemporalInputs::Authoritative(bits & 1, bits & 2, bits & 4, bits & 8) == (bits == 7));
    using namespace DlssNr::NativeTemporalInputs;
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
    for (unsigned alpha = 0; alpha != 256; ++alpha)
        for (unsigned component = 0; component != 256; ++component)
        {
            const uint32_t pixel = alpha << 24 | component << 16 | component << 8 | component;
            assert(Neurotic::UiBrightness::Apply(pixel, 1) == pixel);
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
