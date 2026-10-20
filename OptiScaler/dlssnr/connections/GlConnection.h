#pragma once
#include "D3D12Connection.h"
#include "GlContextIdentity.h"
namespace DlssNr::Connections {
// The import set moves as a whole: capture inputs and output target share one
// ownership episode. GPU completion is proved by the caller before transitions.
class GlImportOwnership {
public:
    enum class State { Empty,Gl,Host,Unknown };
    bool GlOwns()const{return state_==State::Gl;}
    bool HostOwns()const{return state_==State::Host;}
    void ProbeCompleted(){state_=State::Gl;}
    void Clear(){state_=State::Empty;}
    void Unknown(){state_=State::Unknown;}
    bool ReleaseAll(){if(!GlOwns())return false;state_=State::Host;return true;}
    bool ReacquireAll(){if(!HostOwns())return false;state_=State::Gl;return true;}
private:State state_=State::Empty;
};
struct GlObservedFrame {
    Descriptor description{};
    std::shared_ptr<GlContextLease> context;
    // ReShade encoding: object type in bits 40+, GLuint in low 32. Zero is default framebuffer.
    std::uint64_t color=0,depth=0,outputTarget=0,motion=0,distrust=0;
    bool deriveMotion=true;
    bool separateOutput=false;
    // Depth is an observed R32F color representation, not a cast depth attachment.
    bool depthIsR32Float=false;
    std::shared_ptr<void> lease;
};
class GlConnection {
public:
    explicit GlConnection(ID3D12Device*);
    ~GlConnection();
    bool Capture(const GlObservedFrame&,PendingFrame&,std::string&);
    bool Finish(PendingFrame&,nrpg::CpuMainlineClient::GpuOutput&,CopybackReceipt&,std::string&);
    bool Cancel(PendingFrame&,std::string&);
    bool RequiresRestart()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
}
