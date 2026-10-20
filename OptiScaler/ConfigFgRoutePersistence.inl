// Included only by actual Config.cpp, after Config/INI definitions are available.
#include "ConfigFgRoutePersistenceInternal.h"
#include <type_traits>

namespace Neurotic::FgRoutePersistence
{
namespace
{
struct Owner
{
    std::recursive_mutex mutex;
    std::filesystem::path requested;
    std::filesystem::path publicationDestination;
    Snapshot saved;
    uint64_t revision = 0;
};
Owner& State() { static auto* owner = new Owner; return *owner; }
std::filesystem::path Identity(const std::filesystem::path& path)
{
    if (path.empty()) return {};
    std::error_code error;
    auto absolute = std::filesystem::absolute(path, error);
    return error ? std::filesystem::path {} : absolute.lexically_normal();
}
constexpr const char* keys[] {"FGInput", "FGOutput", "FGNvngxReplacement"};
std::optional<uint32_t> Normalized(std::optional<uint32_t> value)
{ return value && *value == 0 ? std::nullopt : value; }
template<class T> std::optional<uint32_t> Number(const std::optional<T>& value)
{ return value.transform([](auto item) { return static_cast<uint32_t>(item); }); }
std::array<std::optional<uint32_t>,3> Current(Config& config)
{
    return {Number(config.FGInput.value_for_config()), Number(config.FGOutput.value_for_config()),
            Number(config.FGNvngxReplacement.value_for_config())};
}
Snapshot ForCurrentDestination()
{
    auto result = State().saved;
    const auto destination = Identity(State().requested);
    if (destination.empty() || result.destination != destination)
    {
        const auto revision = result.revision;
        result = {}; result.destination = destination; result.revision = revision;
    }
    return result;
}
Field Decode(const CSimpleIniA& file, unsigned field)
{
    Field result;
    const char* raw = file.GetValue("FrameGen", keys[field]);
    if (!raw) return result;
    result.raw = raw;
    if (lstrcmpiA(raw, "auto") == 0) return result;
    const auto match = [&](const char* name, auto value) {
        if (lstrcmpiA(raw, name) != 0) return false;
        result.value = static_cast<uint32_t>(value); return true;
    };
    bool known = false;
    if (field == 0)
        known = match("NoFG", FGInput::NoFG) || match("Upscaler", FGInput::Upscaler) ||
            match("NvngxFG", FGInput::NvngxFG) || match("DLSSG", FGInput::DLSSG) ||
            match("FSRFG", FGInput::FSRFG) || match("FSRFG30", FGInput::FSRFG30) ||
            match("Nukems", FGInput::NvngxFG);
    else if (field == 1)
        known = match("NoFG", FGOutput::NoFG) || match("FSRFG", FGOutput::FSRFG) ||
            match("XeFG", FGOutput::XeFG) || match("DLSSG", FGOutput::DLSSG);
    else
        known = match("None", FGNvngxReplacement::None) || match("Nukems", FGNvngxReplacement::Nukems) ||
            match("Arturs", FGNvngxReplacement::Arturs) || match("FFX", FGNvngxReplacement::FFX) ||
            match("Combo", FGNvngxReplacement::Combo);
    result.recognized = known;
    return result;
}
bool Different(Config& config, const Snapshot& saved)
{
    if (!saved.known) return true; // explicit Save remains available
    const auto current = Current(config);
    const std::array<bool,3> explicitIntent {config.FGInput.config_intent().has_value(),
        config.FGOutput.config_intent().has_value(), config.FGNvngxReplacement.config_intent().has_value()};
    for (unsigned i = 0; i < 3; ++i)
        if ((!saved.fields[i].recognized && explicitIntent[i]) ||
            current[i] != Normalized(saved.fields[i].value)) return true;
    return false;
}
}
namespace Detail
{
std::recursive_mutex& TransactionMutex() { return State().mutex; }
std::filesystem::path ResolveDestination(const std::filesystem::path& path) { return Identity(path); }
void PreparePublication()
{
    auto destination = Identity(State().requested);
    State().publicationDestination.swap(destination);
}
void SetDestination(const std::filesystem::path& path)
{
    auto requested = path;
    auto destination = Identity(path);
    State().requested.swap(requested);
    State().publicationDestination.swap(destination);
}
Snapshot Capture(const CSimpleIniA& file, const std::filesystem::path& path, Origin origin)
{
    Snapshot snapshot; snapshot.destination = Identity(path); snapshot.origin = origin;
    snapshot.known = !snapshot.destination.empty();
    for (unsigned i = 0; i < 3; ++i) snapshot.fields[i] = Decode(file, i);
    // Preserve the existing loader's documented legacy Nukems migration as
    // intent, while retaining the untouched on-disk raw tokens separately.
    if (snapshot.fields[0].raw && lstrcmpiA(snapshot.fields[0].raw->c_str(), "nukems") == 0)
    {
        snapshot.fields[2].value = static_cast<uint32_t>(FGNvngxReplacement::Nukems);
        snapshot.fields[2].recognized = true;
    }
    return snapshot;
}
void Publish(Snapshot&& snapshot) noexcept
{
    static_assert(std::is_nothrow_move_assignable_v<Snapshot>);
    if (snapshot.destination != State().publicationDestination) return;
    if (State().revision != UINT64_MAX) ++State().revision;
    snapshot.revision = State().revision;
    State().saved = std::move(snapshot);
}
void ObserveLoaded(const std::filesystem::path& path, const CSimpleIniA* fresh,
                   std::filesystem::path& selectedDestination)
{
    // Prepare every potentially allocating operation before selecting a new
    // Config destination. No allocation/identity work follows the commit.
    auto requested = Identity(path);
    if (requested.empty()) throw std::filesystem::filesystem_error(
        "resolve Config destination", path, std::make_error_code(std::errc::invalid_argument));
    auto publicationDestination = requested;
    auto actualDestination = requested;
    Snapshot candidate;
    if (fresh) candidate = Capture(*fresh, requested, Origin::LoadedFile);
    else if (State().saved.destination == requested) candidate = State().saved;
    else candidate.destination = requested;
    static_assert(std::is_nothrow_move_assignable_v<Snapshot>);
    // These swaps and move assignment are no-throw. A failed preparation leaves
    // Config's actual target and both owner identities at their previous value.
    selectedDestination.swap(actualDestination);
    State().requested.swap(requested);
    State().publicationDestination.swap(publicationDestination);
    Publish(std::move(candidate));
}
void ObserveLoadFailure(const std::filesystem::path& path)
{
    if (Identity(path) != Identity(State().requested)) return;
    PreparePublication();
    const DWORD attributes = GetFileAttributesW(path.c_str());
    const DWORD error = attributes == INVALID_FILE_ATTRIBUTES ? GetLastError() : ERROR_SUCCESS;
    if (attributes == INVALID_FILE_ATTRIBUTES && (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND))
    {
        CSimpleIniA empty;
        Publish(Capture(empty, path, Origin::MissingFile));
    }
    // Other failures retain a known baseline only for the same destination.
}
void PreserveOpaque(Config& config, CSimpleIniA& file, const std::filesystem::path& path)
{
    const auto saved = ForCurrentDestination();
    if (!saved.known || saved.destination != Identity(path)) return;
    const std::array<bool,3> untouched { !config.FGInput.config_intent().has_value(),
        !config.FGOutput.config_intent().has_value(), !config.FGNvngxReplacement.config_intent().has_value() };
    for (unsigned i = 0; i < 3; ++i)
        if (untouched[i] && !saved.fields[i].recognized && saved.fields[i].raw)
            file.SetValue("FrameGen", keys[i], saved.fields[i].raw->c_str());
}
}
Snapshot Read() { std::lock_guard lock(Detail::TransactionMutex()); return ForCurrentDestination(); }
bool Dirty(Config& config)
{ std::lock_guard lock(Detail::TransactionMutex()); return Different(config, ForCurrentDestination()); }
bool CanCancel(Config& config)
{ std::lock_guard lock(Detail::TransactionMutex()); const auto saved = ForCurrentDestination(); return saved.known && Different(config, saved); }
bool Cancel(Config& config)
{
    std::lock_guard lock(Detail::TransactionMutex());
    const auto saved = ForCurrentDestination();
    if (!saved.known) return false;
    config.FGInput.restore_config_intent(saved.fields[0].value.transform([](auto value) { return static_cast<FGInput>(value); }));
    config.FGOutput.restore_config_intent(saved.fields[1].value.transform([](auto value) { return static_cast<FGOutput>(value); }));
    config.FGNvngxReplacement.restore_config_intent(saved.fields[2].value.transform([](auto value) { return static_cast<FGNvngxReplacement>(value); }));
    return true;
}
}
