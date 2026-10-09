#pragma once
#include "CapabilityContract.h"
namespace DlssNr::Capability {
enum class ExportProfile { Minimal, LocalArchive };
enum class ExportState { Encoded, Capacity, InvalidSnapshot, Unavailable, LegacyProjectionUnavailable };
struct ExportResult { ExportState state=ExportState::Unavailable; std::string utf8,reason; };
enum class ImportState { Historical, InvalidSyntax, InvalidShape, InvalidReferences, Capacity, Unavailable };
struct ImportResult;
class HistoricalReport {
    std::string document,digest;
    HistoricalReport(std::string d,std::string h):document(std::move(d)),digest(std::move(h)){}
    friend ImportResult DecodeHistorical(std::span<const std::byte>) noexcept;
  public:
    std::string_view Document() const noexcept { return document; }
    std::string_view ArtifactSha256() const noexcept { return digest; }
    bool IsHistorical() const noexcept { return true; }
    static constexpr bool executionPermission=false;
};
struct ImportResult { ImportState state=ImportState::Unavailable; std::shared_ptr<const HistoricalReport> report; std::string reason; };
ExportResult ExportDiagnostic(const Snapshot&,ExportProfile) noexcept;
ImportResult DecodeHistorical(std::span<const std::byte>) noexcept;
ExportResult ExportHistoricalArchive(const HistoricalReport&) noexcept;
}
