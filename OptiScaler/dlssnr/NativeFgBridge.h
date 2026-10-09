#pragma once
#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <Windows.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_win32.h>
#include <cstdint>
namespace DlssNr::NativeFg {
// The SDK copies these creation nodes into its physical swapchain. Unknown
// chains and application-controlled exclusive mode need additional WSI hooks.
inline const char* SwapchainChainRefusal(const void* chain,bool physical=false){
    bool mode=false,monitor=false;unsigned count=0;
    for(auto* n=static_cast<const VkBaseInStructure*>(chain);n;n=n->pNext){
        if(++count>2)return "swapchain extension chain is too long or cyclic";
        if(n->sType==VK_STRUCTURE_TYPE_SURFACE_FULL_SCREEN_EXCLUSIVE_INFO_EXT){
            if(mode)return "duplicate fullscreen creation metadata";mode=true;
            const auto value=reinterpret_cast<const VkSurfaceFullScreenExclusiveInfoEXT*>(n)->fullScreenExclusive;
            if(value==VK_FULL_SCREEN_EXCLUSIVE_APPLICATION_CONTROLLED_EXT&&!physical)return "application-controlled exclusive fullscreen requires physical presentation with maintenance fences";
            if(value!=VK_FULL_SCREEN_EXCLUSIVE_DEFAULT_EXT&&value!=VK_FULL_SCREEN_EXCLUSIVE_ALLOWED_EXT&&value!=VK_FULL_SCREEN_EXCLUSIVE_DISALLOWED_EXT&&value!=VK_FULL_SCREEN_EXCLUSIVE_APPLICATION_CONTROLLED_EXT)return "unknown fullscreen creation mode";
        }else if(n->sType==VK_STRUCTURE_TYPE_SURFACE_FULL_SCREEN_EXCLUSIVE_WIN32_INFO_EXT){
            if(monitor)return "duplicate fullscreen monitor metadata";
            if(!reinterpret_cast<const VkSurfaceFullScreenExclusiveWin32InfoEXT*>(n)->hmonitor)return "fullscreen monitor is missing";monitor=true;
        }else return "unsupported swapchain creation extension";
    }return nullptr;
}
inline bool SupportedSwapchainChain(const void* chain){return !SwapchainChainRefusal(chain);}
using EnterFn=void(*)(bool);using LeaveFn=void(*)(bool);
using CopyFn=VkResult(*)(void*,VkCommandBuffer,VkImage,unsigned,unsigned,std::uint64_t);
inline constexpr std::uint32_t BridgeVersion=5;
struct BridgeCallbacks {std::uint32_t version=BridgeVersion;EnterFn enter=nullptr;LeaveFn leave=nullptr;PFN_vkGetDeviceProcAddr deviceDispatch=nullptr;};
struct BridgeCreate {std::uint32_t version=BridgeVersion;VkPhysicalDevice physical{};VkDevice device{};VkQueue queue{};unsigned family=0;VkSwapchainCreateInfoKHR info{};VkQueue asyncQueue{},presentQueue{},acquireQueue{};bool physicalMode=false;};
struct BridgeFrame {std::uint64_t frame=0;bool generated=false;CopyFn copy=nullptr;void* context=nullptr;CopyFn compose=nullptr;void* composeContext=nullptr;};
using BridgeInit=bool(*)(const BridgeCallbacks*);
using BridgeNew=VkResult(*)(const BridgeCreate*,VkSwapchainKHR*);
using BridgeImages=VkResult(*)(VkDevice,VkSwapchainKHR,unsigned*,VkImage*);
using BridgeAcquire=VkResult(*)(VkDevice,VkSwapchainKHR,std::uint64_t,VkSemaphore,VkFence,unsigned*);
using BridgeConfigure=bool(*)(VkSwapchainKHR,const BridgeFrame*);
using BridgePresent=VkResult(*)(VkQueue,const VkPresentInfoKHR*);
using BridgeDrain=bool(*)(VkSwapchainKHR);
using BridgeDestroy=bool(*)(VkDevice,VkSwapchainKHR);
using BridgeBind=bool(*)(VkDevice,VkSwapchainKHR,VkQueue,unsigned);
struct BridgeProgress {uint64_t generatedSubmitted=0,generatedRetired=0,realRetired=0,skipped=0;bool retired=false;
 uint64_t composed=0,pendingPresents=0;VkResult lastAcquire=VK_SUCCESS,lastSubmit=VK_SUCCESS,lastPresent=VK_SUCCESS,lastWait=VK_SUCCESS;unsigned waitKind=0;};
using BridgeTeardown=bool(*)(VkDevice);
using BridgeQuery=bool(*)(VkSwapchainKHR,BridgeProgress*);
struct BridgeApi {BridgeInit init{};BridgeNew create{};BridgeImages images{};BridgeAcquire acquire{};BridgeConfigure configure{};BridgePresent present{};BridgeDrain drain{};BridgeDestroy destroy{};BridgeBind bind{};BridgeQuery query{};BridgeTeardown teardown{};};
}
