#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <utility>

namespace DlssNr
{
// Discovery only: a known path is never evidence of provider capability.
// The owner serializes this with its model/lifecycle locks.
class NrProviderPathCache
{
    bool checked_ = false;
    uint64_t lifecycle_ = 0, resume_ = 0;
    std::optional<std::filesystem::path> path_;
public:
    void Reset() { checked_ = false; path_.reset(); }
    template<class Search>
    const std::optional<std::filesystem::path>& Resolve(uint64_t lifecycle, uint64_t resume, Search search)
    {
        if (!checked_ || lifecycle != lifecycle_ || resume != resume_)
        {
            auto found = search();
            path_ = std::move(found);
            lifecycle_ = lifecycle; resume_ = resume; checked_ = true;
        }
        return path_;
    }
};
}
