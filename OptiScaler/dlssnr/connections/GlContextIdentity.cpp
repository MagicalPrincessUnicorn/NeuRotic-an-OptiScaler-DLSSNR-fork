#include "GlContextIdentity.h"
namespace DlssNr::Connections {
namespace {HGLRC Current(){auto lib=GetModuleHandleW(L"opengl32.dll");if(!lib)return nullptr;
    auto current=reinterpret_cast<HGLRC(WINAPI*)()>(GetProcAddress(lib,"wglGetCurrentContext"));return current?current():nullptr;}}
GlContextLease::GlContextLease(HGLRC c,GlContextIdentity id):context_(c),contextIncarnation_(id.contextIncarnation),shareGroupIncarnation_(id.shareGroupIncarnation),thread_(id.ownerThread){
    valid_=c&&id.contextIncarnation&&id.shareGroupIncarnation&&id.ownerThread;
}
bool GlContextLease::IsCurrent()const{return valid_&&thread_==GetCurrentThreadId()&&Current()==context_;}
void GlContextLease::Invalidate(){valid_=false;}
bool GlContextLease::TransferToCurrentThread(){if(!valid_||Current()!=context_)return false;thread_=GetCurrentThreadId();return true;}
GlContextIdentity GlContextLease::Identity()const{return {contextIncarnation_,shareGroupIncarnation_,thread_.load()};}
}
