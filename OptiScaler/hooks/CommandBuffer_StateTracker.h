#include <map>
#pragma once
#include <nr/diagnostics/HostCost.h>
#include <dlssnr/VulkanNrImageFacts.h>

#include <pch.h>

#include <vulkan/vulkan.h>

#include <State.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <optional>
#include <algorithm>
#include <bitset>
#include <atomic>
#include <shared_mutex>

// Native NR needs authenticated image layouts and outside-render-pass state.
// Keep full tracking enabled for Vulkan parity.

namespace vk_state
{
static constexpr uint32_t kMaxDescriptorSets = 32;
static constexpr uint32_t kMaxViewports = 16;
static constexpr uint32_t kMaxScissors = 16;
static constexpr uint32_t kMaxVertexBuffers = 32;      // Standard limit is often 32
static constexpr uint32_t kMaxPushConstantBytes = 256; // 128 is min-spec, 256 covers most

enum class BindPointIndex : uint32_t
{
    Graphics = 0,
    Compute = 1,
    Count
};

inline std::optional<BindPointIndex> ToIndex(VkPipelineBindPoint bp)
{
    switch (bp)
    {
    case VK_PIPELINE_BIND_POINT_GRAPHICS:
        return BindPointIndex::Graphics;
    case VK_PIPELINE_BIND_POINT_COMPUTE:
        return BindPointIndex::Compute;
    case VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR:
    {
        // This is a normal game command, not an error. Local static initialization
        // bounds the diagnostic across rendering threads; logging may flush synchronously.
        static const bool reported = [] {
            LOG_DEBUG("Ray tracing bind point not tracked by state tracker (reported once)");
            return true;
        }();
        (void) reported;
        return std::nullopt;
    }
    default:
    {
        // Unknown bind point - don't track to avoid corrupting other state
        static const bool reported = [bp] {
            LOG_WARN("Unknown pipeline bind point {} - ignoring to avoid state corruption (reported once)", (uint32_t) bp);
            return true;
        }();
        (void) reported;
        return std::nullopt;
    }
    }
}

// Function table for replay
struct VulkanCmdFns
{
    PFN_vkCmdBindPipeline CmdBindPipeline = nullptr;
    PFN_vkCmdBindDescriptorSets CmdBindDescriptorSets = nullptr;
    PFN_vkCmdPushConstants CmdPushConstants = nullptr;
    PFN_vkCmdSetViewport CmdSetViewport = nullptr;
    PFN_vkCmdSetScissor CmdSetScissor = nullptr;
    PFN_vkCmdBindVertexBuffers CmdBindVertexBuffers = nullptr;
    PFN_vkCmdBindIndexBuffer CmdBindIndexBuffer = nullptr;
    PFN_vkCmdPipelineBarrier CmdPipelineBarrier = nullptr;

    // Extended dynamic state
    PFN_vkCmdSetCullMode CmdSetCullMode = nullptr;
    PFN_vkCmdSetFrontFace CmdSetFrontFace = nullptr;
    PFN_vkCmdSetPrimitiveTopology CmdSetPrimitiveTopology = nullptr;
    PFN_vkCmdSetDepthTestEnable CmdSetDepthTestEnable = nullptr;
    PFN_vkCmdSetDepthWriteEnable CmdSetDepthWriteEnable = nullptr;
    PFN_vkCmdSetDepthCompareOp CmdSetDepthCompareOp = nullptr;
    PFN_vkCmdSetDepthBoundsTestEnable CmdSetDepthBoundsTestEnable = nullptr;
    PFN_vkCmdSetStencilTestEnable CmdSetStencilTestEnable = nullptr;
    PFN_vkCmdSetStencilOp CmdSetStencilOp = nullptr;
};

struct DescriptorBinding
{
    bool Bound = false;
    VkDescriptorSet Set = VK_NULL_HANDLE;
    VkPipelineLayout BoundWithLayout = VK_NULL_HANDLE; // Layout used when this set was bound
    uint32_t BindCallIndex = 0;                        // Index into DescriptorBindCalls that established this set
};

// NEW: Verbatim recording of vkCmdBindDescriptorSets calls
struct DescriptorBindCall
{
    VkPipelineLayout Layout = VK_NULL_HANDLE;
    uint32_t FirstSet = 0;
    uint32_t DescriptorSetCount = 0;
    std::vector<VkDescriptorSet> Sets;    // Store sets dynamically to handle any valid count
    std::vector<uint32_t> DynamicOffsets; // All dynamic offsets for this call
};

struct PushConstantEntry
{
    VkPipelineLayout Layout = VK_NULL_HANDLE;
    VkShaderStageFlags Stages = 0;
    uint32_t Offset = 0;
    uint32_t Size = 0;
    std::array<uint8_t, kMaxPushConstantBytes> Data {};
};

struct BindPointState
{
    VkPipeline Pipeline = VK_NULL_HANDLE;
    VkPipelineLayout CurrentPipelineLayout = VK_NULL_HANDLE; // Last layout seen in BindDescriptorSets

    std::array<DescriptorBinding, kMaxDescriptorSets> Sets {};

    // NEW: Timeline of descriptor bind calls
    std::vector<DescriptorBindCall> DescriptorBindCalls;

    BindPointState() { DescriptorBindCalls.reserve(4); }
};

struct DynamicState
{
    std::bitset<kMaxViewports> ViewportValidMask;
    std::array<VkViewport, kMaxViewports> Viewports {};

    std::bitset<kMaxScissors> ScissorValidMask;
    std::array<VkRect2D, kMaxScissors> Scissors {};

    bool CullModeSet = false;
    VkCullModeFlags CullMode = VK_CULL_MODE_NONE;

    bool FrontFaceSet = false;
    VkFrontFace FrontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    bool PrimitiveTopologySet = false;
    VkPrimitiveTopology PrimitiveTopology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

#ifndef LOW_PRECISION_TRACKING
    bool DepthTestEnableSet = false;
    VkBool32 DepthTestEnable = VK_FALSE;

    bool DepthWriteEnableSet = false;
    VkBool32 DepthWriteEnable = VK_TRUE;

    bool DepthCompareOpSet = false;
    VkCompareOp DepthCompareOp = VK_COMPARE_OP_LESS;

    bool DepthBoundsTestEnableSet = false;
    VkBool32 DepthBoundsTestEnable = VK_FALSE;

    bool StencilTestEnableSet = false;
    VkBool32 StencilTestEnable = VK_FALSE;

    bool StencilOpSet = false;
    VkStencilFaceFlags StencilOpFaceMask = 0;
    VkStencilOp StencilFailOp = VK_STENCIL_OP_KEEP;
    VkStencilOp StencilPassOp = VK_STENCIL_OP_KEEP;
    VkStencilOp StencilDepthFailOp = VK_STENCIL_OP_KEEP;
    VkCompareOp StencilCompareOp = VK_COMPARE_OP_ALWAYS;
#endif
};

struct VertexInputState
{
    std::bitset<kMaxVertexBuffers> BufferValid;
    std::array<VkBuffer, kMaxVertexBuffers> Buffers {};
    std::array<VkDeviceSize, kMaxVertexBuffers> Offsets {};

    bool IndexBufferValid = false;
    VkBuffer IndexBuffer = VK_NULL_HANDLE;
    VkDeviceSize IndexOffset = 0;
    VkIndexType IndexType = VK_INDEX_TYPE_UINT16;
};

struct CommandBufferState
{
    bool Recording = false;
    bool HasBegun = false;
    uint32_t BeginFlags = 0;
    uint64_t BeginEpoch = 0; // Epoch when this command buffer was last begun

#ifndef LOW_PRECISION_TRACKING
    std::unordered_map<VkImage, VkImageLayout> ImageLayouts;
    std::unordered_map<VkImage, VkImageSubresourceRange> ImageLayoutRanges;

    bool InRenderPass = false;
    VkRenderPass ActiveRenderPass = VK_NULL_HANDLE;
    VkFramebuffer ActiveFramebuffer = VK_NULL_HANDLE;
#endif

    std::array<BindPointState, static_cast<uint32_t>(BindPointIndex::Count)> BP {};
    DynamicState Dyn {};
    VertexInputState VI {};

    // Push constants are NOT per-bind-point state - they affect the command buffer globally
    // Store them in timeline order to replay correctly in mixed compute/graphics sequences
    std::vector<PushConstantEntry> PushConstantHistory;

    CommandBufferState()
    {
        // Most games use 1-4 push constant updates per frame
        PushConstantHistory.reserve(8);
    }

