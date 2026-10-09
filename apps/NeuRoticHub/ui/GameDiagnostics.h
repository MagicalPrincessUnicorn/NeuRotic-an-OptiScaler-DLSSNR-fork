#pragma once
#include "install/OperationController.h"
#include <cstddef>
namespace nh {
inline constexpr size_t GameLogByteLimit=256*1024;
struct GameDiagnosticsResult {std::string text,status;};
// Called on a worker. Reads the bounded native or prepared renderer log from
// fixed selected-game directories; receipt paths never choose the source.
GameDiagnosticsResult ReadGameDiagnostics(const std::string& executable,const Json& inspection);
}
