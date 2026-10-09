#pragma once
#include <type_traits>
#include <utility>

namespace Neurotic::Protocol
{
// The caller captures an exact owner snapshot before constructing this scope.
// This runs that owner's restoration once; it grants no snapshot, GPU-state,
// resource-write, or delivery authority of its own.
template<class RestoreAction,class FailureAction> class NativeCommandStateScope
{
    RestoreAction restore_;
    FailureAction failure_;
    bool attempted_=false,restored_=false;
  public:
    NativeCommandStateScope(RestoreAction restore,FailureAction failure)
        noexcept(std::is_nothrow_move_constructible_v<RestoreAction>&&std::is_nothrow_move_constructible_v<FailureAction>):
        restore_(std::move(restore)),failure_(std::move(failure))
    {static_assert(std::is_nothrow_invocable_v<FailureAction>);}
    NativeCommandStateScope(const NativeCommandStateScope&)=delete;
    NativeCommandStateScope& operator=(const NativeCommandStateScope&)=delete;
    ~NativeCommandStateScope()noexcept{Restore();}
    bool Restore()noexcept
    {
        if(attempted_)return restored_;
        attempted_=true;
        try{restored_=restore_();}catch(...){restored_=false;}
        if(!restored_)failure_();
        return restored_;
    }
};
}
