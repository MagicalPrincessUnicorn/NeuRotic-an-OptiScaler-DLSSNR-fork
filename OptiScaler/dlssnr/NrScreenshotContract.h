#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace DlssNr::Screenshots
{
inline bool IsPresentRoute(unsigned int route) { return route == 1 || route == 2; }
inline bool NativePairAvailable(unsigned int route, bool enabled, bool beforeSr,
                                bool rayReconstruction, bool performanceBackend)
{
    return route == 0 && enabled && (!beforeSr || rayReconstruction || performanceBackend);
}
inline const char* RouteName(unsigned int route)
{
    return route == 0 ? "Native Temporal" : route == 1 ? "Present Image Only" :
           route == 2 ? "Present Enhanced" : "Unavailable";
}
struct Identity
{
    unsigned int route = 0;
    uint64_t providerFrame = 0;
    uint64_t providerGeneration = 0;
    uint64_t resourceGeneration = 0;
    unsigned int backbuffer = 0;
};
inline std::string JsonString(std::string_view value)
{
    static constexpr char hex[] = "0123456789abcdef";
    std::string result = "\"";
    for (unsigned char c : value)
    {
        if (c == '"' || c == '\\') { result += '\\'; result += char(c); }
        else if (c < 0x20)
        {
            result += "\\u00"; result += hex[c >> 4]; result += hex[c & 15];
        }
        else result += char(c);
    }
    return result + '"';
}
}
