#include "GpuBudget.h"
#include "D3D10Connection.h"
#include "D3D10EventRelay.h"
#include <array>
namespace DlssNr::Connections {
namespace {bool Fail10(std::string& r,const char* w){r=w;return false;}
bool Same10(IUnknown* a,IUnknown* b){ComPtr<IUnknown>x,y;return a&&b&&SUCCEEDED(a->QueryInterface(IID_PPV_ARGS(&x)))&&SUCCEEDED(b->QueryInterface(IID_PPV_ARGS(&y)))&&x==y;}}
struct D3D10Connection::Impl {
    struct Ticket{D3D10ObservedFrame source;PendingFrame inner;bool live=true;};
    std::shared_ptr<GpuBudget::Reservation> bridgeBudget;ComPtr<ID3D12Device> renderer;
    FeedD3D10 relay{};std::array<FeedD3D10Bridge,5> bridges{};D3D11Connection modern;
    UINT w=0,h=0;DXGI_FORMAT cf=DXGI_FORMAT_UNKNOWN,df=DXGI_FORMAT_UNKNOWN,mf=DXGI_FORMAT_UNKNOWN,kf=DXGI_FORMAT_UNKNOWN;
    std::shared_ptr<Ticket> active;nrpg::CpuMainlineClient::GpuOutput held;bool restart=false;
    explicit Impl(ID3D12Device* d):renderer(d),modern(d){}
    ~Impl(){for(auto& b:bridges)FeedD3D10ReleaseBridge(&b);FeedD3D10Close(&relay);}
    bool Init(ID3D10Device* d,UINT width,UINT height,DXGI_FORMAT color,DXGI_FORMAT depth,DXGI_FORMAT motion,DXGI_FORMAT mask,std::string& r){
        if(relay.game&&!Same10(relay.game,d))return Fail10(r,"D3D10 logical device changed");
        if(!relay.ok){FeedD3D10Close(&relay);if(!FeedD3D10Open(&relay,d))return Fail10(r,"D3D10 event relay unavailable");}
        if(w==width&&h==height&&cf==color&&df==depth&&mf==motion&&kf==mask)return true;
        std::uint64_t bytes=0;for(UINT n=0;n<(motion==DXGI_FORMAT_UNKNOWN?3u:5u);++n){auto amount=GpuBudget::ImageBytes(renderer.Get(),width,height,n==1?depth:n==3?motion:n==4?mask:color,n==2?D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET:D3D12_RESOURCE_FLAG_NONE);if(!amount)return Fail10(r,"D3D10 bridge size unavailable");bytes+=amount;}
        auto budget=GpuBudget::Reserve(bytes);if(!budget)return Fail10(r,"D3D10 aggregate GPU budget exceeded");
        std::array<FeedD3D10Bridge,5> fresh{};
        for(UINT n=0;n<(motion==DXGI_FORMAT_UNKNOWN?3u:5u);++n)if(!FeedD3D10MakeBridge(&relay,&fresh[n],width,height,n==1?depth:n==3?motion:n==4?mask:color,n==2)){
            for(auto& b:fresh)FeedD3D10ReleaseBridge(&b);return Fail10(r,"D3D10 exact-format shared bridge unavailable");}
        for(auto& b:bridges)FeedD3D10ReleaseBridge(&b);bridges=fresh;bridgeBudget=std::move(budget);w=width;h=height;cf=color;df=depth;mf=motion;kf=mask;return true;
    }
};
D3D10Connection::D3D10Connection(ID3D12Device* d):impl_(std::make_unique<Impl>(d)){}
D3D10Connection::~D3D10Connection(){if(RequiresRestart())(void)impl_.release();}
bool D3D10Connection::RequiresRestart()const{return impl_->restart||impl_->modern.RequiresRestart();}
bool D3D10Connection::Capture(const D3D10ObservedFrame& f,PendingFrame& out,std::string& r){
    auto& i=*impl_;if(RequiresRestart()||i.active||out.ownerLease)return Fail10(r,"D3D10 capture busy or quarantined");
    if(!GpuBudget::AdmitRaster(f.description.color.width,f.description.color.height,f.deriveMotion))return Fail10(r,"D3D10 raster exceeds bounded GPU admission");
    if(!f.device||!f.color||!f.depth||!f.lease)return Fail10(r,"D3D10 requires owning device, textures and observation lease");
    auto target=f.outputTarget?f.outputTarget:f.color;ComPtr<ID3D10Device>c,d,t;f.color->GetDevice(&c);f.depth->GetDevice(&d);target->GetDevice(&t);
    if(!Same10(f.device.Get(),c.Get())||!Same10(f.device.Get(),d.Get())||!Same10(f.device.Get(),t.Get()))return Fail10(r,"D3D10 foreign device texture");
    D3D10_TEXTURE2D_DESC cd{},dd{},td{};f.color->GetDesc(&cd);f.depth->GetDesc(&dd);target->GetDesc(&td);
    auto shape=[&](const D3D10_TEXTURE2D_DESC& x){return x.Width==f.description.color.width&&x.Height==f.description.color.height&&x.MipLevels==1&&x.ArraySize==1&&x.SampleDesc.Count==1;};
    DXGI_FORMAT depth=dd.Format;
    if(depth==DXGI_FORMAT_D32_FLOAT||depth==DXGI_FORMAT_R32_TYPELESS)depth=DXGI_FORMAT_R32_FLOAT;
    if(depth==DXGI_FORMAT_D16_UNORM||depth==DXGI_FORMAT_R16_TYPELESS)depth=DXGI_FORMAT_R16_UNORM;
    if(depth==DXGI_FORMAT_D24_UNORM_S8_UINT)depth=DXGI_FORMAT_R24G8_TYPELESS;
    // Exact resource/view creation probes typeless legacy sharing before copies.
    if(!shape(cd)||!shape(dd)||!shape(td)||td.Format!=cd.Format||(cd.Format!=DXGI_FORMAT_R8G8B8A8_UNORM&&cd.Format!=DXGI_FORMAT_B8G8R8A8_UNORM)||
       (depth!=DXGI_FORMAT_R32_FLOAT&&depth!=DXGI_FORMAT_R16_UNORM&&depth!=DXGI_FORMAT_R24G8_TYPELESS))return Fail10(r,"D3D10 unsupported shared raster/depth format");
    DXGI_FORMAT mf=DXGI_FORMAT_UNKNOWN,kf=DXGI_FORMAT_UNKNOWN;
    if(!f.deriveMotion){
        if(!f.motion||!f.distrust)return Fail10(r,"D3D10 supplied guides missing");
        ComPtr<ID3D10Device> md,kd;f.motion->GetDevice(&md);f.distrust->GetDevice(&kd);
        D3D10_TEXTURE2D_DESC m{},k{};f.motion->GetDesc(&m);f.distrust->GetDesc(&k);
        if(!Same10(f.device.Get(),md.Get())||!Same10(f.device.Get(),kd.Get())||!shape(m)||!shape(k))return Fail10(r,"D3D10 supplied guides foreign device/raster");
        mf=m.Format;kf=k.Format;
        if((mf!=DXGI_FORMAT_R16G16_FLOAT&&mf!=DXGI_FORMAT_R32G32_FLOAT)||(kf!=DXGI_FORMAT_R8_UNORM&&kf!=DXGI_FORMAT_R32_FLOAT))return Fail10(r,"D3D10 supplied guide format unavailable");
    }
    if(!i.Init(f.device.Get(),cd.Width,cd.Height,cd.Format,depth,mf,kf,r))return false;
    auto ticket=std::make_shared<Impl::Ticket>();ticket->source=f;i.active=ticket;
    FeedD3D10Deposit(&i.relay,&i.bridges[0],f.color.Get());FeedD3D10Deposit(&i.relay,&i.bridges[1],f.depth.Get());
    if(!f.deriveMotion){FeedD3D10Deposit(&i.relay,&i.bridges[3],f.motion.Get());FeedD3D10Deposit(&i.relay,&i.bridges[4],f.distrust.Get());}
    if(!FeedD3D10SyncGame(&i.relay)){i.restart=true;return Fail10(r,"D3D10 deposit completion unknown; restart required");}
    D3D11ObservedFrame inner{};inner.description=f.description;inner.context=i.relay.relay_ctx;inner.color=i.bridges[0].tex11;inner.depth=i.bridges[1].tex11;
    inner.outputTarget=i.bridges[2].tex11;inner.lease=f.lease;inner.deriveMotion=f.deriveMotion;
    if(!f.deriveMotion){inner.motion=i.bridges[3].tex11;inner.distrust=i.bridges[4].tex11;}
    if(!i.modern.Capture(inner,ticket->inner,r)){if(!RequiresRestart())i.active.reset();return false;}
    out=ticket->inner;out.ownerLease=ticket;r.clear();return true;
}
bool D3D10Connection::Finish(PendingFrame& p,nrpg::CpuMainlineClient::GpuOutput& output,CopybackReceipt& receipt,std::string& r){
    receipt={};auto& i=*impl_;auto t=i.active;
    if(RequiresRestart()||!t||p.ownerLease.get()!=t.get()||!t->live)return Fail10(r,"invalid D3D10 copyback ticket");
    auto local=output;i.held=output;CopybackReceipt inner;
    if(!i.modern.Finish(t->inner,local,inner,r)){if(!RequiresRestart())i.held={};return false;}
    auto target=t->source.outputTarget?t->source.outputTarget:t->source.color;
    if(!FeedD3D10Collect(&i.relay,&i.bridges[2],target.Get())||!FeedD3D10SyncGame(&i.relay)){i.restart=true;return Fail10(r,"D3D10 final copyback completion unknown; restart required");}
    receipt=inner;t->live=false;t->source={};i.active.reset();p={};output={};i.held={};r.clear();return true;
}
bool D3D10Connection::Cancel(PendingFrame& p,std::string& r){auto& i=*impl_;auto t=i.active;
    if(RequiresRestart()||!t||p.ownerLease.get()!=t.get()||!t->live)return Fail10(r,"invalid D3D10 cancellation");
    if(!i.modern.Cancel(t->inner,r))return false;t->live=false;t->source={};i.active.reset();p={};return true;}
}
