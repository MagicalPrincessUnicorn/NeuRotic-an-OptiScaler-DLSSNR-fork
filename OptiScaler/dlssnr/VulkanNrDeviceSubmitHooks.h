#pragma once
#include "NativeFgVulkan.h"
#include <windows.h>
#include <detours/detours.h>
#include "VulkanNrCompletion.h"
#include <array>
#include <mutex>
#include <utility>

namespace DlssNr
{
// Device dispatch addresses can be cached without passing through our loader
// wrappers. Each distinct target owns its exact Detours trampoline for the
// process hook lifetime. Core/KHR aliases share a slot; device recreation does
// not overwrite a trampoline that may still serve another live device.
template<class Info> class VkNrDeviceSubmitHooks
{
  public:
    using Function=VkResult(VKAPI_PTR*)(VkQueue,uint32_t,const Info*,VkFence);
    enum class Result { Installed, AlreadyCovered, Unavailable, Capacity, Failed };
    static Result Install(Function target)
    {
        if(!target)return Result::Unavailable;
        std::lock_guard lock(mutex_);
        for(size_t i=0;i<slots_.size();++i)
            if(slots_[i].target==target||slots_[i].original==target||entries_[i]==target)
                return Result::AlreadyCovered;
        for(size_t i=0;i<slots_.size();++i)if(!slots_[i].target){
            auto& slot=slots_[i];slot.original=target;
            // Keep driver/layer code mapped across device and instance teardown
            // until our process-wide detour is removed.
            if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                reinterpret_cast<LPCWSTR>(target),&slot.module)){slot={};return Result::Failed;}
            const auto abandon=[&]{FreeLibrary(slot.module);slot={};};
            LONG status=DetourTransactionBegin();
            if(status!=NO_ERROR){abandon();return Result::Failed;}
            status=DetourUpdateThread(GetCurrentThread());
            if(status==NO_ERROR)status=DetourAttach(reinterpret_cast<PVOID*>(&slot.original),reinterpret_cast<PVOID>(entries_[i]));
            if(status!=NO_ERROR){DetourTransactionAbort();abandon();return Result::Failed;}
            status=DetourTransactionCommit();
            if(status!=NO_ERROR){abandon();return Result::Failed;}
            slot.target=target;return Result::Installed;
        }
        return Result::Capacity;
    }
    static bool Uninstall()
    {
        // Same contract as the surrounding Vulkan hook teardown: callers must
        // have stopped submissions before detaching executable trampolines.
        std::lock_guard lock(mutex_);bool success=true;
        for(size_t i=0;i<slots_.size();++i)if(slots_[i].target){
            auto& slot=slots_[i];LONG status=DetourTransactionBegin();
            if(status!=NO_ERROR){success=false;continue;}
            status=DetourUpdateThread(GetCurrentThread());
            if(status==NO_ERROR)status=DetourDetach(reinterpret_cast<PVOID*>(&slot.original),reinterpret_cast<PVOID>(entries_[i]));
            if(status!=NO_ERROR){DetourTransactionAbort();success=false;continue;}
            if(DetourTransactionCommit()!=NO_ERROR){success=false;continue;}
            FreeLibrary(slot.module);slot={};
        }
        return success;
    }
  private:
    friend struct VkNrDeviceSubmitProbe;
    struct Slot { Function target=nullptr,original=nullptr; HMODULE module=nullptr; };
    template<size_t Index> static VkResult VKAPI_CALL Invoke(VkQueue queue,uint32_t count,const Info* info,VkFence fence)
    {
        const auto original=slots_[Index].original;
        VkNrObservationScope scope(VkNrObservation::Submit,reinterpret_cast<uintptr_t>(queue));
        if(!scope.Observe()||!info)return NativeFg::QueueCall(original,queue,count,info,fence);
        std::vector<VkCommandBuffer> buffers;std::vector<VkSemaphore> waits;
        for(uint32_t i=0;i<count;++i)Gather(info[i],buffers,waits);
        return ObserveVkNrQueueSubmit(queue,buffers,[&]{return NativeFg::QueueCall(original,queue,count,info,fence);},waits);
    }
    static void Gather(const VkSubmitInfo& info,std::vector<VkCommandBuffer>& buffers,std::vector<VkSemaphore>& waits)
    {
        if(info.pCommandBuffers)buffers.insert(buffers.end(),info.pCommandBuffers,info.pCommandBuffers+info.commandBufferCount);
        // Only an exact semaphore supplied to an observed image acquisition
        // grants acquire evidence; unrelated timeline waits never match it.
        if(info.pWaitSemaphores)waits.insert(waits.end(),info.pWaitSemaphores,info.pWaitSemaphores+info.waitSemaphoreCount);
    }
    static void Gather(const VkSubmitInfo2& info,std::vector<VkCommandBuffer>& buffers,std::vector<VkSemaphore>& waits)
    {
        if(info.pCommandBufferInfos)for(uint32_t i=0;i<info.commandBufferInfoCount;++i)buffers.push_back(info.pCommandBufferInfos[i].commandBuffer);
        // For binary semaphores Vulkan ignores value, including nonzero values.
        // The acquisition registry knows the semaphore's role from the actual
        // acquire call, so a timeline-style numeric heuristic would lose proof.
        if(info.pWaitSemaphoreInfos)for(uint32_t i=0;i<info.waitSemaphoreInfoCount;++i)
            waits.push_back(info.pWaitSemaphoreInfos[i].semaphore);
    }
    template<size_t... I> static constexpr auto Entries(std::index_sequence<I...>)
    {return std::array<Function,sizeof...(I)>{&Invoke<I>...};}
    static inline std::mutex mutex_;
    static inline std::array<Slot,8> slots_{};
    static inline const auto entries_=Entries(std::make_index_sequence<8>{});
};
using VkNrDeviceSubmit=VkNrDeviceSubmitHooks<VkSubmitInfo>;
using VkNrDeviceSubmit2=VkNrDeviceSubmitHooks<VkSubmitInfo2>;
}
