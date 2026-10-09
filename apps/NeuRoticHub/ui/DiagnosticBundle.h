#pragma once
#include "install/OperationController.h"
namespace nh {
struct DiagnosticBundleResult { std::string path,status; bool success=false; };
Json RedactAntiCheatForExport(const Json& inspection);
// Blocking bounded worker; invoke off the UI thread. Does not launch the game.
DiagnosticBundleResult ExportDiagnosticBundle(const std::string& executable,
 const std::filesystem::path& outputDirectory,const Json& inspection);
}