    void ResetForNewRecording(uint32_t flags, uint64_t epoch)
    {
        *this = CommandBufferState {};
        Recording = true;
        HasBegun = true;
        BeginFlags = flags;
        BeginEpoch = epoch;
    }

    void ResetAll() { *this = CommandBufferState {}; }
};

struct ReplayParams
{
    bool ReplayGraphicsPipeline = true;

    // Bitmask of sets to replay (1 << setIndex)
    // LIMITATION: 32-bit mask means only sets [0..31] can be requested for replay
    // Sets >= 32 are recorded in DescriptorBindCalls but cannot be selectively replayed
    // For graphics, this is typically sufficient as most pipelines use sets 0-3
    uint32_t RequiredGraphicsSetMask = 0x1;

    VkPipelineLayout OverrideGraphicsLayout = VK_NULL_HANDLE;

    bool ReplayPushConstants = true;
    bool ReplayViewportScissor = true;
    bool ReplayExtendedDynamicState = true;
    bool ReplayVertexIndex = false;
    bool ReplayComputeToo = false;
};

struct CommandBufferStateEntry
{
    std::shared_ptr<CommandBufferState> State;
    std::mutex Mutex; // Fine-grained lock per command buffer
};

class CommandBufferStateTracker
{
    using PoolKey = std::pair<VkDevice,VkCommandPool>;
  public:
    CommandBufferStateTracker() { _state = &State::Instance(); }

    // Call this when command buffers are allocated from a pool
    void OnAllocateCommandBuffers(VkDevice device, VkCommandPool rawPool, uint32_t count, const VkCommandBuffer* pCommandBuffers,
                                  uint32_t queueFamilyIndex)
    {
        Neurotic::HostCost::Scope nrHostCost(Neurotic::HostCost::Kind::TrackOther, 64);
        const PoolKey pool{device,rawPool};
        // Phase 1: Ensure pool metadata exists (only _poolMetadataMutex)
        {
            std::shared_lock poolLock(_poolMetadataMutex);
            if (_poolToQueueFamily.find(pool) == _poolToQueueFamily.end())
            {
                poolLock.unlock();

                std::unique_lock exclusivePoolLock(_poolMetadataMutex);
                // Double-check after acquiring exclusive lock
                if (_poolToQueueFamily.find(pool) == _poolToQueueFamily.end())
                {
                    _poolToQueueFamily[pool] = queueFamilyIndex;

                    auto epochIt = _poolEpochs.find(pool);
                    if (epochIt == _poolEpochs.end())
                    {
                        _poolEpochs[pool] = std::make_shared<std::atomic<uint64_t>>(
                            _globalEpochCounter.load(std::memory_order_acquire));
                    }
                }
            }
        }
        // _poolMetadataMutex fully released here before touching _statesMapMutex

        // Phase 2: Map command buffers (only _statesMapMutex)
        {
            std::unique_lock mapLock(_statesMapMutex);
            for (uint32_t i = 0; i < count; ++i)
            {
                _cmdBufferToPool[pCommandBuffers[i]] = pool;
            }
        }
    }

    void OnBegin(VkCommandBuffer cmd, const VkCommandBufferBeginInfo* pBeginInfo)
    {
        Neurotic::HostCost::Scope nrHostCost(Neurotic::HostCost::Kind::TrackLifecycle, 1);
        if (!TracksGameState())
            return;

        const uint32_t flags = (pBeginInfo) ? pBeginInfo->flags : 0;

        // Fast path: Try to get existing entry with read lock
        std::shared_ptr<CommandBufferStateEntry> entry;
        {
            std::shared_lock mapLock(_statesMapMutex);
            auto it = _states.find(cmd);
            if (it != _states.end())
                entry = it->second;
        }

        // Slow path: Create new entry if needed
        if (!entry)
        {
            std::unique_lock mapLock(_statesMapMutex);
            auto& entryRef = _states[cmd];
            if (!entryRef)
                entryRef = std::make_shared<CommandBufferStateEntry>();
            entry = entryRef;
        }

        // Get pool and epoch info (only needs _cmdBufferToPool lock)
        PoolKey pool{};
        {
            std::shared_lock mapLock(_statesMapMutex);
            auto poolIt = _cmdBufferToPool.find(cmd);
            if (poolIt != _cmdBufferToPool.end())
                pool = poolIt->second;
        }

        uint64_t currentEpoch = 0;
        if (pool.second != VK_NULL_HANDLE)
        {
            // Lock-free atomic read
            std::shared_lock poolLock(_poolMetadataMutex);
            auto epochIt = _poolEpochs.find(pool);

            if (epochIt == _poolEpochs.end())
            {
                poolLock.unlock();
                std::unique_lock exclusiveLock(_poolMetadataMutex);

                // Double-check after exclusive lock
                epochIt = _poolEpochs.find(pool);
                if (epochIt == _poolEpochs.end())
                {
                    currentEpoch = _globalEpochCounter.load(std::memory_order_acquire);
                    _poolEpochs[pool] = std::make_shared<std::atomic<uint64_t>>(currentEpoch);
                }
                else
                {
                    currentEpoch = epochIt->second->load(std::memory_order_acquire);
                }
            }
            else
            {
                currentEpoch = epochIt->second->load(std::memory_order_acquire);
            }
        }

        // Now lock only THIS command buffer's state
        std::scoped_lock stateLock(entry->Mutex);
        nrHostCost.Acquired();
        if (!entry->State)
            entry->State = std::make_shared<CommandBufferState>();

        entry->State->ResetForNewRecording(flags, currentEpoch);
    }

    void OnEnd(VkCommandBuffer cmd)
    {
        Neurotic::HostCost::Scope nrHostCost(Neurotic::HostCost::Kind::TrackLifecycle, 1);
        if (!TracksGameState())
            return;

        // Fast path: get entry with read lock
        std::shared_ptr<CommandBufferStateEntry> entry;
        {
            std::shared_lock mapLock(_statesMapMutex);
            auto it = _states.find(cmd);
            if (it != _states.end())
                entry = it->second;
        }

        if (!entry)
            return;

        // Lock only this command buffer's state
        std::scoped_lock stateLock(entry->Mutex);
        nrHostCost.Acquired();
        if (entry->State)
            entry->State->Recording = false;
    }

    void OnReset(VkCommandBuffer cmd)
    {
        Neurotic::HostCost::Scope nrHostCost(Neurotic::HostCost::Kind::TrackLifecycle, 1);
        if (!TracksGameState())
            return;

        // Fast path: get entry with read lock
        std::shared_ptr<CommandBufferStateEntry> entry;
        {
            std::shared_lock mapLock(_statesMapMutex);
            auto it = _states.find(cmd);
            if (it != _states.end())
                entry = it->second;
        }

        if (!entry)
            return;

        // Lock only this command buffer's state
        std::scoped_lock stateLock(entry->Mutex);
        nrHostCost.Acquired();
        if (entry->State)
            entry->State->ResetAll();
    }

    void OnResetPool(VkDevice device, VkCommandPool rawPool)
    {
        const PoolKey pool{device,rawPool};
        Neurotic::HostCost::Scope nrHostCost(Neurotic::HostCost::Kind::TrackLifecycle, 1);
        if (!TracksGameState())
            return;

        // Atomic increment - lock-free for the epoch counter
        uint64_t newEpoch = _globalEpochCounter.fetch_add(1, std::memory_order_acq_rel) + 1;

        // Need lock only to access/modify the map itself
        std::unique_lock poolLock(_poolMetadataMutex);

        auto epochIt = _poolEpochs.find(pool);
        if (epochIt != _poolEpochs.end())
        {
            epochIt->second->store(newEpoch, std::memory_order_release);
        }
        else
        {
            _poolEpochs[pool] = std::make_shared<std::atomic<uint64_t>>(newEpoch);
        }
    }

    void OnBindPipeline(VkCommandBuffer cmd, VkPipelineBindPoint bindPoint, VkPipeline pipeline)
    {
        Neurotic::HostCost::Scope nrHostCost(Neurotic::HostCost::Kind::TrackBinding, 64);
        if (!TracksGameState())
            return;

        const auto idx = ToIndex(bindPoint);
        if (!idx.has_value())
            return;

        // Fast path: Try to get existing entry with read lock
        std::shared_ptr<CommandBufferStateEntry> entry;
        {
            std::shared_lock mapLock(_statesMapMutex);
            auto it = _states.find(cmd);
            if (it != _states.end())
                entry = it->second;
        }

        // Slow path: Create new entry if needed
        if (!entry)
        {
            std::unique_lock mapLock(_statesMapMutex);
            auto& entryRef = _states[cmd];
            if (!entryRef)
                entryRef = std::make_shared<CommandBufferStateEntry>();
            entry = entryRef;
        }

        // Now lock only THIS command buffer's state
        std::scoped_lock stateLock(entry->Mutex);
        nrHostCost.Acquired();
        if (!entry->State)
            entry->State = std::make_shared<CommandBufferState>();

        entry->State->BP[static_cast<uint32_t>(*idx)].Pipeline = pipeline;
    }

