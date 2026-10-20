#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

class Config;
namespace Neurotic::FgRoutePersistence
{
enum class Origin { Unknown, MissingFile, LoadedFile, PublishedFile };
struct Field
{
    std::optional<std::string> raw;
    std::optional<uint32_t> value;
    bool recognized = true;
};
struct Snapshot
{
    std::filesystem::path destination;
    std::array<Field,3> fields;
    uint64_t revision = 0;
    Origin origin = Origin::Unknown;
    bool known = false;
};
// Config owns this evidence. These are not another editable route draft or
// active runtime state. Enter outside the NR synchronization domain.
Snapshot Read();
bool Dirty(Config&);
bool CanCancel(Config&);
bool Cancel(Config&);
}
