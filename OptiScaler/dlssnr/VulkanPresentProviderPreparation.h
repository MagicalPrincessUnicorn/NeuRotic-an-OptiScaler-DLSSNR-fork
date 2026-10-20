#pragma once
#include "VulkanNrModelLifetime.h"
#include <cstdint>
#include <string>

namespace DlssNr {
enum class VkProviderPrepareStage { NotStarted, Preflight, BeforeToken, Token, BeforeTags, Tags, Prepared };
enum class VkProviderPrepareFailure {
    None, Provider, Frame, Viewport, Adapter, TokenApi, TagApi, SourceRevision,
    UnqualifiedLifetime, OwnerUnavailable, OwnerMismatch, TokenResult, TokenNull,
    TokenVersion, TokenFrame, TagResult
};
enum class VkProviderPrepareEffects { Unknown, NoExternalEntry, ExternalEntered };
struct VkProviderPrepareCall { bool entered=false,returned=false;int32_t result=0; };

// A CPU preparation receipt, never submission/completion or resource ownership.
// Unknown is deliberately conservative, including a callback that throws.
struct VkPresentProviderPreparation {
    bool prepared=false;
    VkProviderPrepareStage stage=VkProviderPrepareStage::NotStarted;
    VkProviderPrepareStage providerObservedAt=VkProviderPrepareStage::NotStarted;
    VkProviderPrepareStage ownerObservedAt=VkProviderPrepareStage::NotStarted;
    VkProviderPrepareFailure failure=VkProviderPrepareFailure::None;
    VkProviderPrepareEffects effects=VkProviderPrepareEffects::Unknown;
    VkProviderPrepareCall token,tags;
    uint64_t provider=0,observedProvider=0,frame=0,sourceRevision=0,observedRevision=0;
    uint32_t viewport=UINT32_MAX,tokenVersion=0,tokenFrame=0;
    VkNrPresentModelLifetime expectedLifetime;
    std::optional<VkNrPresentModelLifetime> observedLifetime;
    std::string adapterReason;
    bool NoExternalEntry()const noexcept {
        return !prepared&&effects==VkProviderPrepareEffects::NoExternalEntry&&
            !token.entered&&!token.returned&&!tags.entered&&!tags.returned;
    }
};
}