    void OnBindDescriptorSets(VkCommandBuffer cmd, VkPipelineBindPoint bindPoint, VkPipelineLayout layout,
                              uint32_t firstSet, uint32_t descriptorSetCount, const VkDescriptorSet* pDescriptorSets,
                              uint32_t dynamicOffsetCount, const uint32_t* pDynamicOffsets)
    {
        Neurotic::HostCost::Scope nrHostCost(Neurotic::HostCost::Kind::TrackBinding, 64);
        if (!TracksGameState())
            return;

        const auto idx = ToIndex(bindPoint);
        if (!idx.has_value())
            return;

        // Fast path: Try to get existing entry with read lock
        std::shared_ptr<CommandBufferStateEntry> entry;
        {
            std::shared_lock mapLock(_statesMapMutex);
            auto it = _states.find(cmd);
            if (it != _states.end())
                entry = it->second;
        }

        // Slow path: Create new entry if needed
        if (!entry)
        {
            std::unique_lock mapLock(_statesMapMutex);
            auto& entryRef = _states[cmd];
            if (!entryRef)
                entryRef = std::make_shared<CommandBufferStateEntry>();
            entry = entryRef;
        }

        // Now lock only THIS command buffer's state
        std::scoped_lock stateLock(entry->Mutex);
        nrHostCost.Acquired();
        if (!entry->State)
            entry->State = std::make_shared<CommandBufferState>();

        auto& bp = entry->State->BP[static_cast<uint32_t>(*idx)];
        bp.CurrentPipelineLayout = layout;

        if ((descriptorSetCount && !pDescriptorSets) || (dynamicOffsetCount && !pDynamicOffsets))
        {
            LOG_ERROR("vkCmdBindDescriptorSets has a missing set or dynamic-offset array");
            return;
        }

        // Reuse a superseded exact range even when disjoint ranges alternate.
        // Moving an update across disjoint bindings with this identical layout
        // preserves set state and dynamic offsets. A different layout or an
        // overlapping range stops the search: disturbance/chronology must survive.
        size_t replacement = bp.DescriptorBindCalls.size();
        for (size_t i = bp.DescriptorBindCalls.size(); i > 0; --i)
        {
            const auto& previous = bp.DescriptorBindCalls[i - 1];
            if (previous.Layout != layout)
                break;
            if (previous.FirstSet == firstSet && previous.DescriptorSetCount == descriptorSetCount)
            {
                replacement = i - 1;
                break;
            }
            if (!descriptorSetCount || !previous.DescriptorSetCount ||
                (uint64_t(firstSet) < uint64_t(previous.FirstSet) + previous.DescriptorSetCount &&
                 uint64_t(previous.FirstSet) < uint64_t(firstSet) + descriptorSetCount))
                break;
        }
        if (replacement == bp.DescriptorBindCalls.size())
            bp.DescriptorBindCalls.emplace_back();
        const auto bindCallIndex = static_cast<uint32_t>(replacement);
        auto& bindCall = bp.DescriptorBindCalls[replacement];
        bindCall.Layout = layout;
        bindCall.FirstSet = firstSet;
        bindCall.DescriptorSetCount = descriptorSetCount;

        // Reuse capacity for the replacement call's arrays.
        if (descriptorSetCount > 0)
            bindCall.Sets.assign(pDescriptorSets, pDescriptorSets + descriptorSetCount);
        else
            bindCall.Sets.clear();

        if (dynamicOffsetCount > 0)
            bindCall.DynamicOffsets.assign(pDynamicOffsets, pDynamicOffsets + dynamicOffsetCount);
        else
            bindCall.DynamicOffsets.clear();

        // Update per-set tracking for quick queries
        for (uint32_t i = 0; i < descriptorSetCount; ++i)
        {
            uint32_t setIdx = firstSet + i;
            if (setIdx >= kMaxDescriptorSets)
                continue;

            auto& binding = bp.Sets[setIdx];
            binding.Bound = true;
            binding.Set = pDescriptorSets[i];
            binding.BoundWithLayout = layout;
            binding.BindCallIndex = bindCallIndex;
        }
    }

    void OnPushConstants(VkCommandBuffer cmd, VkPipelineBindPoint bindPoint, VkPipelineLayout layout,
                         VkShaderStageFlags stageFlags, uint32_t offset, uint32_t size, const void* pValues)
    {
        Neurotic::HostCost::Scope nrHostCost(Neurotic::HostCost::Kind::TrackBinding, 64);
        if (!TracksGameState())
            return;

        const auto idx = ToIndex(bindPoint);
        if (!idx.has_value())
            return;

        auto entry = GetOrCreateEntry(cmd, true);
        std::scoped_lock stateLock(entry->Mutex);
        nrHostCost.Acquired();

        auto& bp = entry->State->BP[static_cast<uint32_t>(*idx)];
        bp.CurrentPipelineLayout = layout;

        PushConstantEntry pushEntry;
        pushEntry.Layout = layout;
        pushEntry.Stages = stageFlags;
        pushEntry.Offset = offset;
        pushEntry.Size = size;

        if (size > 0 && pValues && size <= kMaxPushConstantBytes)
        {
            std::memcpy(&pushEntry.Data[0], pValues, size);
        }
        else if (size > kMaxPushConstantBytes && pValues)
        {
            std::memcpy(&pushEntry.Data[0], pValues, kMaxPushConstantBytes);
            pushEntry.Size = kMaxPushConstantBytes;
        }
        else if (size > 0 && !pValues)
        {
            LOG_ERROR("vkCmdPushConstants called with size={} but pValues=nullptr", size);
            return;
        }

        auto& history = entry->State->PushConstantHistory;
        // Only reorder across disjoint bytes/stages with an identical layout.
        // Overlapping mixed-stage writes retain their original chronology.
        for (size_t i = history.size(); i > 0; --i)
        {
            auto& previous = history[i - 1];
            if (previous.Layout != pushEntry.Layout)
                break;
            if (previous.Stages == pushEntry.Stages && previous.Offset == pushEntry.Offset &&
                previous.Size == pushEntry.Size)
            {
                previous = pushEntry;
                return;
            }
            if (!pushEntry.Size || !previous.Size ||
                ((previous.Stages & pushEntry.Stages) &&
                 uint64_t(pushEntry.Offset) < uint64_t(previous.Offset) + previous.Size &&
                 uint64_t(previous.Offset) < uint64_t(pushEntry.Offset) + pushEntry.Size))
                break;
        }
        history.push_back(pushEntry);
    }

    void OnSetViewport(VkCommandBuffer cmd, uint32_t first, uint32_t count, const VkViewport* pViewports)
    {
        Neurotic::HostCost::Scope nrHostCost(Neurotic::HostCost::Kind::TrackOther, 64);
        if (!TracksGameState())
            return;

        if (count > 0 && !pViewports)
        {
            LOG_ERROR("vkCmdSetViewport called with count={} but pViewports=nullptr", count);
            return;
        }

        auto entry = GetOrCreateEntry(cmd, true);
        std::scoped_lock stateLock(entry->Mutex);
        nrHostCost.Acquired();

        for (uint32_t i = 0; i < count; ++i)
        {
            uint32_t idx = first + i;
            if (idx < kMaxViewports)
            {
                entry->State->Dyn.Viewports[idx] = pViewports[i];
                entry->State->Dyn.ViewportValidMask.set(idx);
            }
        }
    }

    void OnSetScissor(VkCommandBuffer cmd, uint32_t first, uint32_t count, const VkRect2D* pScissors)
    {
        Neurotic::HostCost::Scope nrHostCost(Neurotic::HostCost::Kind::TrackOther, 64);
        if (!TracksGameState())
            return;

        if (count > 0 && !pScissors)
        {
            LOG_ERROR("vkCmdSetScissor called with count={} but pScissors=nullptr", count);
            return;
        }

        auto entry = GetOrCreateEntry(cmd, true);
        std::scoped_lock stateLock(entry->Mutex);
        nrHostCost.Acquired();

        for (uint32_t i = 0; i < count; ++i)
        {
            uint32_t idx = first + i;
            if (idx < kMaxScissors)
            {
                entry->State->Dyn.Scissors[idx] = pScissors[i];
                entry->State->Dyn.ScissorValidMask.set(idx);
            }
        }
    }

