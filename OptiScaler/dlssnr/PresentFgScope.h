#pragma once
#include <cstdint>
namespace DlssNr::PreFg::FinalColor
{
struct Scope
{
    uint64_t provider = 0, epoch = 0, viewport = 0, resume = 0, route = 0;
    bool operator==(const Scope& b) const
    {
        return provider == b.provider && epoch == b.epoch && viewport == b.viewport &&
            resume == b.resume && route == b.route;
    }
};
}
