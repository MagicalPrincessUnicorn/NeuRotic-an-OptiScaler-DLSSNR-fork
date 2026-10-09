#pragma once
#include <mutex>
#include <cstdint>
namespace Neurotic::Runtime
{
inline std::recursive_mutex& FgOwnerMutex()
{
    static auto* mutex = new std::recursive_mutex;
    return *mutex;
}
inline unsigned fgOwnerTransitions=0;
inline std::uint64_t fgOwnerEpoch=1;
inline thread_local unsigned fgLocalTransitions=0;
// Caller holds FgOwnerMutex for admission and the subsequent input operation.
inline bool FgOwnerChanging() noexcept {return fgOwnerTransitions!=0;}
inline std::uint64_t FgOwnerEpoch() noexcept {return fgOwnerEpoch;}
class FgOwnerTransition {
    bool admitted_=false;
  public:
    FgOwnerTransition() {
        std::lock_guard lock(FgOwnerMutex());
        if(fgOwnerTransitions && !fgLocalTransitions)return;
        if(!fgOwnerTransitions)++fgOwnerEpoch;
        ++fgOwnerTransitions;++fgLocalTransitions;admitted_=true;
    }
    ~FgOwnerTransition(){if(admitted_){std::lock_guard lock(FgOwnerMutex());--fgOwnerTransitions;--fgLocalTransitions;}}
    explicit operator bool() const noexcept{return admitted_;}
    FgOwnerTransition(const FgOwnerTransition&)=delete;
    FgOwnerTransition& operator=(const FgOwnerTransition&)=delete;
};
}