    void OnBindVertexBuffers(VkCommandBuffer cmd, uint32_t first, uint32_t count, const VkBuffer* pBuffers,
                             const VkDeviceSize* pOffsets)
    {
        Neurotic::HostCost::Scope nrHostCost(Neurotic::HostCost::Kind::TrackBinding, 64);
        if (!TracksGameState())
            return;

        if (count > 0 && (!pBuffers || !pOffsets))
        {
            LOG_ERROR("vkCmdBindVertexBuffers called with count={} but pBuffers={} or pOffsets={}", count,
                      (void*) pBuffers, (void*) pOffsets);
            return;
        }

        auto entry = GetOrCreateEntry(cmd, true);
        std::scoped_lock stateLock(entry->Mutex);
        nrHostCost.Acquired();

        for (uint32_t i = 0; i < count; ++i)
        {
            uint32_t idx = first + i;
            if (idx < kMaxVertexBuffers)
            {
                entry->State->VI.Buffers[idx] = pBuffers[i];
                entry->State->VI.Offsets[idx] = pOffsets[i];
                entry->State->VI.BufferValid.set(idx);
            }
        }
    }

    void OnBindIndexBuffer(VkCommandBuffer cmd, VkBuffer buffer, VkDeviceSize offset, VkIndexType indexType)
    {
        Neurotic::HostCost::Scope nrHostCost(Neurotic::HostCost::Kind::TrackBinding, 64);
        if (!TracksGameState())
            return;

        auto entry = GetOrCreateEntry(cmd, true);
        std::scoped_lock stateLock(entry->Mutex);
        nrHostCost.Acquired();

        entry->State->VI.IndexBufferValid = true;
        entry->State->VI.IndexBuffer = buffer;
        entry->State->VI.IndexOffset = offset;
        entry->State->VI.IndexType = indexType;
    }

    void OnPipelineBarrier(VkCommandBuffer cmd, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask,
                           VkDependencyFlags dependencyFlags, uint32_t memoryBarrierCount,
                           const VkMemoryBarrier* pMemoryBarriers, uint32_t bufferMemoryBarrierCount,
                           const VkBufferMemoryBarrier* pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount,
                           const VkImageMemoryBarrier* pImageMemoryBarriers)
    {
        Neurotic::HostCost::Scope nrHostCost(Neurotic::HostCost::Kind::TrackBarrier, 64);
#ifndef LOW_PRECISION_TRACKING
        if (!TracksGameState())
            return;

        auto entry = GetOrCreateEntry(cmd, true);
        std::scoped_lock stateLock(entry->Mutex);
        nrHostCost.Acquired();

        for (uint32_t i = 0; i < imageMemoryBarrierCount; ++i)
        {
            const auto& barrier = pImageMemoryBarriers[i];
            entry->State->ImageLayouts[barrier.image] = barrier.newLayout;
            entry->State->ImageLayoutRanges[barrier.image] = barrier.subresourceRange;
        }
#endif
    }

    void OnPipelineBarrier2(VkCommandBuffer cmd, const VkDependencyInfo* info)
    {
        Neurotic::HostCost::Scope nrHostCost(Neurotic::HostCost::Kind::TrackBarrier, 64);
#ifndef LOW_PRECISION_TRACKING
        if (!TracksGameState() || !info) return;
        auto entry = GetOrCreateEntry(cmd,true);
        std::scoped_lock stateLock(entry->Mutex);
        nrHostCost.Acquired();
        for (uint32_t i=0;i<info->imageMemoryBarrierCount;++i) {
            const auto& b=info->pImageMemoryBarriers[i];
            // Synchronization2 ignores layout values on an equal-layout,
            // equal-family dependency. It cannot establish or replace a layout observation.
            if (b.oldLayout==b.newLayout && b.srcQueueFamilyIndex==b.dstQueueFamilyIndex) continue;
            entry->State->ImageLayouts[b.image]=b.newLayout;
            entry->State->ImageLayoutRanges[b.image]=b.subresourceRange;
        }
#endif
    }

    void OnSetCullMode(VkCommandBuffer cmd, VkCullModeFlags cullMode)
    {
        Neurotic::HostCost::Scope nrHostCost(Neurotic::HostCost::Kind::TrackOther, 64);
        if (!TracksGameState())
            return;

        auto entry = GetOrCreateEntry(cmd, true);
        std::scoped_lock stateLock(entry->Mutex);
        nrHostCost.Acquired();

        entry->State->Dyn.CullMode = cullMode;
        entry->State->Dyn.CullModeSet = true;
    }

    void OnSetFrontFace(VkCommandBuffer cmd, VkFrontFace frontFace)
    {
        Neurotic::HostCost::Scope nrHostCost(Neurotic::HostCost::Kind::TrackOther, 64);
        if (!TracksGameState())
            return;

        auto entry = GetOrCreateEntry(cmd, true);
        std::scoped_lock stateLock(entry->Mutex);
        nrHostCost.Acquired();

        entry->State->Dyn.FrontFace = frontFace;
        entry->State->Dyn.FrontFaceSet = true;
    }

    void OnSetPrimitiveTopology(VkCommandBuffer cmd, VkPrimitiveTopology primitiveTopology)
    {
        Neurotic::HostCost::Scope nrHostCost(Neurotic::HostCost::Kind::TrackOther, 64);
        if (!TracksGameState())
            return;

        auto entry = GetOrCreateEntry(cmd, true);
        std::scoped_lock stateLock(entry->Mutex);
        nrHostCost.Acquired();

        entry->State->Dyn.PrimitiveTopology = primitiveTopology;
        entry->State->Dyn.PrimitiveTopologySet = true;
    }

    void OnSetDepthTestEnable(VkCommandBuffer cmd, VkBool32 depthTestEnable)
    {
        Neurotic::HostCost::Scope nrHostCost(Neurotic::HostCost::Kind::TrackOther, 64);
#ifndef LOW_PRECISION_TRACKING
        if (!TracksGameState())
            return;

        auto entry = GetOrCreateEntry(cmd, true);
        std::scoped_lock stateLock(entry->Mutex);
        nrHostCost.Acquired();

        entry->State->Dyn.DepthTestEnable = depthTestEnable;
        entry->State->Dyn.DepthTestEnableSet = true;
#endif
    }

    void OnSetDepthWriteEnable(VkCommandBuffer cmd, VkBool32 depthWriteEnable)
    {
        Neurotic::HostCost::Scope nrHostCost(Neurotic::HostCost::Kind::TrackOther, 64);
#ifndef LOW_PRECISION_TRACKING
        if (!TracksGameState())
            return;

        auto entry = GetOrCreateEntry(cmd, true);
        std::scoped_lock stateLock(entry->Mutex);
        nrHostCost.Acquired();

        entry->State->Dyn.DepthWriteEnable = depthWriteEnable;
        entry->State->Dyn.DepthWriteEnableSet = true;
#endif
    }

    void OnSetDepthCompareOp(VkCommandBuffer cmd, VkCompareOp depthCompareOp)
    {
        Neurotic::HostCost::Scope nrHostCost(Neurotic::HostCost::Kind::TrackOther, 64);
#ifndef LOW_PRECISION_TRACKING
        if (!TracksGameState())
            return;

        auto entry = GetOrCreateEntry(cmd, true);
        std::scoped_lock stateLock(entry->Mutex);
        nrHostCost.Acquired();

        entry->State->Dyn.DepthCompareOp = depthCompareOp;
        entry->State->Dyn.DepthCompareOpSet = true;
#endif
    }

    void OnSetDepthBoundsTestEnable(VkCommandBuffer cmd, VkBool32 depthBoundsTestEnable)
    {
        Neurotic::HostCost::Scope nrHostCost(Neurotic::HostCost::Kind::TrackOther, 64);
#ifndef LOW_PRECISION_TRACKING
        if (!TracksGameState())
            return;

        auto entry = GetOrCreateEntry(cmd, true);
        std::scoped_lock stateLock(entry->Mutex);
        nrHostCost.Acquired();

        entry->State->Dyn.DepthBoundsTestEnable = depthBoundsTestEnable;
        entry->State->Dyn.DepthBoundsTestEnableSet = true;
#endif
    }

