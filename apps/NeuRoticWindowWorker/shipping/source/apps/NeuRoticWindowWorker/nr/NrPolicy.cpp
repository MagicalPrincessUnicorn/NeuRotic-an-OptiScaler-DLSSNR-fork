#include "NrPolicy.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <stdexcept>
#include "../../../OptiScaler/dlssnr/DlssNr_PresentResolution.h"
namespace nrw::detail
{
bool ValidOptions(const NrOptions &o, std::string &reason)
{
    if(o.modelStyle<0||o.modelStyle>2){reason="Unsupported model style";return false;}
    if (o.modelPath.empty())
    {
        reason = "Please provide your DLSS NR file.";
        return false;
    }
    if (!std::filesystem::path(o.modelPath).is_absolute() ||
        !std::filesystem::path(o.forwarderPath).is_absolute())
    {
        reason = "Explicit absolute model and forwarder paths are required.";
        return false;
    }
    if (!std::isfinite(o.transferStrength) || !std::isfinite(o.colourStrength) || o.transferStrength < 0 ||
        o.transferStrength > 4 || o.colourStrength < 0 || o.colourStrength > 4)
    {
        reason = "NR strengths must be finite values in [0,4].";
        return false;
    }
    if ((o.workWidth == 0) != (o.workHeight == 0) || o.workWidth > 16384 || o.workHeight > 16384)
    {
        reason = "NR work extent must be complete and within D3D texture limits.";
        return false;
    }
    reason.clear();
    if(o.nrScalePercent<25 || o.nrScalePercent>100 || (o.nrScalePercent!=100 && o.workWidth)) {
        reason="NR resolution must be 25 to 100 percent without an explicit work size";return false;
    }
    return true;
}
std::pair<uint32_t,uint32_t> WorkingExtent(const NrOptions& o,uint32_t width,uint32_t height)
{
    if(o.workWidth || o.workHeight)return {o.workWidth,o.workHeight};
    // Same aligned manual-resolution policy as in-game Present; recomputed on resize.
    auto size=DlssNr::PresentResolution::Resolve({DlssNr::PresentResolution::Manual,o.nrScalePercent},width,height);
    if(!size.width || !size.height)throw std::runtime_error(size.reason ? size.reason : "Invalid NR resolution");
    return {size.width,size.height};
}
bool UseGuides(const NrOptions &o, const FrameStamp &f, const GuideResult *g)
{
    if (o.guides != GuideMode::Experimental || !g || !g->estimated || !g->depthCompleted ||
        !g->motionCompleted)
        return false;
    if (g->stamp.session != f.session || g->stamp.sequence != f.sequence ||
        g->stamp.timestampQpc != f.timestampQpc || g->stamp.width != f.width || g->stamp.height != f.height ||
        g->stamp.reset != f.reset || g->stamp.streamEpoch!=f.streamEpoch || g->stamp.geometryEpoch!=f.geometryEpoch || g->stamp.configEpoch!=f.configEpoch || !g->width || !g->height || g->width > f.width || g->height > f.height ||
        g->width > 16384 || g->height > 16384)
        return false;
    const size_t count = size_t(g->width) * g->height;
    return count && g->depth.size() == count && g->motionXY.size() == count * 2 &&
           std::all_of(g->depth.begin(), g->depth.end(),
                       [](float v) { return std::isfinite(v) && v >= 0; }) &&
           std::all_of(g->motionXY.begin(), g->motionXY.end(), [](float v) { return std::isfinite(v); });
}
bool ResetHistory(const FrameStamp &f, const FrameStamp &p)
{
    return f.reset || !p.sequence || f.session != p.session || f.streamEpoch!=p.streamEpoch || f.geometryEpoch!=p.geometryEpoch || f.configEpoch!=p.configEpoch || f.width != p.width || f.height != p.height ||
           f.sequence != p.sequence + 1 || f.timestampQpc <= p.timestampQpc;
}
bool AdmitShape(uint32_t width, uint32_t height, uint32_t workWidth, uint32_t workHeight, std::string &reason)
{
    if (!width || !height || !workWidth || !workHeight || width > 16384 || height > 16384 ||
        workWidth > width || workHeight > height)
    {
        reason = "NR shape is outside the captured raster or D3D texture limits";
        return false;
    }
    // Conservative bound for owned shared colour, float composition/model images,
    // guide copies/uploads, readback, CPU conversion and independently owned output.
    // Opaque NVIDIA feature/driver allocation is not measured by this admission.
    constexpr uint64_t budget = 1280ull * 1024 * 1024;
    const uint64_t ownedBytes =
        uint64_t(width) * height * 100 + uint64_t(workWidth) * workHeight * 24 + 32ull * 1024 * 1024;
    if (ownedBytes > budget)
    {
        reason =
            "NR owned transport/image admission exceeds 1280 MiB budget; opaque provider memory excluded";
        return false;
    }
    return true;
}
std::filesystem::path CoreAtRoot(const std::filesystem::path &root)
{
    if (!root.is_absolute())
        return {};
    for (const auto *filename : {L"nvngx.dll", L"_nvngx.dll"})
        if (std::filesystem::is_regular_file(root / filename))
            return root / filename;
    return {};
}
} // namespace nrw::detail
