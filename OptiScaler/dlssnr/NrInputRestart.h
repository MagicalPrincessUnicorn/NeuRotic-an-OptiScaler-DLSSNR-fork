#pragma once
namespace DlssNr {
struct InputSelection { int source, transport; bool allowCpuFallback; };
constexpr bool InputRestartRequired(InputSelection requested, InputSelection process) noexcept {
    return requested.source != process.source || requested.transport != process.transport;
}
}
