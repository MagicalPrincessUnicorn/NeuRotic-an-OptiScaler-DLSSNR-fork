// NeuRotic host adapter. No ReShade dependency or addon registration.
#pragma once
#ifdef NR_EXPERIMENTAL_MFG_TEST
#include <string>
#include <vector>
#endif
namespace reshade::log {
enum class level { info, warning, error };
#ifdef NR_EXPERIMENTAL_MFG_TEST
inline std::vector<std::string> lines;
#endif
inline void message(level severity, const char* text) {
#ifdef NR_EXPERIMENTAL_MFG_TEST
    (void)severity; lines.emplace_back(text);
#else
    if(severity==level::info) LOG_INFO("{}",text);
    else LOG_WARN("{}",text);
#endif
}
}
