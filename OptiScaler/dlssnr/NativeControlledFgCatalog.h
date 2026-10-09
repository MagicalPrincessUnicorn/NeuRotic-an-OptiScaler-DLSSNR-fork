#pragma once
#include <string_view>
namespace DlssNr {
// Exact SDK binary from the supervised controlled-C-facade full-v1 build.
// Catalog membership is necessary, not sufficient: live context/allocation,
// action, actual recipe and complete release evidence are checked separately.
inline constexpr std::string_view NativeControlledFgSdkSha256=
    "2b2077fd916793dd3ea0e72fc63bdb70e58afcdb650795f06888e470f0160cc5";
inline constexpr unsigned long long NativeControlledFgSdkBytes=6768640;
}