    void OnSetStencilTestEnable(VkCommandBuffer cmd, VkBool32 stencilTestEnable)
    {
        Neurotic::HostCost::Scope nrHostCost(Neurotic::HostCost::Kind::TrackOther, 64);
#ifndef LOW_PRECISION_TRACKING
        if (!TracksGameState())
            return;

        auto entry = GetOrCreateEntry(cmd, true);
        std::scoped_lock stateLock(entry->Mutex);
        nrHostCost.Acquired();

        entry->State->Dyn.StencilTestEnable = stencilTestEnable;
        entry->State->Dyn.StencilTestEnableSet = true;
#endif
    }

    void OnSetStencilOp(VkCommandBuffer cmd, VkStencilFaceFlags faceMask, VkStencilOp failOp, VkStencilOp passOp,
                        VkStencilOp depthFailOp, VkCompareOp compareOp)
    {
        Neurotic::HostCost::Scope nrHostCost(Neurotic::HostCost::Kind::TrackOther, 64);
#ifndef LOW_PRECISION_TRACKING
        if (!TracksGameState())
            return;

        auto entry = GetOrCreateEntry(cmd, true);
        std::scoped_lock stateLock(entry->Mutex);
        nrHostCost.Acquired();

        entry->State->Dyn.StencilOpFaceMask = faceMask;
        entry->State->Dyn.StencilFailOp = failOp;
        entry->State->Dyn.StencilPassOp = passOp;
        entry->State->Dyn.StencilDepthFailOp = depthFailOp;
        entry->State->Dyn.StencilCompareOp = compareOp;
        entry->State->Dyn.StencilOpSet = true;
#endif
    }

    // NEW: Render pass tracking
    void OnBeginRenderPass(VkCommandBuffer cmd, const VkRenderPassBeginInfo* pRenderPassBegin)
    {
        Neurotic::HostCost::Scope nrHostCost(Neurotic::HostCost::Kind::TrackOther, 64);
#ifndef LOW_PRECISION_TRACKING
        if (!TracksGameState())
            return;

        auto entry = GetOrCreateEntry(cmd, true);
        std::scoped_lock stateLock(entry->Mutex);
        nrHostCost.Acquired();

        entry->State->InRenderPass = true;
        entry->State->ActiveRenderPass = pRenderPassBegin ? pRenderPassBegin->renderPass : VK_NULL_HANDLE;
        entry->State->ActiveFramebuffer = pRenderPassBegin ? pRenderPassBegin->framebuffer : VK_NULL_HANDLE;
#endif
    }

    void OnEndRenderPass(VkCommandBuffer cmd)
    {
        Neurotic::HostCost::Scope nrHostCost(Neurotic::HostCost::Kind::TrackOther, 64);
#ifndef LOW_PRECISION_TRACKING
        if (!TracksGameState())
            return;

        auto entry = GetOrCreateEntry(cmd, true);
        std::scoped_lock stateLock(entry->Mutex);
        nrHostCost.Acquired();

        entry->State->InRenderPass = false;
#endif
    }

    void OnCommandBufferDestroyed(VkCommandBuffer cmd)
    {
        Neurotic::HostCost::Scope nrHostCost(Neurotic::HostCost::Kind::TrackOther, 64);
        std::unique_lock lock(_statesMapMutex);
        _states.erase(cmd);
    }

    void OnFreeCommandBuffers(VkCommandPool pool, uint32_t count, const VkCommandBuffer* pCommandBuffers)
    {
        Neurotic::HostCost::Scope nrHostCost(Neurotic::HostCost::Kind::TrackOther, 64);
        std::unique_lock lock(_statesMapMutex);
        for (uint32_t i = 0; i < count; ++i)
        {
            _states.erase(pCommandBuffers[i]);
            _cmdBufferToPool.erase(pCommandBuffers[i]);
        }

        // LOG_DEBUG("Freed {} command buffers from pool {:X}", count, (size_t) pool.second);
    }

    // Call this when a command pool is destroyed
    void OnDestroyPool(VkDevice device, VkCommandPool rawPool)
    {
        const PoolKey pool{device,rawPool};
        Neurotic::HostCost::Scope nrHostCost(Neurotic::HostCost::Kind::TrackLifecycle, 1);
        if (!TracksGameState())
            return;

        std::unique_lock poolLock(_poolMetadataMutex);
        std::unique_lock mapLock(_statesMapMutex);

        // Remove all command buffers allocated from this pool
        for (auto it = _cmdBufferToPool.begin(); it != _cmdBufferToPool.end();)
        {
            if (it->second == pool)
            {
                _states.erase(it->first);
                it = _cmdBufferToPool.erase(it);
            }
            else
            {
                ++it;
            }
        }

        _poolEpochs.erase(pool);
        _poolToQueueFamily.erase(pool);
        LOG_DEBUG("Pool {:X} destroyed - removed all associated command buffers", (size_t) pool.second);
    }

    bool CaptureAndReplay(VkCommandBuffer srcCmd, VkCommandBuffer dstCmd, const ReplayParams& params) const
    {
        if (!TracksGameState())
            return false;

        if (!_hasCachedFns)
        {
            LOG_ERROR("Function table not initialized! Call SetFunctionTable() first.");
            return false;
        }

        return CaptureAndReplay(srcCmd, dstCmd, _cachedFns, params);
    }

    bool ReplayForGraphicsDraw(const VulkanCmdFns& fns, VkCommandBuffer srcCmd, VkCommandBuffer dstCmd,
                               const ReplayParams& params) const
    {
        if (!TracksGameState())
            return false;

        CommandBufferState snapshot;
        if (!TryGetSnapshot(srcCmd, snapshot))
        {
            LOG_WARN("Failed to get snapshot for command buffer {:p} - may have been invalidated by pool reset",
                     (void*) srcCmd);
            return false;
        }

        // 1. Pipeline
        if (params.ReplayGraphicsPipeline)
        {
            auto& gfx = snapshot.BP[static_cast<uint32_t>(BindPointIndex::Graphics)];
            if (gfx.Pipeline && fns.CmdBindPipeline)
                fns.CmdBindPipeline(dstCmd, VK_PIPELINE_BIND_POINT_GRAPHICS, gfx.Pipeline);
        }

        // 2. Descriptor Sets - use unified helper with slicing
        {
            auto& gfx = snapshot.BP[static_cast<uint32_t>(BindPointIndex::Graphics)];
            ReplayDescriptorSets(fns, dstCmd, gfx, VK_PIPELINE_BIND_POINT_GRAPHICS, params.RequiredGraphicsSetMask,
                                 params.OverrideGraphicsLayout);
        }

        // 3. Push Constants
        if (params.ReplayPushConstants)
        {
            // Replay push constants from global timeline in order
            // Filter by stage mask compatibility (optional) and layout compatibility
            constexpr VkShaderStageFlags graphicsStages =
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT |
                VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT | VK_SHADER_STAGE_GEOMETRY_BIT |
                VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_TASK_BIT_EXT | VK_SHADER_STAGE_MESH_BIT_EXT;

            for (const auto& entry : snapshot.PushConstantHistory)
            {
                // Optional: filter to graphics-relevant stages (conservative - keeps ALL_GRAPHICS too)
                // Skip only if exclusively compute/ray-tracing stages
                bool hasGraphicsStages = (entry.Stages & graphicsStages) != 0;
                bool hasAllGraphics = (entry.Stages & VK_SHADER_STAGE_ALL_GRAPHICS) != 0;

                if (!hasGraphicsStages && !hasAllGraphics)
                {
                    // This is exclusively compute or ray tracing - skip for graphics replay
                    continue;
                }

                VkPipelineLayout layoutToUse =
                    params.OverrideGraphicsLayout ? params.OverrideGraphicsLayout : entry.Layout;

                if (layoutToUse && entry.Size > 0 && fns.CmdPushConstants)
                {
                    fns.CmdPushConstants(dstCmd, layoutToUse, entry.Stages, entry.Offset, entry.Size, &entry.Data[0]);
                }
            }
        }

        // 4. Dynamic State
        if (params.ReplayViewportScissor)
        {
            ReplayViewports(fns, dstCmd, snapshot.Dyn);
            ReplayScissors(fns, dstCmd, snapshot.Dyn);
        }

        // 4.5. Extended Dynamic State
        if (params.ReplayExtendedDynamicState)
        {
            ReplayExtendedDynamicState(fns, dstCmd, snapshot.Dyn);
        }

        // 5. Vertex/Index
        if (params.ReplayVertexIndex)
        {
            ReplayVertexBuffers(fns, dstCmd, snapshot.VI);
            if (snapshot.VI.IndexBufferValid && fns.CmdBindIndexBuffer)
            {
                fns.CmdBindIndexBuffer(dstCmd, snapshot.VI.IndexBuffer, snapshot.VI.IndexOffset, snapshot.VI.IndexType);
            }
        }

        return true;
    }

