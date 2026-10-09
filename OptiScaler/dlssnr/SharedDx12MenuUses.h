#pragma once
#include "NrGpuSafety.h"
#include "NativeIdentity.h"
#include <algorithm>
#include <mutex>

namespace DlssNr
{
// Shared menu heaps/targets have their own aggregate recording lifetime.
// A single feature's retirement cannot prove another feature's draws retired.
class SharedDx12MenuUses
{
    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    GpuSafety::CompletionSet uses_;
    std::recursive_mutex mutex_;
    inline static thread_local bool entered_=false;
  public:
    class Action
    {
        friend class SharedDx12MenuUses;
        std::unique_lock<std::recursive_mutex> lock_;
        explicit Action(std::unique_lock<std::recursive_mutex>&& lock):lock_(std::move(lock)){}
      public:
        Action(const Action&)=delete;
        Action& operator=(const Action&)=delete;
        Action(Action&&)=default;
        ~Action(){if(lock_.owns_lock())entered_=false;}
    };
    // All actual shared-pointer/device/use mutations and Render are on this
    // action, including bridge APIs. Skip busy/reentrant UI work, never wait
    // while an API/configuration owner may be held by the competing caller.
    std::optional<Action> Acquire()
    {
        if(entered_)return {};
        std::unique_lock lock(mutex_,std::try_to_lock);
        if(!lock.owns_lock())return {};
        entered_=true;return Action(std::move(lock));
    }
    bool MatchesDevice(IUnknown* device) const
    { return device_&&NativeIdentity::CompareDevices(device_.Get(),device).equal; }
    bool BindDevice(ID3D12Device* device)
    {
        if(device_)return MatchesDevice(device);
        const auto identity=NativeIdentity::ResolveDeviceIdentity(device);
        if(!identity.object||FAILED(identity.result))return false;
        device_=device;return true; // retain the original execution interface
    }
    bool Track(ID3D12GraphicsCommandList* list)
    {
        if(!list||!device_)return false;
        Microsoft::WRL::ComPtr<ID3D12Device> actual;
        if(FAILED(list->GetDevice(IID_PPV_ARGS(&actual)))||!MatchesDevice(actual.Get()))return false;
        std::erase_if(uses_,[](const auto& use){return use&&GpuSafety::Reusable(use);});
        auto use=GpuSafety::Record(list);
        if(!use)return false; // refuse before recording any shared menu draw
        if(std::find(uses_.begin(),uses_.end(),use)==uses_.end())uses_.push_back(std::move(use));
        return true;
    }
    bool CanRetire() const
    { return std::all_of(uses_.begin(),uses_.end(),[](const auto& use){return use&&GpuSafety::Reusable(use);}); }
    template<class Destroy>bool Retire(Destroy&& destroy)
    {
        if(!CanRetire())return false;
        destroy();uses_.clear();device_.Reset();return true;
    }
};
}
