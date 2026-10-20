#include <cstdio>
#include <cstdlib>
#include <cmath>
#if __has_include("../OptiScaler/dlssnr/VulkanNrFrameContract.h")
#include "../OptiScaler/dlssnr/VulkanNrFrameContract.h"
#include "../OptiScaler/dlssnr/VulkanNrFrameParams.h"
#include "../OptiScaler/dlssnr/VulkanNrImageSnapshot.h"
#endif

static void Check(bool value, const char* message)
{
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
int main()
{
#if !__has_include("../OptiScaler/dlssnr/VulkanNrFrameContract.h")
    Check(false, "Vulkan immutable frame contract is missing");
#else
    using namespace DlssNr;
    VkNrTemporalMetadata metadata{};
    metadata.color = {8,12,100,100};
    metadata.depth = {4,6,100,100}; metadata.motion = {2,3,100,100};
    metadata.motionScaleX = 2; metadata.motionScaleY = -3;
    metadata.jitterX = .5f; metadata.jitterY = -.25f;
    const auto scaled = ScaleVkNrTemporal(metadata, {100,100}, {50,75});
    Check(scaled.color.x == 8 && scaled.color.y == 12, "source origins survive work scaling");
    Check(scaled.depth.width == 100 && scaled.motion.height == 100, "guide raster not resized by metadata scaling");
    Check(scaled.motionScaleX == 1 && scaled.motionScaleY == -2.25f, "anisotropic motion scaled once");
    Check(scaled.jitterX == .25f && scaled.jitterY == -.1875f, "jitter follows work raster once");
    Check(metadata.motionScaleX == 2 && metadata.jitterX == .5f, "source metadata immutable");
    VkNrFrameContract frame{};
    frame.output = {100,100}; frame.work = {50,75};
    frame.representation.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    Check(ValidateVkNrFrameContract(frame), "metadata absent remains legitimate image-only contract");
    frame.work.width = 0;
    Check(!ValidateVkNrFrameContract(frame), "zero work refused");
    frame.work = {8193,75};
    Check(!ValidateVkNrFrameContract(frame), "resource bound enforced");
    frame.work = {50,75}; frame.temporal = metadata;
    frame.temporal->motion.x = UINT32_MAX;
    Check(!ValidateVkNrFrameContract(frame), "overflowing guide origin refused");
    frame.temporal = metadata; frame.temporal->motionScaleX = NAN;
    Check(!ValidateVkNrFrameContract(frame), "nonfinite temporal scale refused");
    VkFrameRequest owned;owned.route=VkNrRoute::Present;owned.contract.route=VkNrRoute::Present;owned.ownedPresentInputs=true;
    Check(VkNrSnapshotMetadataQualified(owned),"Present-owned neutral inputs support Hold without invented temporal metadata");
    owned.route=VkNrRoute::Native;owned.contract.route=VkNrRoute::Native;
    Check(!VkNrSnapshotMetadataQualified(owned),"ownership marker never bypasses Native temporal proof");
    VkFrame::Failure failure;
    auto invalid = ScaleVkNrTemporal(metadata, {0,100}, {50,75});
    Check(!std::isfinite(invalid.motionScaleX), "invalid scale source cannot mint unit metadata");
    std::puts("PASS: Vulkan immutable frame bounds, active origins and once-only motion/jitter scaling");
#endif
}
