#include "ui/HubViewModel.h"
#include <array>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace {
std::string ReadSelectionBytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("Could not read owned library fixture");
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
void WriteSelectionBytes(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!stream) throw std::runtime_error("Could not write owned library fixture");
}
void LoadSelectionLibrary(nh::HubModel& model) {
    // Hold only the unrelated package-preflight operation through its supported
    // busy state. Library parsing, publication, merge and persistence stay real.
    model.readiness.busy = true;
    try { model.Load(); } catch (...) { model.readiness.busy = false; throw; }
    model.readiness.busy = false;
}
void WriteInertSelectionExecutable(const std::filesystem::path& path) {
    // Inspection-only PE headers: no code or entry point, never executed.
    std::array<char, 512> bytes{};
    IMAGE_DOS_HEADER dos{}; dos.e_magic = IMAGE_DOS_SIGNATURE; dos.e_lfanew = 64;
    const DWORD signature = IMAGE_NT_SIGNATURE;
    IMAGE_FILE_HEADER header{}; header.Machine = IMAGE_FILE_MACHINE_AMD64;
    header.NumberOfSections = 1; header.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);
    header.Characteristics = IMAGE_FILE_EXECUTABLE_IMAGE;
    const WORD magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    std::memcpy(bytes.data(), &dos, sizeof(dos));
    std::memcpy(bytes.data() + 64, &signature, sizeof(signature));
    std::memcpy(bytes.data() + 68, &header, sizeof(header));
    std::memcpy(bytes.data() + 88, &magic, sizeof(magic));
    WriteSelectionBytes(path, std::string(bytes.data(), bytes.size()));
    if (!nh::InspectExecutable(path).suitable)
        throw std::runtime_error("Inert executable fixture did not satisfy inspection precondition");
}
}

