#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace Neurotic::Hooks
{
// A fresh borrow for ONE normal SDK-managed feature OnLoad. The caller supplies
// the actual declared modern SystemCaps object and restores it before returning
// to the host. This is not an owner, a cached publication or a promise about
// arbitrary concurrent raw callbacks/Shutdown. No module hold owns this data.
template<class Caps>
class StreamlineScopedCaps
{
    using AdapterArray=decltype(std::declval<Caps&>().adapters);
    static_assert(std::is_array_v<AdapterArray>);
    static constexpr std::size_t capacity=std::extent_v<AdapterArray>;
    static_assert(capacity>0);
    using Adapter=std::remove_extent_t<AdapterArray>;
    using Architecture=decltype(std::declval<Adapter&>().architecture);
    using Vendor=decltype(std::declval<Adapter&>().vendor);
    using Driver=decltype(std::declval<Caps&>().driverVersionMajor);
    using Scheduling=decltype(std::declval<Caps&>().hwsSupported);
    struct Saved {Architecture architecture{};Vendor vendor{};};

    Caps* borrowed=nullptr;
    std::size_t count=0;
    std::array<Saved,capacity> previous{};
    Driver driverBefore{};
    Scheduling schedulingBefore{};
    bool changedDriver=false;

  public:
    StreamlineScopedCaps(Caps* caps,std::uint32_t architecture,Vendor vendor,bool rewriteDriver) noexcept
    {
        if(!caps)return;
        const auto actualCount=static_cast<std::size_t>(caps->gpuCount);
        // This is the declared interface's actual array bound, not a machine,
        // DLL version or validation-coverage allowlist. An invalid count leaves
        // this operation unapplied; the caller still invokes the chosen original.
        if(actualCount>capacity)return;
        count=actualCount;
        for(std::size_t i=0;i<count;++i)
            previous[i]={caps->adapters[i].architecture,caps->adapters[i].vendor};
        driverBefore=caps->driverVersionMajor;
        schedulingBefore=caps->hwsSupported;
        changedDriver=rewriteDriver;
        borrowed=caps;
        for(std::size_t i=0;i<count;++i)
        {
            caps->adapters[i].architecture=architecture;
            caps->adapters[i].vendor=vendor;
        }
        if(changedDriver)caps->driverVersionMajor=999;
        caps->hwsSupported=true;
    }
    StreamlineScopedCaps(const StreamlineScopedCaps&)=delete;
    StreamlineScopedCaps& operator=(const StreamlineScopedCaps&)=delete;
    ~StreamlineScopedCaps() noexcept
    {
        if(!borrowed)return;
        // Restore ONLY fields this scope wrote, using each exact prior value.
        // Real helper side effects and unrelated original writes remain intact.
        // The original callback's updated gpuCount is not overwritten or used
        // to change the bounded set this scope actually modified.
        for(std::size_t i=0;i<count;++i)
        {
            borrowed->adapters[i].architecture=previous[i].architecture;
            borrowed->adapters[i].vendor=previous[i].vendor;
        }
        if(changedDriver)borrowed->driverVersionMajor=driverBefore;
        borrowed->hwsSupported=schedulingBefore;
    }
    bool Applied() const noexcept {return borrowed!=nullptr;}
};
}
