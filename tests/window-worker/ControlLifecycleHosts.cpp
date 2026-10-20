// Test-only control-state substitutes. This binary never loads or evaluates an NR provider.
#include "../../apps/NeuRoticWindowWorker/WorkerContracts.h"
#include "../../apps/NeuRoticWindowWorker/guidance/dav2/Dav2Host.h"
#include <cstdlib>
namespace nrw {
HWND ControlFixtureForeground(){return reinterpret_cast<HWND>(1);}
struct CaptureHost::Impl {ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;bool active=false,emitted=false;uint64_t session=0,sequence=0;};
CaptureHost::CaptureHost():impl_(std::make_unique<Impl>()){}
CaptureHost::~CaptureHost(){Stop();}
bool CaptureHost::Start(const CaptureOptions& o,std::string& reason) {
    Stop();impl_->session=o.session;
    auto hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&impl_->device,nullptr,&impl_->context);
    impl_->active=SUCCEEDED(hr);reason=impl_->active?"":"TEST ONLY WARP setup failed";return impl_->active;
}
bool CaptureHost::Next(CapturedFrame& f,std::string& reason) {
    reason.clear();auto mode=std::getenv("NRW_CONTROL_FIXTURE_CASE");
    const bool live=mode&&std::string(mode).starts_with("live");if(!impl_->active || (!live&&(impl_->emitted||!mode||std::string(mode)!="quarantine")))return false; if(live)Sleep(10);
    D3D11_TEXTURE2D_DESC d{};d.Width=(live&&impl_->sequence>1)?96:64;d.Height=64;d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;d.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
    if(FAILED(impl_->device->CreateTexture2D(&d,nullptr,&f.texture))){reason="TEST ONLY texture creation failed";return false;}
    LARGE_INTEGER q{};QueryPerformanceCounter(&q);f.stamp={impl_->session,++impl_->sequence,uint64_t(q.QuadPart),d.Width,d.Height,impl_->sequence==1};impl_->emitted=true;return true;
}
bool CaptureHost::Present(const CapturedFrame&,ID3D11Texture2D*,float,std::string& reason,int,SnapshotResult*,int,std::shared_ptr<void>,const color::Representation*){auto mode=std::getenv("NRW_CONTROL_FIXTURE_CASE");if(mode&&std::string(mode).starts_with("live")){reason.clear();return true;}reason="TEST ONLY presenter must not be reached";return false;}
CaptureStatistics CaptureHost::Statistics()const{return {};}
bool CaptureHost::Paused()const{return false;}
HANDLE CaptureHost::WakeHandle()const{return nullptr;}
bool CaptureHost::CursorCaptured()const{return false;}
FrameStamp CaptureHost::ObservedIdentity()const{return {impl_->session,impl_->sequence,0,64,64,false};}
bool CaptureHost::DisengageHotkeyAvailable()const{return false;}
void CaptureHost::Disengage(){}
bool CaptureHost::Pump(){return impl_->active;}
void CaptureHost::RetainSharedGeneration() noexcept {} // declared control double
void CaptureHost::Stop(){impl_->active=false;impl_->emitted=false;impl_->context.Reset();impl_->device.Reset();}
ID3D11Device* CaptureHost::Device()const{return impl_->device.Get();}
ID3D11DeviceContext* CaptureHost::Context()const{return impl_->context.Get();}
std::vector<WindowIdentity> CaptureHost::Enumerate(){WindowIdentity w;std::string reason;Inspect(reinterpret_cast<HWND>(1),w,reason);return {w};}
bool CaptureHost::Inspect(HWND,WindowIdentity& w,std::string& reason){w={};w.hwnd=w.pid=w.processCreation=1;w.width=w.height=64;w.windowClass=L"ControlFixture";w.executable=L"test-only";reason.clear();return true;}
bool CaptureHost::Validate(const WindowIdentity&,std::string& reason){reason.clear();return true;}
struct NrHost::Impl{bool active=false;NrProcessingOptions options;};
NrHost::NrHost():impl_(std::make_unique<Impl>()){}
NrHost::~NrHost(){}
bool NrHost::Initialize(ID3D11Device*,ID3D11DeviceContext*,const NrOptions& o,std::string& reason){reason.clear();impl_->options={o.transferStrength,o.colourStrength,o.nrScalePercent,o.modelStyle};impl_->active=true;return true;}
bool NrHost::Reconfigure(const NrProcessingOptions& o,std::string& reason){reason.clear();if(!impl_->active)return false;impl_->options=o;return true;}
NrResult NrHost::Process(const CapturedFrame& f,const GuideResult*) {NrResult r;r.stamp=f.stamp;r.recorded=r.submitted=true;auto mode=std::getenv("NRW_CONTROL_FIXTURE_CASE");if(mode&&std::string(mode).starts_with("live")){r.completed=true;r.texture=f.texture;r.workWidth=f.stamp.width*impl_->options.nrScalePercent/100;r.workHeight=f.stamp.height*impl_->options.nrScalePercent/100;return r;}r.reason="TEST ONLY NR must not be submitted after guide quarantine";return r;}
void NrHost::RetainSharedGeneration() noexcept {retained_=true;} // declared control double
void NrHost::Stop(){auto mode=std::getenv("NRW_CONTROL_FIXTURE_CASE");if(impl_->active && mode && std::string(mode)=="shutdown")retained_=true;impl_->active=false;}
bool NrHost::RequiresRestart()const{return retained_;}
struct GuidanceHost::Impl {bool quarantined=false,preview=false;std::optional<CapturedFrame> depth;uint64_t fresh=0;};
GuidanceHost::GuidanceHost():impl_(std::make_unique<Impl>()){}
GuidanceHost::~GuidanceHost(){}
bool GuidanceHost::Initialize(ID3D11Device*,ID3D11DeviceContext*,const GuidanceOptions& options,std::string& reason){impl_->preview=options.dav2.preview;reason.clear();return true;}
GuideResult GuidanceHost::Process(const CapturedFrame& f) {GuideResult g;g.stamp=f.stamp;auto mode=std::getenv("NRW_CONTROL_FIXTURE_CASE");if(mode&&std::string(mode).starts_with("live"))return g;g.reason="TEST ONLY injected unknown guidance retirement";impl_->quarantined=true;return g;}
void GuidanceHost::Stop(){}
void GuidanceHost::OfferDepth(const CapturedFrame& frame,uint64_t){impl_->depth=frame;}
void GuidanceHost::ObserveDepthEpochs(const FrameStamp&,uint64_t,uint64_t){}
bool GuidanceHost::PollDepth(depth::DepthFrame& result,std::string&){if(!impl_->preview||!impl_->depth)return false;result.color=*impl_->depth;result.preview=result.color.texture;result.color.pairedDepthOutput=true;result.reason="TEST ONLY completed depth substitute";impl_->depth.reset();++impl_->fresh;return true;}
bool GuidanceHost::DepthReady()const{return impl_->preview;}
bool GuidanceHost::DepthBusy()const{return false;}
uint64_t GuidanceHost::DepthFresh()const{return impl_->fresh;}
uint64_t GuidanceHost::DepthDropped()const{return 0;}
bool GuidanceHost::RequiresRestart()const{return impl_->quarantined;}
}