    void SetFunctionTable(const VulkanCmdFns& fns)
    {
        _cachedFns = fns;
        _hasCachedFns = true;
    }

    std::optional<uint32_t> GetCommandBufferQueueFamily(VkCommandBuffer cmd) const
    {
        // Phase 1: Get pool handle (only _statesMapMutex)
        PoolKey pool{};
        {
            std::shared_lock mapLock(_statesMapMutex);
            auto poolIt = _cmdBufferToPool.find(cmd);
            if (poolIt == _cmdBufferToPool.end())
            {
                LOG_WARN("Command buffer {:X} not tracked in any pool", (size_t) cmd);
                return std::nullopt;
            }
            pool = poolIt->second;
        }
        // _statesMapMutex fully released here before touching _poolMetadataMutex

        // Phase 2: Get queue family (only _poolMetadataMutex)
        {
            std::shared_lock poolLock(_poolMetadataMutex);
            auto familyIt = _poolToQueueFamily.find(pool);
            if (familyIt == _poolToQueueFamily.end())
            {
                LOG_WARN("Pool {:X} has no queue family info", (size_t) pool.second);
                return std::nullopt;
            }
            return familyIt->second;
        }
    }

  private:
    inline static State* _state;
    inline static std::atomic<bool> nativeDeviceObserved_ {false};
    bool TracksGameState() const noexcept
    { return DlssNr::TrackVkNrCommandState(_state->currentFeature && _state->currentFeature->IsWithDx12(),nativeDeviceObserved_.load(std::memory_order_relaxed)); }

    // Helper method to replay descriptor sets with proper slicing and timeline ordering
    void ReplayDescriptorSets(const VulkanCmdFns& fns, VkCommandBuffer dstCmd, const BindPointState& bindPoint,
                              VkPipelineBindPoint bindPointType, uint32_t requiredSetMask,
                              VkPipelineLayout overrideLayout) const
    {
        if (!fns.CmdBindDescriptorSets)
            return;

        // Track which bind calls contain at least one required set - use dynamic container to handle any number of
        // calls
        const size_t numCalls = bindPoint.DescriptorBindCalls.size();
        if (numCalls == 0)
            return;

        std::vector<bool> callsNeeded(numCalls, false);

        // Pre-compute and cache range end for each call to avoid redundant overflow checks
        std::vector<uint32_t> callEnds(numCalls);
        std::vector<bool> callRangeValid(numCalls, false);

        for (size_t callIdx = 0; callIdx < numCalls; ++callIdx)
        {
            const auto& call = bindPoint.DescriptorBindCalls[callIdx];
            const uint64_t callEnd64 = (uint64_t) call.FirstSet + call.DescriptorSetCount;

            // Validate range with overflow check
            if (call.DescriptorSetCount > 0)
            {
                if (callEnd64 > (uint64_t) UINT32_MAX + 1ull || (uint32_t) callEnd64 < call.FirstSet)
                {
                    LOG_ERROR("Descriptor set call {} range overflow (firstSet={}, count={}) - skipping", callIdx,
                              call.FirstSet, call.DescriptorSetCount);
                    continue;
                }
            }

            callEnds[callIdx] = (uint32_t) callEnd64;
            callRangeValid[callIdx] = true;
        }

        // First pass: identify which calls contain any required set
        for (uint32_t setIdx = 0; setIdx < kMaxDescriptorSets; ++setIdx)
        {
            if (!((requiredSetMask >> setIdx) & 1))
                continue;

            const auto& binding = bindPoint.Sets[setIdx];
            if (!binding.Bound || binding.Set == VK_NULL_HANDLE)
                continue;

            uint32_t callIdx = binding.BindCallIndex;
            if (callIdx >= numCalls)
            {
                LOG_WARN("Set {} references bind call {} but only {} calls exist - skipping", setIdx, callIdx,
                         numCalls);
                continue;
            }

            if (!callRangeValid[callIdx])
                continue; // Already logged error during range validation

            // Validate that this set actually belongs to this call's range
            const auto& call = bindPoint.DescriptorBindCalls[callIdx];
            uint32_t callEnd = callEnds[callIdx];

            if (setIdx < call.FirstSet || setIdx >= callEnd)
            {
                // Only warn if count > 0 (avoid underflow in log message)
                if (call.DescriptorSetCount > 0)
                {
                    LOG_WARN("Set {} tracked with call {} but outside call range [{}..{}] - skipping", setIdx, callIdx,
                             call.FirstSet, callEnd - 1);
                }
                continue;
            }

            // Mark this call as needed
            callsNeeded[callIdx] = true;
        }

        // Second pass: replay needed calls in timeline order
        for (uint32_t callIdx = 0; callIdx < numCalls; ++callIdx)
        {
            if (!callsNeeded[callIdx] || !callRangeValid[callIdx])
                continue;

            const auto& call = bindPoint.DescriptorBindCalls[callIdx];
            uint32_t callEnd = callEnds[callIdx]; // Use cached value

            VkPipelineLayout layoutToUse = overrideLayout ? overrideLayout : call.Layout;

            if (!layoutToUse)
                continue;

            // Validate consistency between DescriptorSetCount and Sets.size()
            if (call.DescriptorSetCount > call.Sets.size())
            {
                LOG_ERROR("Descriptor set call {} has count={} but Sets.size()={} - skipping to avoid driver crash",
                          callIdx, call.DescriptorSetCount, call.Sets.size());
                continue;
            }

            // CRITICAL: If this call has dynamic offsets, we MUST replay it verbatim (no slicing)
            // Dynamic offsets are paired with descriptor sets in a complex way that requires
            // pipeline layout introspection to understand - without that, slicing is unsafe
            if (!call.DynamicOffsets.empty())
            {
                // Additional safety check for verbatim replay path
                const VkDescriptorSet* pSetsToUse = (call.DescriptorSetCount > 0) ? call.Sets.data() : nullptr;

                // Replay the entire original call verbatim
                fns.CmdBindDescriptorSets(dstCmd, bindPointType, layoutToUse, call.FirstSet, call.DescriptorSetCount,
                                          pSetsToUse, (uint32_t) call.DynamicOffsets.size(),
                                          call.DynamicOffsets.data());

                LOG_DEBUG("Replayed descriptor set call {} verbatim (has {} dynamic offsets, firstSet={}, count={})",
                          callIdx, call.DynamicOffsets.size(), call.FirstSet, call.DescriptorSetCount);
                continue;
            }

            // No dynamic offsets - safe to slice to only the required sets
            // Build list of which sets from this call are actually required
            std::vector<uint32_t> requiredSetIndices; // Absolute set indices

            for (uint32_t setIdx = 0; setIdx < kMaxDescriptorSets; ++setIdx)
            {
                if (!((requiredSetMask >> setIdx) & 1))
                    continue;

                const auto& binding = bindPoint.Sets[setIdx];
                if (!binding.Bound || binding.Set == VK_NULL_HANDLE)
                    continue;

                if (binding.BindCallIndex != callIdx)
                    continue;

                // Validate set is within call range (using pre-computed safe callEnd)
                if (setIdx >= call.FirstSet && setIdx < callEnd)
                {
                    requiredSetIndices.push_back(setIdx);
                }
            }

            if (requiredSetIndices.empty())
                continue;

            // requiredSetIndices should already be sorted (we iterate setIdx in order)
            // but sort anyway for robustness
            std::sort(requiredSetIndices.begin(), requiredSetIndices.end());

            // Group into contiguous ranges for efficient binding
            for (size_t i = 0; i < requiredSetIndices.size();)
            {
                uint32_t rangeStart = requiredSetIndices[i];
                uint32_t rangeEnd = rangeStart;

                // Find end of contiguous range
                while (i + 1 < requiredSetIndices.size() && requiredSetIndices[i + 1] == rangeEnd + 1)
                {
                    rangeEnd = requiredSetIndices[++i];
                }
                i++;

                uint32_t rangeCount = rangeEnd - rangeStart + 1;

                // Extract sets for this range from the original call
                std::vector<VkDescriptorSet> setsToRebind;
                setsToRebind.reserve(rangeCount);

                // Convert absolute indices to call-relative indices and extract sets
                bool allValid = true;
                for (uint32_t absoluteSetIdx = rangeStart; absoluteSetIdx <= rangeEnd; ++absoluteSetIdx)
                {
                    if (absoluteSetIdx < call.FirstSet || absoluteSetIdx >= callEnd)
                    {
                        LOG_ERROR("Set {} outside call range [{}, {}) - internal error", absoluteSetIdx, call.FirstSet,
                                  callEnd);
                        allValid = false;
                        break;
                    }

                    uint32_t setIndexInCall = absoluteSetIdx - call.FirstSet;

                    if (setIndexInCall >= call.Sets.size())
                    {
                        LOG_ERROR("Set index {} maps to out-of-bounds call array index {} (size {})", absoluteSetIdx,
                                  setIndexInCall, call.Sets.size());
                        allValid = false;
                        break;
                    }

                    setsToRebind.push_back(call.Sets[setIndexInCall]);
                }

                // Replay this contiguous range if all sets were valid
                if (allValid && !setsToRebind.empty())
                {
                    fns.CmdBindDescriptorSets(dstCmd, bindPointType, layoutToUse, rangeStart,
                                              (uint32_t) setsToRebind.size(), setsToRebind.data(), 0, nullptr);
                }
            }
        }
    }

