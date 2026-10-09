#pragma once
#include "install/OperationController.h"
#include <optional>
#include "install/BulkUninstall.h"

namespace nh {
struct CachedGameProfile { Json inspection; std::string checkedUtc; };
// Advisory snapshots only. The installer always performs its own fresh checks.
std::optional<CachedGameProfile> LoadGameProfile(const std::string& executable) noexcept;
bool SaveGameProfile(const std::string& executable, const Json& inspection) noexcept;
void RemoveGameProfile(const std::string& executable) noexcept;
struct KnownInstallationInventory {std::vector<BulkUninstallTarget> targets;std::vector<std::string> issues;};
// Bounded nonrecursive metadata only. Stale entries are candidates, never authority.
KnownInstallationInventory EnumerateKnownInstallations() noexcept;
bool RememberKnownInstallation(const std::string& executable,const std::string& title={}) noexcept;
}
