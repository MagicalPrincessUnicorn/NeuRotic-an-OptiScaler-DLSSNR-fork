#include "dlssnr/VulkanPresent.h"
#include <cassert>
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

using namespace DlssNr;

struct FakeCommands : IVkPresentCommands
{
    std::vector<std::string> calls;
    bool valid = true;
    bool model = true;
    bool close = true;
    bool completeChain = true;
    bool preparing = false;
    bool Check(const VkPresentImageRequest&) override { calls.push_back("check"); return valid; }
    bool Begin() override { calls.push_back("begin"); return true; }
    void SourceToTransfer() override { calls.push_back("source-to-transfer"); }
    void Capture() override { calls.push_back("capture"); }
    void SourceToPresent() override { calls.push_back("source-to-present"); }
    bool ConvertToModel() override { calls.push_back("convert-to-model"); return true; }
    bool Evaluate(bool) override { calls.push_back("model"); return model; }
    VkRecordResult ModelResult() const override {
        auto result=IVkPresentCommands::ModelResult();
        if(preparing){result.status=VkRecordStatus::Partial;result.preparationOnly=true;result.outputWriteRecorded=false;
            result.chainDeliverable=false;result.reason="model initialization awaits owned completion";return result;}
        if(!completeChain){result.requestedPasses=10;result.completedPasses=2;result.chainDeliverable=false;}
        return result;
    }
    bool ConvertFromModel() override { calls.push_back("convert-from-model"); return true; }
    void TargetToTransfer() override { calls.push_back("target-to-transfer"); }
    void CopyBack() override { calls.push_back("copyback"); }
    void TargetToPresent() override { calls.push_back("target-to-present"); }
    bool End() override { calls.push_back("end"); return close; }
    void Discard() override { calls.push_back("discard"); }
};

int main()
{
    VkNrTemporalMetadata sr;sr.color={0,0,1920,1080};sr.motion={0,0,1920,1080};
    sr.jitterX=0.5f;sr.jitterY=-0.25f;sr.motionScaleX=192.0f;sr.motionScaleY=108.0f;
    const auto present=VkPresentTemporal(sr,{3840,2160});
    const auto model=ScaleVkNrTemporal(present,{3840,2160},{1920,1080});
    assert(model.jitterX==sr.jitterX&&model.jitterY==sr.jitterY&&model.motionScaleX==sr.motionScaleX&&model.motionScaleY==sr.motionScaleY);
    assert(present.motion.width==sr.motion.width&&present.color.width==sr.color.width);
    assert(!VkPresentNeedsView(VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT));
    assert(VkPresentNeedsView(VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT));
    assert(VkPresentNeedsView(VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT));
    // CPU reference checks the selected Vulkan blit representation, including alpha and
    // BGRA component semantics. GPU pixels still need the game/driver validation run.
    for (const auto format : { VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8A8_UNORM,
                               VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_B8G8R8A8_SRGB })
    {
        const auto recipe = SelectVkPresentColorRecipe(format);
        assert(recipe.supported && recipe.filter == VK_FILTER_NEAREST);
        for (const std::array<unsigned char, 4> logical : { std::array<unsigned char, 4>{255, 0, 0, 64},
                                                           std::array<unsigned char, 4>{0, 0, 255, 192} })
        {
            auto encoded = logical;
            if (recipe.bgra) std::swap(encoded[0], encoded[2]);
            auto decoded = encoded;
            if (recipe.bgra) std::swap(decoded[0], decoded[2]);
            assert(decoded == logical);
            assert(decoded[3] == logical[3]);
            if (recipe.srgb)
            {
                const double channel = decoded[0] / 255.0;
                const double linear = channel <= 0.04045 ? channel / 12.92 :
                    std::pow((channel + 0.055) / 1.055, 2.4);
                const double roundtrip = linear <= 0.0031308 ? linear * 12.92 :
                    1.055 * std::pow(linear, 1.0 / 2.4) - 0.055;
                assert(std::abs(roundtrip - channel) < 1e-5);
            }
        }
    }
    assert(!SelectVkPresentColorRecipe(VK_FORMAT_A2B10G10R10_UNORM_PACK32).supported);

    VkPresentImageRequest request {};
    request.image = reinterpret_cast<VkImage>(1);
    request.format = VK_FORMAT_B8G8R8A8_SRGB;
    request.extent = { 1920, 1080 };
    request.generation = 3;
    request.commandBuffer = reinterpret_cast<VkCommandBuffer>(2);
    auto run = [&](FakeCommands& commands) { return RecordVkPresentImage(request, commands); };

    FakeCommands good;
    auto result = run(good);
    assert(result.status == VkRecordStatus::Complete && result.targetWriteRecorded);
    assert((good.calls == std::vector<std::string> { "check", "begin", "source-to-transfer", "capture",
        "source-to-present", "convert-to-model", "model", "convert-from-model", "target-to-transfer",
        "copyback", "target-to-present", "end" }));

    request.captureOnly=true;FakeCommands diagnostic;result=run(diagnostic);
    assert(result.status==VkRecordStatus::Complete&&!result.targetWriteRecorded&&result.captureOnly);
    assert(std::find(diagnostic.calls.begin(),diagnostic.calls.end(),"copyback")==diagnostic.calls.end());
    request.captureOnly=false;
    FakeCommands preparation;preparation.preparing=true;preparation.model=false;
    result=run(preparation);
    assert(result.status==VkRecordStatus::Complete&&!result.targetWriteRecorded&&!result.chainDeliverable);
    assert(std::find(preparation.calls.begin(),preparation.calls.end(),"discard")==preparation.calls.end());
    assert(std::find(preparation.calls.begin(),preparation.calls.end(),"copyback")==preparation.calls.end());
    assert(preparation.calls.back()=="end");
    FakeCommands invalid;
    invalid.valid = false;
    result = run(invalid);
    assert(result.status == VkRecordStatus::NoWork && !result.targetWriteRecorded);
    assert((invalid.calls == std::vector<std::string> { "check" }));

    FakeCommands refused;
    refused.model = false;
    result = run(refused);
    assert(result.status != VkRecordStatus::Complete && !result.targetWriteRecorded);
    assert(std::find(refused.calls.begin(), refused.calls.end(), "copyback") == refused.calls.end());

    FakeCommands unsealed;
    FakeCommands partial;
    partial.completeChain=false;result=run(partial);
    assert(result.status==VkRecordStatus::Partial&&!result.targetWriteRecorded);
    assert(std::find(partial.calls.begin(),partial.calls.end(),"copyback")==partial.calls.end());
    unsealed.close = false;
    result = run(unsealed);
    assert(result.status != VkRecordStatus::Complete);
    return 0;
}