    void ReplayVertexBuffers(const VulkanCmdFns& fns, VkCommandBuffer dst, const VertexInputState& vi) const
    {
        if (!fns.CmdBindVertexBuffers)
            return;

        uint32_t start = 0;
        while (start < kMaxVertexBuffers)
        {
            if (!vi.BufferValid.test(start))
            {
                start++;
                continue;
            }

            uint32_t count = 0;
            while ((start + count) < kMaxVertexBuffers && vi.BufferValid.test(start + count))
            {
                count++;
            }

            const VkBuffer* pBuf = &vi.Buffers[start];
            const VkDeviceSize* pOff = &vi.Offsets[start];
            fns.CmdBindVertexBuffers(dst, start, count, pBuf, pOff);

            start += count;
        }
    }

    void ReplayViewports(const VulkanCmdFns& fns, VkCommandBuffer dst, const DynamicState& dyn) const
    {
        if (!fns.CmdSetViewport)
            return;

        uint32_t start = 0;
        while (start < kMaxViewports)
        {
            if (!dyn.ViewportValidMask.test(start))
            {
                start++;
                continue;
            }

            uint32_t count = 0;
            while (start + count < kMaxViewports && dyn.ViewportValidMask.test(start + count))
            {
                count++;
            }

            fns.CmdSetViewport(dst, start, count, &dyn.Viewports[start]);
            start += count;
        }
    }

    void ReplayScissors(const VulkanCmdFns& fns, VkCommandBuffer dst, const DynamicState& dyn) const
    {
        if (!fns.CmdSetScissor)
            return;

        uint32_t start = 0;
        while (start < kMaxScissors)
        {
            if (!dyn.ScissorValidMask.test(start))
            {
                start++;
                continue;
            }

            uint32_t count = 0;
            while (start + count < kMaxScissors && dyn.ScissorValidMask.test(start + count))
            {
                count++;
            }

            fns.CmdSetScissor(dst, start, count, &dyn.Scissors[start]);
            start += count;
        }
    }

    void ReplayExtendedDynamicState(const VulkanCmdFns& fns, VkCommandBuffer dst, const DynamicState& dyn) const
    {
        // Only replay if actually set by the app
        if (dyn.CullModeSet && fns.CmdSetCullMode)
            fns.CmdSetCullMode(dst, dyn.CullMode);

        if (dyn.FrontFaceSet && fns.CmdSetFrontFace)
            fns.CmdSetFrontFace(dst, dyn.FrontFace);

        if (dyn.PrimitiveTopologySet && fns.CmdSetPrimitiveTopology)
            fns.CmdSetPrimitiveTopology(dst, dyn.PrimitiveTopology);

#ifndef LOW_PRECISION_TRACKING
        if (dyn.DepthTestEnableSet && fns.CmdSetDepthTestEnable)
            fns.CmdSetDepthTestEnable(dst, dyn.DepthTestEnable);

        if (dyn.DepthWriteEnableSet && fns.CmdSetDepthWriteEnable)
            fns.CmdSetDepthWriteEnable(dst, dyn.DepthWriteEnable);

        if (dyn.DepthCompareOpSet && fns.CmdSetDepthCompareOp)
            fns.CmdSetDepthCompareOp(dst, dyn.DepthCompareOp);

        if (dyn.DepthBoundsTestEnableSet && fns.CmdSetDepthBoundsTestEnable)
            fns.CmdSetDepthBoundsTestEnable(dst, dyn.DepthBoundsTestEnable);

        if (dyn.StencilTestEnableSet && fns.CmdSetStencilTestEnable)
            fns.CmdSetStencilTestEnable(dst, dyn.StencilTestEnable);

        if (dyn.StencilOpSet && fns.CmdSetStencilOp)
        {
            fns.CmdSetStencilOp(dst, dyn.StencilOpFaceMask, dyn.StencilFailOp, dyn.StencilPassOp,
                                dyn.StencilDepthFailOp, dyn.StencilCompareOp);
        }
#endif
    }

  public:
    void EnableNativeObservations() { nativeDeviceObserved_.store(true,std::memory_order_release); }
    bool HasFunctionTable() const noexcept { return _hasCachedFns; }
    bool CaptureNrState(VkCommandBuffer cmd, CommandBufferState& out) const
    { return _hasCachedFns && TryGetSnapshot(cmd,out); }
    bool ReplaySaved(VkCommandBuffer cmd, const CommandBufferState& saved, const ReplayParams& params) const
    {
        return _hasCachedFns && ReplayFromSnapshot(_cachedFns, saved, cmd, params);
    }

  private:

    bool TryGetSnapshot(VkCommandBuffer cmd, CommandBufferState& out) const
    {
        Neurotic::HostCost::Scope nrHostCost(Neurotic::HostCost::Kind::TrackSnapshot, 1);
        // Get entry with read lock
        std::shared_ptr<CommandBufferStateEntry> entry;
        {
            std::shared_lock mapLock(_statesMapMutex);
            auto it = _states.find(cmd);
            if (it == _states.end())
                return false;
            entry = it->second;
        }

        if (!entry)
            return false;

        // Get pool info
        PoolKey pool{};
        {
            std::shared_lock mapLock(_statesMapMutex);
            auto poolIt = _cmdBufferToPool.find(cmd);
            if (poolIt == _cmdBufferToPool.end())
            {
                LOG_WARN("Command buffer {:p} not tracked in any pool", (void*) cmd);
                return false;
            }
            pool = poolIt->second;
        }

        // Lock-free atomic read
        std::shared_lock poolLock(_poolMetadataMutex);
        auto epochIt = _poolEpochs.find(pool);
        if (epochIt == _poolEpochs.end())
        {
            LOG_ERROR("Pool {:X} has no epoch entry", (size_t) pool.second);
            return false;
        }

        uint64_t currentPoolEpoch = epochIt->second->load(std::memory_order_acquire);
        poolLock.unlock();

        // Lock THIS command buffer's state for snapshot
        std::scoped_lock stateLock(entry->Mutex);
        nrHostCost.Acquired();
        if (!entry->State)
            return false;

        if (entry->State->BeginEpoch < currentPoolEpoch)
        {
            LOG_WARN("Command buffer {:p} has stale state (epoch {} < pool {:X} epoch {})", (void*) cmd,
                     entry->State->BeginEpoch, (size_t) pool.second, currentPoolEpoch);
            return false;
        }

        out = *entry->State;
        return true;
    }

