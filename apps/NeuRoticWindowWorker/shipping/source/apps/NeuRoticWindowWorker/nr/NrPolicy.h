#pragma once
#include "../WorkerContracts.h"
#include <filesystem>
#include <string>
#include <utility>
namespace nrw::detail
{
bool ValidOptions(const NrOptions &, std::string &);
std::pair<uint32_t,uint32_t> WorkingExtent(const NrOptions&, uint32_t width, uint32_t height);
bool UseGuides(const NrOptions &, const FrameStamp &, const GuideResult *);
bool ResetHistory(const FrameStamp &, const FrameStamp &);
bool AdmitShape(uint32_t width, uint32_t height, uint32_t workWidth, uint32_t workHeight,
                std::string &reason);
std::filesystem::path CoreAtRoot(const std::filesystem::path &);
} // namespace nrw::detail
