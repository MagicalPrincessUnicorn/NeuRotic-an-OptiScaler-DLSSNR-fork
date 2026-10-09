#pragma once
#include <chrono>
namespace DlssNr::NativeGuides {
inline double NowMs() noexcept {
    return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
}
