#pragma once
#include "../nr/diagnostics/capability/CapabilityNgxObservation.h"

namespace DlssNr
{
// Synchronous NGX binding override. This observes/restores a parameter binding
// only: it asserts no content revision, GPU state, SR write or delivery result.
// The owning callback retains the parameter block and original COM reference.
template<class Parameters,class Result> class NativeParameterOverride
{
    Parameters* parameters_;
    const char* key_;
    void* original_;
    Result success_;
    bool ready_=false,attempted_=false,restoreAttempted_=false,restored_=false;
    bool Matches(void* expected)const noexcept
    {
        try {void* value=nullptr; const bool matched=parameters_&&parameters_->Get(key_,&value)==success_&&value==expected;
            Capability::CaptureParameterReadback(key_,"pointer","evaluation",matched,attempted_); return matched;}
        catch(...){Capability::CaptureParameterReadback(key_,"pointer","evaluation",false,attempted_);return false;}
    }
  public:
    NativeParameterOverride(Parameters* parameters,const char* key,void* original,Result success)noexcept
        :parameters_(parameters),key_(key),original_(original),success_(success)
    {ready_=key_&&original_&&Matches(original_);}
    NativeParameterOverride(const NativeParameterOverride&)=delete;
    NativeParameterOverride& operator=(const NativeParameterOverride&)=delete;
    ~NativeParameterOverride()noexcept{Restore();}
    bool Ready()const noexcept{return ready_&&!attempted_&&!restoreAttempted_;}
    bool Bind(void* replacement)noexcept
    {
        if(!Ready()||!replacement||!Matches(original_))return false;
        // Set has no result and can throw after mutation. The attempted flag is
        // set first so every such exit still restores the captured binding.
        attempted_=true;
        try {parameters_->Set(key_,replacement);return Matches(replacement);}
        catch(...){return false;}
    }
    bool Restore()noexcept
    {
        if(restoreAttempted_)return restored_;
        restoreAttempted_=true;
        if(!attempted_)return restored_=ready_;
        try {parameters_->Set(key_,original_);restored_=Matches(original_);}
        catch(...){restored_=false;}
        return restored_;
    }
};
}