int RunNrDllLibrarySelectionTests() {
    std::cout << std::unitbuf;
    wchar_t explicitRoot[32768]{};
    const auto count = GetEnvironmentVariableW(L"NEUROTIC_HUB_FIXTURE_ROOT", explicitRoot, 32768);
    if (!count || count >= 32768) throw std::runtime_error("Explicit owned fixture root is required");
    const auto root = std::filesystem::path(explicitRoot).lexically_normal();
    if (!root.is_absolute() || root == root.root_path() || nh::UserRoot() != root)
        throw std::runtime_error("Fixture root must be an ordinary absolute owned path");
    std::filesystem::create_directories(root);
    const auto file = root / L"library.json";
    if (std::filesystem::exists(file)) throw std::runtime_error("Library fixture root must start empty");
    const auto directory = root / L"Saved Game";
    std::filesystem::create_directories(directory);
    const auto executable = directory / L"primary.exe";
    WriteInertSelectionExecutable(executable);
    const auto gameRoot = nh::Utf8(directory.wstring());
    const auto gameExe = nh::Utf8(executable.wstring());
    const auto manualRoot = nh::Utf8((root / L"Manual Game").wstring());
    const auto defaultRoot = nh::Utf8((root / L"Default Game").wstring());
    const nh::Json selectionMetadata = {{"owner", "future-user"}, {"opaque", nh::Json::array({17, "keep", false})}};
    const nh::Json gameMetadata = {{"keep", "saved-game-data"}};
    const nh::Json directoryMetadata = {{"keep", "saved-library-data"}};
    nh::Json original = {
        {"schemaVersion", 3}, {"lightTheme", true}, {"favoritesFirst", true},
        {"onlineArtwork", false}, {"reducedMotion", true}, {"anythingCountdownSeconds", 17},
        {"searchRoots", nh::Json::array({gameRoot})}, {"excludedRoots", nh::Json::array()},
        {"futureDocument", {{"keep", "saved-document-data"}}},
        {"games", nh::Json::array({{{"id", "saved-stable-id"}, {"title", "Saved title"},
            {"store", "Steam"}, {"storeId", "saved-store-id"}, {"root", gameRoot},
            {"steamRoot", gameRoot}, {"iconPath", gameRoot + "/icon.png"},
            {"executable", gameExe}, {"manualTarget", true}, {"favorite", true}, {"hidden", true},
            {"futureRecord", gameMetadata}}})},
        {"knownDirectories", nh::Json::array({{{"path", gameRoot}, {"source", "Steam"},
            {"automatic", true}, {"futureDirectory", directoryMetadata}}})},
        {"nrDllSelections", nh::Json::array({
            {{"directory", gameRoot}, {"mode", "profile"}, {"profileId", "unavailable-saved-profile"}, {"futureSelectionMetadata", selectionMetadata}},
            {{"directory", manualRoot}, {"mode", "manual"}, {"profileId", ""}},
            {{"directory", defaultRoot}, {"mode", "default"}, {"profileId", ""}}})}
    };
    int checks = 0, failures = 0;
    auto check = [&](bool pass, const char* label) {
        ++checks; failures += !pass;
        std::cout << (pass ? "PASS " : "FAIL ") << label << '\n';
    };
    auto choiceForDirectory = [&](const nh::HubModel& model, const std::filesystem::path& path) {
        nh::Game alias; alias.target.path = nh::Utf8((path / L"primary.exe").wstring());
        return model.NrDllChoiceFor(alias);
    };
    WriteSelectionBytes(file, original.dump());
    nh::HubModel model; LoadSelectionLibrary(model);
    if (model.games.size() != 1) throw std::runtime_error("Positive library fixture did not load its one game");
    auto choice = model.NrDllChoiceFor(model.games[0]);
    check(choice.mode == nh::NrDllChoiceMode::Profile && choice.profileId == "unavailable-saved-profile",
          "Load retains unavailable explicit profile without default fallback or source acquisition");
    check(choiceForDirectory(model, root / L"Manual Game").mode == nh::NrDllChoiceMode::Manual,
          "Load retains explicit manual selection for another installation folder");
    check(choiceForDirectory(model, root / L"Default Game").mode == nh::NrDllChoiceMode::Default,
          "Load retains default selection for another installation folder");
    model.scanFolder = gameRoot;
    model.MergeDiscovery({{"protocolVersion", 2}, {"kind", "DiscoveryResult"}, {"errors", nh::Json::array()},
        {"games", nh::Json::array({{{"id", "different-rescan-id"}, {"title", "Rescanned title"},
            {"store", "Steam"}, {"storeId", "saved-store-id"}, {"installRoot", gameRoot},
            {"steamRoot", gameRoot}, {"executable", gameExe}}})}});
    check(model.games.size() == 1 && model.games[0].id == "saved-stable-id" &&
          model.games[0].favorite && model.games[0].hidden && model.games[0].storeId == "saved-store-id" &&
          model.games[0].iconHint == gameRoot + "/icon.png",
          "Actual same-game rescan preserves stable identity, flags, launcher identity and artwork");
    choice = model.NrDllChoiceFor(model.games[0]);
    check(choice.mode == nh::NrDllChoiceMode::Profile && choice.profileId == "unavailable-saved-profile",
          "Actual same-game merge retains directory-owned explicit DLL selection");
    model.reducedMotion = false; model.Save();
    const auto saved = nh::Json::parse(ReadSelectionBytes(file));
    const auto& selections = saved.at("nrDllSelections");
    bool profile = false, manual = false, useDefault = false, selectionUnknown = false;
    for (const auto& row : selections) {
        if (row.at("mode") == "profile") {
            profile = row.at("profileId") == "unavailable-saved-profile";
            selectionUnknown = row.at("futureSelectionMetadata") == selectionMetadata;
        } else if (row.at("mode") == "manual") manual = row.at("profileId") == "";
        else if (row.at("mode") == "default") useDefault = row.at("profileId") == "";
    }
    check(selections.size() == 3 && profile && manual && useDefault,
          "Save after rescan persists all three explicit selection modes without acquiring a provider");
    check(selectionUnknown && saved.at("futureDocument") == original.at("futureDocument") &&
          saved.at("games").at(0).at("futureRecord") == gameMetadata &&
          saved.at("knownDirectories").at(0).at("futureDirectory") == directoryMetadata,
          "Save after rescan preserves unknown document, game, directory and DLL-selection metadata");
    check(!saved.value("reducedMotion", true) && saved.value("anythingCountdownSeconds", 0) == 17,
          "Owned preference update survives alongside retained selection and library records");
    nh::HubModel reloaded; LoadSelectionLibrary(reloaded);
    if (reloaded.games.size() != 1) throw std::runtime_error("Saved positive library did not reload");
    const auto reloadedChoice = reloaded.NrDllChoiceFor(reloaded.games[0]);
    check(reloadedChoice.mode == nh::NrDllChoiceMode::Profile && reloadedChoice.profileId == "unavailable-saved-profile" &&
          reloaded.games[0].id == "saved-stable-id" && !reloaded.reducedMotion,
          "Load after actual merge and Save restores selection, stable game identity and changed preference");

    // Each late invalid record must leave an already-published snapshot intact.
    // These catch partial map publication, accepting malformed profile mode,
    // and missing normalized-directory duplicate validation.
    const std::array<nh::Json, 3> invalidRecords = {{
        {{"directory", manualRoot}, {"mode", "unknown"}, {"profileId", ""}},
        {{"directory", manualRoot}, {"mode", "profile"}, {"profileId", ""}},
        {{"directory", gameRoot + "\\"}, {"mode", "manual"}, {"profileId", ""}}
    }};
    for (const auto& invalid : invalidRecords) {
        auto malformed = original;
        malformed["lightTheme"] = false;
        malformed["games"][0]["id"] = "must-not-publish";
        malformed["nrDllSelections"] = nh::Json::array({original["nrDllSelections"][0], invalid});
        const auto bytes = malformed.dump(); WriteSelectionBytes(file, bytes);
        LoadSelectionLibrary(reloaded);
        const auto retained = reloaded.NrDllChoiceFor(reloaded.games.at(0));
        check(reloaded.games.size() == 1 && reloaded.games[0].id == "saved-stable-id" && reloaded.light &&
              !reloaded.reducedMotion && retained.mode == nh::NrDllChoiceMode::Profile &&
              retained.profileId == "unavailable-saved-profile" &&
              choiceForDirectory(reloaded, root / L"Manual Game").mode == nh::NrDllChoiceMode::Manual,
              "Invalid late selection commits no partial games, preferences or selection map");
        check(!reloaded.SetNrDllChoice(reloaded.games[0], {nh::NrDllChoiceMode::Manual, {}}),
              "Failed intake prevents selection mutation from replacing preserved user data");
        reloaded.Save();
        check(ReadSelectionBytes(file) == bytes, "Save after invalid selection preserves exact original file bytes");
        check(!reloaded.message.empty(), "Invalid selection intake leaves an actionable persistence refusal");
    }
    std::cout << "RESULT actualNrDllLibrarySelection=true checks=" << checks << " failures=" << failures
              << " NO_PROVIDER_EXECUTION=true NO_GAME_LAUNCH=true\n";
    return failures ? 1 : 0;
}
#ifdef NEUROTIC_NR_DLL_LIBRARY_SELECTION_STANDALONE
int main() try { return RunNrDllLibrarySelectionTests(); }
catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 2; }
#endif
