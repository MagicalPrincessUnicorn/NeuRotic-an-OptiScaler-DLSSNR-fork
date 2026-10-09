#pragma once
namespace DlssNr
{
// Private forwarder ABI v3. These are CPU call notifications, never NGX release
// callbacks or evidence that commands completed. No callback owns a route slot.
struct ProviderEntryCallbacks
{
    void* context=nullptr;
    bool (*enter)(void*) noexcept=nullptr;
    void (*returned)(void*,bool) noexcept=nullptr;
};
class ProviderEntryScope
{
    const ProviderEntryCallbacks* callbacks_=nullptr;
    bool attempted_=false,active_=false;
  public:
    explicit ProviderEntryScope(const ProviderEntryCallbacks* callbacks):callbacks_(callbacks){}
    ProviderEntryScope(const ProviderEntryScope&)=delete;
    bool Enter()noexcept
    {
        if(attempted_)return false;
        attempted_=true;
        if(!callbacks_||!callbacks_->enter||!callbacks_->returned||!callbacks_->enter(callbacks_->context))
        {callbacks_=nullptr;return false;}
        active_=true;return true;
    }
    void Returned(bool success)noexcept
    {if(active_){active_=false;callbacks_->returned(callbacks_->context,success);}}
    ~ProviderEntryScope(){Returned(false);}
};
}
