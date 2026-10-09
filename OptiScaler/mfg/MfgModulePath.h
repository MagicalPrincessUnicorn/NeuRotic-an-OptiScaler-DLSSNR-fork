#pragma once

#include <cwchar>

namespace Neurotic::Mfg
{
constexpr wchar_t MfgLower(wchar_t c) noexcept
{
    return c >= L'A' && c <= L'Z' ? c + (L'a' - L'A') : c;
}

constexpr bool MfgPathCharEqual(wchar_t a, wchar_t b) noexcept
{
    if ((a == L'\\' || a == L'/') && (b == L'\\' || b == L'/')) return true;
    return MfgLower(a) == MfgLower(b);
}

inline bool MfgPathContains(const wchar_t* path, const wchar_t* pattern) noexcept
{
    if (!path || !pattern) return false;
    for (const wchar_t* at = path; *at; ++at)
    {
        const wchar_t* a = at;
        const wchar_t* b = pattern;
        while (*a && *b && MfgPathCharEqual(*a, *b)) { ++a; ++b; }
        if (!*b) return true;
    }
    return false;
}

inline bool IsMfgWrapperPath(const wchar_t* path) noexcept
{
    if (!path) return false;
    const wchar_t* leaf = path;
    for (const wchar_t* at = path; *at; ++at)
        if (*at == L'\\' || *at == L'/') leaf = at + 1;
    if (_wcsicmp(leaf, L"sl.dlss_g.dll") == 0) return true;
    const size_t leafLength = std::wcslen(leaf);
    if (leafLength < 5 || _wcsicmp(leaf + leafLength - 4, L".dll") != 0) return false;
    return MfgPathContains(path, L"\\NVIDIA\\NGX\\models\\sl_dlss_g_override_") &&
        MfgPathContains(path, L"\\versions\\") && MfgPathContains(path, L"\\files\\");
}
}
