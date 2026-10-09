#pragma once
#include "NativeIdentity.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

class StreamlineHooks;
namespace DlssNr
{
class NativeNgxCallCapture;
// Transport correlation for an actual enclosing Streamline evaluation only.
// Numeric frame/viewport values are not canonical content identities, continuity,
// C03 permission, or evidence that the provider produced or consumed an image.
class StreamlineSourceScope
{
    friend class ::StreamlineHooks;
    friend class NativeNgxCallCapture;
#ifdef NR_SPECTRE_SOURCE_TESTING
    friend class StreamlineSourceScopeTestAccess;
#endif
  public:
    static constexpr std::size_t MaximumDepth=8;
    enum class ReturnStatus:std::uint32_t {Pending,Succeeded,Failed,Abandoned};
    class Observation
    {
        friend class StreamlineSourceScope;
#ifdef NR_SPECTRE_SOURCE_TESTING
        friend class StreamlineSourceScopeTestAccess;
#endif
        const std::uint32_t frame_,viewport_;
        // One release publication carries both disposition and the exact result.
        // Pending/Abandoned have no result; zero is a valid observed result word.
        std::atomic<std::uint64_t> return_{static_cast<std::uint64_t>(ReturnStatus::Pending)};
        Observation(std::uint32_t frame,std::uint32_t viewport)noexcept:frame_(frame),viewport_(viewport){}
      public:
        std::uint32_t Frame()const noexcept{return frame_;}
        std::uint32_t Viewport()const noexcept{return viewport_;}
        ReturnStatus Status()const noexcept
        {return static_cast<ReturnStatus>(static_cast<std::uint32_t>(return_.load(std::memory_order_acquire)));}
        std::optional<std::uint32_t> Result()const noexcept
        {
            const auto value=return_.load(std::memory_order_acquire);
            const auto status=static_cast<ReturnStatus>(static_cast<std::uint32_t>(value));
            if(status!=ReturnStatus::Succeeded&&status!=ReturnStatus::Failed)return {};
            return static_cast<std::uint32_t>(value>>32);
        }
    };
  private:
    inline static thread_local StreamlineSourceScope* current_=nullptr;
    StreamlineSourceScope* const previous_;
    const std::size_t depth_;
    bool configured_=false;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> list_;
    std::shared_ptr<Observation> observation_;
    // Install before virtual frame conversion or any other foreign call. Every
    // unsupported/invalid/excess-depth evaluation is still an inheritance barrier.
    StreamlineSourceScope()noexcept:previous_(current_),
        depth_(previous_?(previous_->depth_<MaximumDepth+1?previous_->depth_+1:MaximumDepth+1):1)
    {current_=this;}
    StreamlineSourceScope(bool selected,std::uint32_t frame,std::optional<std::uint32_t> viewport,
        ID3D12GraphicsCommandList* list)noexcept:StreamlineSourceScope()
    {Configure(selected,frame,viewport,list);}
    void Configure(bool selected,std::uint32_t frame,std::optional<std::uint32_t> viewport,
        ID3D12GraphicsCommandList* list)noexcept
    {
        if(configured_)return;configured_=true;
        if(!selected||!viewport||!list||depth_>MaximumDepth)return;
        try
        {
            // No global mutex or token pointer. Scope retains only the resolved
            // native direct list; the surviving observation contains no COM object.
            auto native=NativeIdentity::Resolve<ID3D12GraphicsCommandList>(list).object;
            if(!native||native->GetType()!=D3D12_COMMAND_LIST_TYPE_DIRECT)return;
            auto observation=std::shared_ptr<Observation>(new Observation(frame,*viewport));
            list_=std::move(native);observation_=std::move(observation);
        }
        catch(...){/* Allocation/foreign failure leaves the installed barrier empty. */}
    }
    static std::shared_ptr<const Observation> CaptureFor(ID3D12GraphicsCommandList* canonicalList)noexcept
    {
        const auto* scope=current_;
        if(!canonicalList||!scope||!scope->observation_||scope->list_.Get()!=canonicalList||
           scope->observation_->Status()!=ReturnStatus::Pending)return {};
        return scope->observation_;
    }
    void Complete(std::uint32_t actualResult,bool success)noexcept
    {
        if(!observation_)return;
        std::uint64_t expected=static_cast<std::uint64_t>(ReturnStatus::Pending);
        const auto value=(static_cast<std::uint64_t>(actualResult)<<32)|
            static_cast<std::uint64_t>(success?ReturnStatus::Succeeded:ReturnStatus::Failed);
        observation_->return_.compare_exchange_strong(expected,value,std::memory_order_release,std::memory_order_relaxed);
    }
  public:
    StreamlineSourceScope(const StreamlineSourceScope&)=delete;
    StreamlineSourceScope& operator=(const StreamlineSourceScope&)=delete;
    StreamlineSourceScope(StreamlineSourceScope&&)=delete;
    StreamlineSourceScope& operator=(StreamlineSourceScope&&)=delete;
    ~StreamlineSourceScope()
    {
        if(observation_)
        {
            std::uint64_t expected=static_cast<std::uint64_t>(ReturnStatus::Pending);
            observation_->return_.compare_exchange_strong(expected,static_cast<std::uint64_t>(ReturnStatus::Abandoned),
                std::memory_order_release,std::memory_order_relaxed);
        }
        // Release may reenter; keep this closed barrier until COM is gone so a
        // nested teardown cannot accidentally inherit the outer call's evidence.
        list_.Reset();current_=previous_;
    }
};
}
