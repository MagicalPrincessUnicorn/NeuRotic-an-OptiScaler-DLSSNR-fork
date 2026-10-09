#pragma once
#include "../WorkerContracts.h"
#include <json.hpp>
#include <filesystem>

namespace nrw {
inline constexpr size_t MaxCommandBytes = 65536;
using Json = nlohmann::json;
struct ModelInfo {
    bool valid = false;
    std::wstring path;
    std::string sha256, reason;
    uint64_t bytes = 0, fileId = 0, modified = 0;
};
ModelInfo VerifyModel(const std::wstring& path);
ModelInfo ImportModel(const std::wstring& source, const std::wstring& modelsRoot);
std::string HashFile(const std::wstring& path, std::string& reason);
std::wstring DefaultModelsRoot();
std::string Utf8(const std::wstring&);
std::wstring Wide(const std::string&);
Json ModelJson(const ModelInfo&);
Json WindowJson(const WindowIdentity&);
WindowIdentity ParseWindow(const Json&);
Json ParseCommand(const std::string& line);
depth::Settings ParseDepthSettings(const Json&);

class SelectionState {
public:
    bool Request(uint64_t revision, std::string& reason);
    bool Countdown(uint64_t revision, unsigned seconds, uint64_t nowMs, bool modelReady, std::string& reason);
    bool Due(uint64_t nowMs);
    void Cancel();
    uint64_t Requested() const { return requested_; }
    bool Pending() const { return pending_; }
    uint64_t Remaining(uint64_t nowMs) const { return pending_ && deadline_ > nowMs ? deadline_ - nowMs : 0; }
private: uint64_t requested_ = 0, deadline_ = 0; bool pending_ = false;
};
int RunControlTests();
}
