#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace DlssNr::VkTuning
{
struct Settings
{
    uint32_t preset = 0, style = 0;
    float intensity = 1, structure = 1, tone = 1, skin = -1;
    bool mask = true;
    bool operator==(const Settings&) const = default;
    bool Valid() const
    {
        return preset <= 3 && style <= 2 && std::isfinite(intensity) && intensity >= 0 && intensity <= 2 &&
               std::isfinite(structure) && structure >= 0 && structure <= 2 &&
               std::isfinite(tone) && tone >= 0 && tone <= 2 && std::isfinite(skin) && skin >= -1 && skin <= 2;
    }
};

// Readback validates the typed interface, not whether the proprietary model consumes the value.
template<class Params, class T> bool WriteChecked(Params* params, const char* key, T value)
{
    params->Set(key, value);
    T read{};
    return static_cast<unsigned int>(params->Get(key, &read)) == 1 && read == value;
}

template<class Params> bool Prepare(Params* p, const Settings& s, uint32_t width, uint32_t height,
                                   const char** failedKey = nullptr)
{
    if (!p || !s.Valid()) return false;
    bool ok = true;
    const auto put = [&](const char* key, auto value) {
        if (!WriteChecked(p, key, value))
        {
            if (ok && failedKey) *failedKey = key;
            ok = false;
        }
    };
    put("DLSSNR.Enabled", 1u);
    put("DLSSNR.Width", width); put("DLSSNR.Height", height);
    put("CreationNodeMask", 1u); put("VisibilityNodeMask", 1u);
    put("DLSSNR.Hint.Render.Preset", s.preset); put("DLSSNR.Style", s.style);
    put("DLSSNR.Intensity", s.intensity); put("DLSSNR.LocalStructureStrength", s.structure);
    put("DLSSNR.LocalToneStrength", s.tone); put("DLSSNR.SkinStructureStrength", s.skin);
    put("DLSSNR.UseAutoMask", s.mask ? 1u : 0u); put("DLSSNR.UICorrection", 1u);
    return ok;
}

enum class Result { Applied, Full, Failed, Invalid };
template<class Params> struct Cache
{
    static constexpr size_t Capacity = 8;
    struct Entry
    {
        bool used = false, ready = false;
        Settings settings{};
        void* feature = nullptr;
        Params* params = nullptr;
    };
    std::array<Entry, Capacity> entries{};
    int active = -1;
    size_t size = 0;
    bool changed = false;

    // Failed attempts retain their allocations: vendor creation may have recorded work even on failure.
    template<class Create> Result Select(const Settings& requested, Create create)
    {
        changed = false;
        if (!requested.Valid()) return Result::Invalid;
        for (size_t i = 0; i < size; ++i)
        {
            if (entries[i].settings == requested)
            {
                if (!entries[i].ready) return Result::Failed;
                changed = active != static_cast<int>(i);
                active = static_cast<int>(i);
                return Result::Applied;
            }
        }
        if (size == Capacity) return Result::Full;
        const size_t i = size++;
        auto& entry = entries[i];
        entry.used = true;
        entry.settings = requested;
        entry.ready = create(entry);
        if (!entry.ready) return Result::Failed;
        active = static_cast<int>(i);
        changed = true;
        return Result::Applied;
    }
};
}