    bool CaptureAndReplay(VkCommandBuffer srcCmd, VkCommandBuffer dstCmd, const VulkanCmdFns& fns,
                          const ReplayParams& params) const
    {
        CommandBufferState snapshot;

        // Capture state from source command buffer
        {
            // Get entry with read lock
            std::shared_ptr<CommandBufferStateEntry> entry;
            {
                std::shared_lock mapLock(_statesMapMutex);
                auto it = _states.find(srcCmd);
                if (it == _states.end())
                {
                    LOG_WARN("Can't found captured state for command buffer {:p}", (void*) srcCmd);
                    return false;
                }
                entry = it->second;
            }

            if (!entry)
            {
                LOG_WARN("Captured state is empty for command buffer {:p}", (void*) srcCmd);
                return false;
            }

            // Get pool info
            PoolKey pool{};
            {
                std::shared_lock mapLock(_statesMapMutex);
                auto poolIt = _cmdBufferToPool.find(srcCmd);
                if (poolIt == _cmdBufferToPool.end())
                {
                    LOG_WARN("Command buffer {:p} not tracked in any pool (allocation hook missed?). "
                             "Cannot validate epoch - refusing replay for safety.",
                             (void*) srcCmd);
                    return false;
                }
                pool = poolIt->second;
            }

            // Lock-free atomic read
            std::shared_lock poolLock(_poolMetadataMutex);
            auto epochIt = _poolEpochs.find(pool);
            if (epochIt == _poolEpochs.end())
            {
                LOG_ERROR("Pool {:X} for command buffer {:p} has no epoch entry. Internal state corruption?",
                          (size_t) pool.second, (void*) srcCmd);
                return false;
            }

            uint64_t currentPoolEpoch = epochIt->second->load(std::memory_order_acquire);
            poolLock.unlock();

            // Lock THIS command buffer's state for deep copy
            std::scoped_lock stateLock(entry->Mutex);
            if (!entry->State)
            {
                LOG_WARN("Captured state is empty for command buffer {:p}", (void*) srcCmd);
                return false;
            }

            if (entry->State->BeginEpoch < currentPoolEpoch)
            {
                LOG_WARN("Command buffer {:p} has stale state (epoch {} < pool {:X} epoch {}), refusing replay. "
                         "This command buffer was invalidated by vkResetCommandPool and must not be used until "
                         "vkBeginCommandBuffer is called.",
                         (void*) srcCmd, entry->State->BeginEpoch, (size_t) pool.second, currentPoolEpoch);
                return false;
            }

            // Deep copy the state - now a true immutable snapshot
            snapshot = *entry->State;
        }

        // Replay to destination (no lock needed - working with copied snapshot)
        return ReplayFromSnapshot(fns, snapshot, dstCmd, params);
    }

    bool ReplayFromSnapshot(const VulkanCmdFns& fns, const CommandBufferState& snapshot, VkCommandBuffer dstCmd,
                            const ReplayParams& params) const
    {
        // 1. Graphics Pipeline
        if (params.ReplayGraphicsPipeline)
        {
            auto& gfx = snapshot.BP[static_cast<uint32_t>(BindPointIndex::Graphics)];
            if (gfx.Pipeline && fns.CmdBindPipeline)
                fns.CmdBindPipeline(dstCmd, VK_PIPELINE_BIND_POINT_GRAPHICS, gfx.Pipeline);
        }

        // 1.5. Compute Pipeline (if requested)
        if (params.ReplayComputeToo)
        {
            auto& comp = snapshot.BP[static_cast<uint32_t>(BindPointIndex::Compute)];
            if (comp.Pipeline && fns.CmdBindPipeline)
                fns.CmdBindPipeline(dstCmd, VK_PIPELINE_BIND_POINT_COMPUTE, comp.Pipeline);
        }

        // 2. Descriptor Sets - Graphics
        {
            auto& gfx = snapshot.BP[static_cast<uint32_t>(BindPointIndex::Graphics)];
            ReplayDescriptorSets(fns, dstCmd, gfx, VK_PIPELINE_BIND_POINT_GRAPHICS, params.RequiredGraphicsSetMask,
                                 params.OverrideGraphicsLayout);
        }

        // 2.5. Descriptor Sets - Compute (if requested)
        if (params.ReplayComputeToo)
        {
            auto& comp = snapshot.BP[static_cast<uint32_t>(BindPointIndex::Compute)];

            // For compute, replay all descriptor bind calls verbatim in timeline order
            // This avoids the kMaxDescriptorSets limitation and ensures correctness
            if (fns.CmdBindDescriptorSets)
            {
                for (const auto& call : comp.DescriptorBindCalls)
                {
                    if (!call.Layout || call.DescriptorSetCount == 0)
                        continue;

                    // Validate consistency before replay
                    if (call.DescriptorSetCount > call.Sets.size())
                    {
                        LOG_ERROR("Compute descriptor set call has count={} but Sets.size()={} - skipping",
                                  call.DescriptorSetCount, call.Sets.size());
                        continue;
                    }

                    // Sanity check: validate dynamic offset data consistency
                    if (!call.DynamicOffsets.empty() && call.DescriptorSetCount == 0)
                    {
                        LOG_WARN("Compute bind call has {} dynamic offsets but zero sets (firstSet={}) - possible "
                                 "corruption, skipping",
                                 call.DynamicOffsets.size(), call.FirstSet);
                        continue;
                    }

                    // Additional safety: cap dynamic offset count to avoid pathological driver behavior
                    constexpr uint32_t kMaxSaneDynamicOffsets = 1024; // Generous upper bound
                    if (call.DynamicOffsets.size() > kMaxSaneDynamicOffsets)
                    {
                        LOG_ERROR("Compute bind call has {} dynamic offsets (exceeds sanity limit of {}) - possible "
                                  "corruption, skipping",
                                  call.DynamicOffsets.size(), kMaxSaneDynamicOffsets);
                        continue;
                    }

                    const VkDescriptorSet* pSets = call.Sets.data();
                    const uint32_t* pDynamicOffsets =
                        call.DynamicOffsets.empty() ? nullptr : call.DynamicOffsets.data();

                    fns.CmdBindDescriptorSets(dstCmd, VK_PIPELINE_BIND_POINT_COMPUTE, call.Layout, call.FirstSet,
                                              call.DescriptorSetCount, pSets, (uint32_t) call.DynamicOffsets.size(),
                                              pDynamicOffsets);
                }
            }
        }

        // Push constants share one chronological history. A mixed graphics/compute
        // entry must not be repeated after a later graphics-only update.
        if (params.ReplayPushConstants && fns.CmdPushConstants)
        {
            constexpr VkShaderStageFlags graphicsStages =
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT |
                VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT | VK_SHADER_STAGE_GEOMETRY_BIT |
                VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_TASK_BIT_EXT | VK_SHADER_STAGE_MESH_BIT_EXT;
            for (const auto& entry : snapshot.PushConstantHistory)
            {
                const bool graphics = (entry.Stages & graphicsStages) != 0;
                const bool compute = params.ReplayComputeToo && (entry.Stages & VK_SHADER_STAGE_COMPUTE_BIT) != 0;
                if (!graphics && !compute) continue;
                const auto layout = graphics && params.OverrideGraphicsLayout ? params.OverrideGraphicsLayout : entry.Layout;
                if (layout && entry.Size > 0)
                    fns.CmdPushConstants(dstCmd, layout, entry.Stages, entry.Offset, entry.Size, entry.Data.data());
            }
        }
        // 4. Dynamic State
        if (params.ReplayViewportScissor)
        {
            ReplayViewports(fns, dstCmd, snapshot.Dyn);
            ReplayScissors(fns, dstCmd, snapshot.Dyn);
        }

        // 4.5. Extended Dynamic State
        if (params.ReplayExtendedDynamicState)
        {
            ReplayExtendedDynamicState(fns, dstCmd, snapshot.Dyn);
        }

        // 5. Vertex/Index
        if (params.ReplayVertexIndex)
        {
            ReplayVertexBuffers(fns, dstCmd, snapshot.VI);
            if (snapshot.VI.IndexBufferValid && fns.CmdBindIndexBuffer)
            {
                fns.CmdBindIndexBuffer(dstCmd, snapshot.VI.IndexBuffer, snapshot.VI.IndexOffset, snapshot.VI.IndexType);
            }
        }

        return true;
    }

    std::shared_ptr<CommandBufferStateEntry> GetOrCreateEntry(VkCommandBuffer cmd, bool createState = true)
    {
        {
            std::shared_lock mapLock(_statesMapMutex);
            auto it = _states.find(cmd);
            if (it != _states.end())
                return it->second;
        }

        std::unique_lock mapLock(_statesMapMutex);
        auto it = _states.find(cmd);
        if (it != _states.end())
            return it->second;

        auto entry = std::make_shared<CommandBufferStateEntry>();
        if (createState)
            entry->State = std::make_shared<CommandBufferState>();

        _states[cmd] = entry;
        return entry;
    }

    mutable std::shared_mutex _poolMetadataMutex; // Separate lock for pool metadata
    mutable std::shared_mutex _statesMapMutex;    // Only for map structure modifications

    std::unordered_map<VkCommandBuffer, std::shared_ptr<CommandBufferStateEntry>> _states;

    VulkanCmdFns _cachedFns {};
    bool _hasCachedFns = false;

    // Per-pool epoch tracking for accurate invalidation
    std::unordered_map<VkCommandBuffer, PoolKey> _cmdBufferToPool;
    std::map<PoolKey, uint32_t> _poolToQueueFamily;
    std::map<PoolKey, std::shared_ptr<std::atomic<uint64_t>>> _poolEpochs;
    std::atomic<uint64_t> _globalEpochCounter { 1 };
};
} // namespace vk_state
