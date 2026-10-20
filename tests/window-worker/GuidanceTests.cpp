// GPL-3.0. Real worker-owned D3D11 textures; no vendor model is needed by default.
#include "../../apps/NeuRoticWindowWorker/WorkerContracts.h"
#include "../../apps/NeuRoticWindowWorker/guidance/DepthBundle.h"
#include "../../apps/NeuRoticWindowWorker/nr/NrPolicy.h"
#include <iostream>
#include <stdexcept>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <winioctl.h>
#include <cstring>
using namespace nrw;
static int checks=0;
static void Require(bool value,const char* message) { ++checks; if(!value)throw std::runtime_error(message); }
struct OwnedEmptyDirectory {
    std::filesystem::path path;
    explicit OwnedEmptyDirectory(std::filesystem::path p):path(std::move(p)){Require(CreateDirectoryW(path.c_str(),nullptr)!=FALSE,"create owned empty admission fixture");}
    ~OwnedEmptyDirectory(){RemoveDirectoryW(path.c_str());}
};
// Native mount-point fixture; no command interpreter or model/runtime copies.
struct MutableJunction {
    std::filesystem::path path;
    MutableJunction(std::filesystem::path p,const std::filesystem::path& target):path(std::move(p)){
        Require(CreateDirectoryW(path.c_str(),nullptr)!=FALSE,"create owned junction fixture");
        try{Set(target,false);}catch(...){RemoveDirectoryW(path.c_str());throw;}
    }
    ~MutableJunction(){RemoveDirectoryW(path.c_str());}
    void Set(const std::filesystem::path& target,bool replace){
        HANDLE handle=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        Require(handle!=INVALID_HANDLE_VALUE,"open native mutable junction");
        struct Close {HANDLE h;~Close(){CloseHandle(h);}} close{handle};DWORD returned=0;
        if(replace){struct {DWORD tag;WORD length,reserved;} remove{IO_REPARSE_TAG_MOUNT_POINT,0,0};Require(DeviceIoControl(handle,FSCTL_DELETE_REPARSE_POINT,&remove,sizeof(remove),nullptr,0,&returned,nullptr)!=FALSE,"retarget original unleased bundle junction");}
        auto print=std::filesystem::absolute(target).make_preferred().wstring();std::wstring substitute=L"\\??\\"+print;
        auto bytes=(substitute.size()+1+print.size()+1)*sizeof(wchar_t);std::vector<uint8_t> data(16+bytes);
        DWORD tag=IO_REPARSE_TAG_MOUNT_POINT;WORD length=WORD(8+bytes),offset=0,subLength=WORD(substitute.size()*2),printOffset=WORD((substitute.size()+1)*2),printLength=WORD(print.size()*2);
        std::memcpy(data.data(),&tag,4);std::memcpy(data.data()+4,&length,2);std::memcpy(data.data()+8,&offset,2);std::memcpy(data.data()+10,&subLength,2);std::memcpy(data.data()+12,&printOffset,2);std::memcpy(data.data()+14,&printLength,2);
        std::memcpy(data.data()+16,substitute.c_str(),(substitute.size()+1)*2);std::memcpy(data.data()+16+printOffset,print.c_str(),(print.size()+1)*2);
        Require(DeviceIoControl(handle,FSCTL_SET_REPARSE_POINT,data.data(),DWORD(data.size()),nullptr,0,&returned,nullptr)!=FALSE,"set native bundle junction");
    }
};
static CapturedFrame Image(ID3D11Device* device,uint64_t sequence,int dx=0,int dy=0,uint32_t w=128,uint32_t h=96,uint64_t session=1) {
    std::vector<uint8_t> pixels(size_t(w)*h*4);
    for(uint32_t y=0;y<h;++y)for(uint32_t x=0;x<w;++x) {
        int sx=int(x)-dx,sy=int(y)-dy;
        // Deterministic aperiodic texture, with literal translation applied independently of provider.
        unsigned a=unsigned(sx)*73856093u ^ unsigned(sy)*19349663u;
        a^=a>>13; a*=1274126177u;
        for(int c=0;c<3;++c)pixels[(size_t(y)*w+x)*4+c]=uint8_t(a>>16);
        pixels[(size_t(y)*w+x)*4+3]=255;
    }
    D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.ArraySize=d.MipLevels=1;
    d.Format=DXGI_FORMAT_B8G8R8A8_UNORM;d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_DEFAULT;
    D3D11_SUBRESOURCE_DATA data{pixels.data(),w*4,0}; CapturedFrame frame;
    Require(SUCCEEDED(device->CreateTexture2D(&d,&data,&frame.texture)),"fixture texture creation");
    frame.stamp={session,sequence,sequence*1000,w,h,false};return frame;
}
static void Translation(const GuideResult& r,float x,float y) {
    Require(r.motionCompleted,"real image motion completed");
    Require(r.motionProvider=="cpu-block-match-sdr-v1","honest CPU provider");
    Require(r.estimated,"guidance remains estimated");
    unsigned good=0,total=0;
    for(unsigned py=24;py+24<r.height;++py)for(unsigned px=24;px+24<r.width;++px) {
        auto i=(size_t(py)*r.width+px)*2;++total;
        if(std::abs(r.motionXY[i]-x)<.01f && std::abs(r.motionXY[i+1]-y)<.01f)++good;
    }
    Require(total && good>total*95/100,"current->previous displacement sign and scale");
}
int wmain(int argc,wchar_t** argv)try {
    GuidanceHost off;std::string reason;GuidanceOptions options;
    options.depthBundle=L"C:/definitely-missing/pinned-bundle";
    Require(off.Initialize(nullptr,nullptr,options,reason),"Off initializes without devices or runtime");
    CapturedFrame noTexture;noTexture.stamp={1,1,1,128,96,false};
    auto disabled=off.Process(noTexture);
    Require(!disabled.depthCompleted && !disabled.motionCompleted && disabled.depth.empty() && disabled.motionXY.empty(),"Off does no readback or inference");
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    Require(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)),"WARP fixture device");
    GuidanceHost host; options.mode=GuideMode::Shadow;options.depth=false;options.depthBundle.clear();
    Require(host.Initialize(device.Get(),context.Get(),options,reason),"motion initialize");
    auto first=host.Process(Image(device.Get(),1));Require(!first.motionCompleted,"first frame establishes reference");
    auto second=host.Process(Image(device.Get(),2,4,-3)); Translation(second,-4,3);
    Require(second.stamp.session==1 && second.stamp.sequence==2 && second.stamp.width==128 && second.width==128 && second.height==96,"exact current frame stamp and guide extent");
    Require(!host.Process(Image(device.Get(),2,4,-3)).motionCompleted,"duplicate sequence resets reference");
    Translation(host.Process(Image(device.Get(),3,2,-1)),2,-2);
    Require(!host.Process(Image(device.Get(),5)).motionCompleted,"skipped frame resets reference");
    Require(!host.Process(Image(device.Get(),6,0,0,128,96,2)).motionCompleted,"session discontinuity resets reference");
    auto reset=Image(device.Get(),7,0,0,128,96,2);reset.stamp.reset=true;
    Require(!host.Process(reset).motionCompleted,"explicit reset clears reference");
    Require(!host.Process(Image(device.Get(),8,0,0,160,96,2)).motionCompleted,"size discontinuity resets reference");
    auto invalid=Image(device.Get(),9,0,0,160,96,2);invalid.stamp.width=159;
    Require(!host.Process(invalid).motionCompleted,"texture extent must match stamped extent");
    Require(!host.Process(Image(device.Get(),10,0,0,160,96,2)).motionCompleted,"invalid capture clears history");
    host.Stop();Require(!host.Process(Image(device.Get(),11)).motionCompleted,"stopped host does not produce guides");
    Require(host.Initialize(device.Get(),context.Get(),options,reason),"restart initializes fresh history");
    Require(!host.Process(Image(device.Get(),1,0,0,640,360)).motionCompleted,"resized guide first frame");
    auto scaled=host.Process(Image(device.Get(),2,8,-6,640,360));
    Require(scaled.width==320 && scaled.height==180,"bounded guide extent");Translation(scaled,-4,3);
    host.Stop();options.depth=true;options.motion=false;options.depthBundle=L"C:/definitely-missing/pinned-bundle";
    Require(host.Initialize(device.Get(),context.Get(),options,reason),"unavailable optional depth reports status without blocking host");
    auto missing=host.Process(Image(device.Get(),1));
    Require(!missing.depthCompleted && missing.depth.empty() && missing.depthProvider.empty() && !missing.reason.empty(),"missing depth never falls back to CPU model or synthetic depth");
    host.Stop();
    const auto fixture=std::filesystem::path(argv[0]).parent_path()/L"bundle-fixture";
    std::filesystem::create_directory(fixture);
    {std::ofstream file(fixture/L"model.onnx",std::ios::binary);file<<"abc";}
    {
        HANDLE file=CreateFileW((fixture/L"model.onnx").c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
        Require(file!=INVALID_HANDLE_VALUE,"local hash fixture");
        auto digest=guidance::HashFile(file);CloseHandle(file);
        Require(digest=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","real SHA256 verification");
        guidance::DepthBundle bundle;Require(!bundle.Open(std::filesystem::absolute(fixture),reason),"substituted model rejects pinned bundle");
    }
    std::filesystem::remove(fixture/L"model.onnx");std::filesystem::remove(fixture);
    options.depthBundle=std::filesystem::absolute(fixture).wstring();
    Require(host.Initialize(device.Get(),context.Get(),options,reason),"removed bundle remains optional failure");
    Require(!host.Process(Image(device.Get(),2)).depthCompleted,"removed/substituted bundle never reports depth success");host.Stop();
    {
        auto parent=std::filesystem::path(argv[0]).parent_path();
        OwnedEmptyDirectory ancestor(parent/L"leased-parent");
        OwnedEmptyDirectory root(ancestor.path/L"leased-root");
        auto rootMoved=ancestor.path/L"root-renamed",ancestorMoved=parent/L"parent-renamed";
        {
            guidance::DirectoryLeases leases;Require(leases.Open(std::filesystem::absolute(root.path),reason),"real owned canonical directory leases");
            auto moved=MoveFileExW(root.path.c_str(),rootMoved.c_str(),0);auto moveError=GetLastError();
            std::cout<<"directory lease root rename result="<<moved<<" error="<<moveError<<"\n";
            Require(!moved && moveError==ERROR_SHARING_VIOLATION,"leased root cannot be renamed before module/session release");
            moved=MoveFileExW(ancestor.path.c_str(),ancestorMoved.c_str(),0);moveError=GetLastError();
            std::cout<<"directory lease ancestor rename result="<<moved<<" error="<<moveError<<"\n";
            Require(!moved && moveError==ERROR_SHARING_VIOLATION,"leased ancestor cannot be renamed before module/session release");
            HANDLE write=CreateFileW(root.path.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
            auto error=GetLastError();if(write!=INVALID_HANDLE_VALUE)CloseHandle(write);
            Require(write==INVALID_HANDLE_VALUE && error==ERROR_SHARING_VIOLATION,"directory retarget/write handle refused while leased");
        }
        Require(MoveFileExW(root.path.c_str(),rootMoved.c_str(),0)!=FALSE,"root rename becomes possible after leases retire");
        Require(MoveFileExW(rootMoved.c_str(),root.path.c_str(),0)!=FALSE,"restore owned directory fixture");
        Require(MoveFileExW(ancestor.path.c_str(),ancestorMoved.c_str(),0)!=FALSE,"ancestor rename becomes possible after all leases retire");
        Require(MoveFileExW(ancestorMoved.c_str(),ancestor.path.c_str(),0)!=FALSE,"restore owned ancestor fixture");
    }
    if(argc>1) {
        {
            auto parent=std::filesystem::path(argv[0]).parent_path();
            OwnedEmptyDirectory replacement(parent/L"admission-replacement");
            MutableJunction junction(parent/L"admission-junction",std::filesystem::absolute(argv[1]));
            guidance::DepthBundle pinned;bool admitted=pinned.Open(junction.path,reason);
            if(admitted){
                // All real files remain pinned, but the original root can still be redirected.
                junction.Set(replacement.path,true);
                Require(GetFileAttributesW((junction.path/L"model.onnx").c_str())==INVALID_FILE_ATTRIBUTES,"original admitted root retargets away from pinned model");
            }
            Require(!admitted,"mutable bundle-root junction must be refused before provider load");
        }
        {
            auto real=std::filesystem::absolute(argv[1]);
            MutableJunction ancestor(std::filesystem::path(argv[0]).parent_path()/L"admission-ancestor-junction",real.parent_path());
            guidance::DepthBundle pinned;Require(!pinned.Open(ancestor.path/real.filename(),reason),"reparse ancestor of otherwise valid pinned bundle refused before provider load");
        }
        options.motion=true;
        options.depthBundle=std::filesystem::absolute(argv[1]).wstring();
        Require(host.Initialize(device.Get(),context.Get(),options,reason),"explicit local depth initialization");
        auto depth=host.Process(Image(device.Get(),1,0,0,1280,720));
        Require(depth.depthCompleted && depth.width==518 && depth.height==294 && depth.depth.size()==518*294,"real pinned depth model execution");
        Require(depth.depthProvider=="depth-anything-v2-small-directml-fp32-v1" && depth.estimated,"depth provider provenance");
        float lo=INFINITY,hi=-INFINITY;bool valid=true;for(float value:depth.depth){valid=valid && std::isfinite(value) && value>=0;lo=std::min(lo,value);hi=std::max(hi,value);}
        Require(valid,"relative inverse-depth values");Require(hi>lo+.001f,"nonconstant model estimate");
        auto next=Image(device.Get(),2,4,-3,1280,720,1);
        auto paired=host.Process(next);NrOptions experimental;experimental.guides=GuideMode::Experimental;
        Require(paired.depthCompleted && paired.motionCompleted,"real pinned depth and CPU motion current pair");
        Require(detail::UseGuides(experimental,next.stamp,&paired),"actual bounded producer raster admitted by experimental NR consumer");
        auto stale=next.stamp;++stale.sequence;
        Require(!detail::UseGuides(experimental,stale,&paired),"real estimates refuse stale consumer frame");
        experimental.guides=GuideMode::Shadow;
        Require(!detail::UseGuides(experimental,next.stamp,&paired),"real shadow estimates do not alter NR inputs");host.Stop();
        Require(host.Initialize(device.Get(),context.Get(),options,reason),"pinned provider clean reinitialization");
        auto resetImage=Image(device.Get(),1,0,0,1280,720,10);resetImage.stamp.reset=true;
        auto afterReset=host.Process(resetImage);
        Require(afterReset.depthCompleted && afterReset.stamp.session==10 && afterReset.stamp.sequence==1 && afterReset.stamp.reset && !afterReset.motionCompleted,"exact model output and motion reset after checked Stop/restart");host.Stop();
    }
    options={};options.dav2.provider=depth::Provider::Dav2;options.dav2Root=L"C:/definitely-missing/dav2";
    Require(host.Initialize(device.Get(),context.Get(),options,reason),"missing optional DAV2 keeps image-only NR available");
    auto unavailable=host.Process(Image(device.Get(),1));Require(!unavailable.depthCompleted&&!unavailable.motionCompleted&&unavailable.depth.empty()&&unavailable.motionXY.empty(),"DAV2 never implicitly enables CPU motion/readback");
    Require(!host.DepthReady()&&!host.RequiresRestart(),"missing assets do not quarantine unsubmitted work");host.Stop();
    options.dav2.preview=true;Require(!host.Initialize(device.Get(),context.Get(),options,reason)&&!reason.empty(),"unavailable independent preview explains its missing assets");host.Stop();
    std::cout<<"PASS GuidanceTests "<<checks<<" checks\n";return 0;
} catch(const std::exception& e) {std::cerr<<"FAIL GuidanceTests: "<<e.what()<<"\n";return 1;}
