#pragma once
#include "ConfigFgRoutePersistence.h"
#include "CheckedIniFile.h"
#include <mutex>
#include <utility>

namespace Neurotic::FgRoutePersistence::Detail
{
std::recursive_mutex& TransactionMutex();
std::filesystem::path ResolveDestination(const std::filesystem::path&);
void SetDestination(const std::filesystem::path&);
void PreparePublication();
void ObserveLoaded(const std::filesystem::path&, const CSimpleIniA* fresh, std::filesystem::path& selectedDestination);
void ObserveLoadFailure(const std::filesystem::path&);
void PreserveOpaque(Config&, CSimpleIniA&, const std::filesystem::path&);
Snapshot Capture(const CSimpleIniA&, const std::filesystem::path&, Origin);
void Publish(Snapshot&&) noexcept;

// The caller serializes its complete candidate mutation using TransactionMutex.
// Capture and actual checked publication use the same image, never later Config.
inline CheckedIniFile::Result WritePublished(const CSimpleIniA& file, const std::filesystem::path& path)
{
    std::lock_guard lock(TransactionMutex());
    PreparePublication();
    auto candidate = Capture(file, path, Origin::PublishedFile);
    if (!candidate.known) return {false, ERROR_INVALID_NAME, "resolve-destination"};
    const auto result = CheckedIniFile::SaveDetailed(file, candidate.destination);
    if (result.succeeded) Publish(std::move(candidate));
    return result;
}
template<class Edit, class Reload> bool SaveSubset(const std::filesystem::path& path, Edit&& edit, Reload&& reload)
{
    std::lock_guard lock(TransactionMutex());
    CSimpleIniA partial;
    const auto destination = ResolveDestination(path);
    if (destination.empty() || partial.LoadFile(destination.c_str()) != SI_OK) return false;
    edit(partial);
    if (!WritePublished(partial, destination).succeeded) return false;
    // Publication and this optional cache refresh are different outcomes.
    return reload();
}
}
