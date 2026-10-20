#pragma once
#include <cstdint>
#include <filesystem>

namespace DlssNr {
// Cache immutable export discovery only. Creation/evaluation still perform the
// forwarder's live provider identity check, and capability parameters stay live.
// Serialized by the renderer's lifecycle/model locks; never caches a refusal.
class NrProviderProbeCache {
    bool ready_ = false;
    uint64_t lifecycle_ = 0, resume_ = 0;
    const void* module_ = nullptr;
    const void* device_ = nullptr;
    std::filesystem::path path_;
public:
    void Reset() noexcept { ready_ = false; }
    template<class Probe>
    int Resolve(uint64_t lifecycle, uint64_t resume, const void* module,
                const void* device, const std::filesystem::path& path, Probe probe) {
        if (ready_ && lifecycle_ == lifecycle && resume_ == resume &&
            module_ == module && device_ == device && path_ == path) return 31;
        ready_ = false;
        const int mask = probe();
        if (mask == 31 && module && device && !path.empty()) {
            path_ = path; lifecycle_ = lifecycle; resume_ = resume;
            module_ = module; device_ = device; ready_ = true;
        }
        return mask;
    }
};
}
