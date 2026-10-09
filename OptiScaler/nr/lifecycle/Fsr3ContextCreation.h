#pragma once
#include <cstdint>
#include <limits>
#include <mutex>
#include <optional>
#include <unordered_map>

namespace Neurotic::Lifecycle
{
// Loader-owned creation observations. These identify the module actually called,
// not an authenticated provider, SDK capability, live lease or release receipt.
// The caller must separately retain/authenticate the module and SDK context.
template<class Context,class Module,class Kind>class Fsr3ContextCreations
{
  public:
    struct Binding
    {
        Context context{};Module module{};Kind requestedKind{},routingKind{};
        std::uint64_t generation=0;bool ambiguous=false;
        friend bool operator==(const Binding&,const Binding&)=default;
    };
  private:
    mutable std::mutex mutex_;
    std::unordered_map<Context,Binding> bindings_;
    std::uint64_t next_=0;
  public:
    std::optional<Binding> ObserveCreate(bool succeeded,Context context,Module module,Kind requested,Kind routing)
    {
        if(!succeeded||!context||!module)return {};
        std::lock_guard lock(mutex_);
        if(auto found=bindings_.find(context);found!=bindings_.end())
        {
            // Reusing a live address is not a new proved incarnation. Preserve
            // legacy routing but make every observation unusable for enrollment.
            found->second.ambiguous=true;return {};
        }
        if(next_==(std::numeric_limits<std::uint64_t>::max)())return {};
        Binding value{context,module,requested,routing,++next_,false};
        bindings_.emplace(context,value);return value;
    }
    std::optional<Binding> Find(Context context)const
    {
        std::lock_guard lock(mutex_);const auto found=bindings_.find(context);
        return found==bindings_.end()?std::nullopt:std::optional{found->second};
    }
    bool Current(const Binding& binding)const
    {
        const auto current=Find(binding.context);
        return !binding.ambiguous&&current&&*current==binding;
    }
    bool ObserveDestroy(bool succeeded,const Binding& binding)
    {
        if(!succeeded)return false;
        std::lock_guard lock(mutex_);const auto found=bindings_.find(binding.context);
        if(found==bindings_.end()||found->second.generation!=binding.generation)return false;
        bindings_.erase(found);return true;
    }
};
}
