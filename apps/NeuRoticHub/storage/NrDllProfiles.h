#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <vector>

namespace nh {
enum class NrDllErrorCode {
    ReadOnly, InvalidPath, InvalidName, WrongFilename, MissingProfile,
    NoDefault, InUse, UnsafePath, ChangedAsset, CorruptMetadata, IoError, Cancelled
};
// Codes are translated by the UI through the live catalog, never a fixed locale
// switch. what() is a diagnostic token, not a user-facing English sentence.
class NrDllError : public std::runtime_error {
public:
    NrDllError(NrDllErrorCode code, unsigned long systemError = 0);
    const NrDllErrorCode code;
    const unsigned long systemError;
    NrDllErrorCode Code() const noexcept { return code; }
};
struct NrDllProfile {
    std::string id, name, sha256;
    std::filesystem::path dllPath;
    std::uint64_t bytes = 0;
    // Presence only. This is not loader, export, API, or runtime qualification.
    bool available = false;
};
struct NrDllIssue {
    std::string profileId;
    NrDllErrorCode code = NrDllErrorCode::IoError;
};
struct NrDllCatalog {
    std::vector<NrDllProfile> profiles;
    std::string defaultId;
    std::vector<NrDllIssue> issues;
};
class NrDllAssetLease;
struct NrDllSelection {
    NrDllProfile profile;
    // Hold through Plan/Execute AND process drain/cancellation. Copies share the
    // same file/directory pins. Release only when no child can read the source.
    std::shared_ptr<const NrDllAssetLease> lease;
};
class NrDllProfiles {
public:
    // Caller owns UserDataSession and supplies its writable state. Use
    // SharedRuntimeRoot()/NrDllProfiles, outside clearable HubData-v2.
    explicit NrDllProfiles(std::filesystem::path root, bool writable = false);
    const std::filesystem::path& Root() const noexcept { return root_; }
    NrDllCatalog List() const; // Does not create/write files or hash DLLs.
    NrDllProfile Import(const std::filesystem::path& source, const std::string& name,
                        std::stop_token stop = {});
    void Rename(const std::string& id, const std::string& name);
    void SetDefault(const std::string& id);
    void ClearDefault();
    void Remove(const std::string& id);
    NrDllSelection Acquire(const std::string& id, std::stop_token stop = {}) const;
    // Empty ONLY when no default is configured. Configured unavailable IDs throw.
    std::optional<NrDllSelection> AcquireDefault(std::stop_token stop = {}) const;
private:
    std::filesystem::path root_;
    bool writable_;
};
} // namespace nh
