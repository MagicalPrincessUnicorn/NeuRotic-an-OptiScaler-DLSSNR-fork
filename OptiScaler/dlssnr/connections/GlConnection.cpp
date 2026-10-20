#include "GpuBudget.h"
#include "GlConnection.h"
#include "GlExternalObjects.h"
#include <array>
#include <cstring>
namespace DlssNr::Connections {
namespace {
bool FailGl(std::string& r,const char* why){r=why;return false;}
bool ValidProc(PROC p){return p&&p!=reinterpret_cast<PROC>(1)&&p!=reinterpret_cast<PROC>(2)&&p!=reinterpret_cast<PROC>(3)&&p!=reinterpret_cast<PROC>(-1);}
bool WaitGl(ID3D12Fence* f,UINT64 n){auto v=f->GetCompletedValue();if(v==UINT64_MAX)return false;if(v>=n)return true;
    HANDLE e=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!e)return false;bool ok=SUCCEEDED(f->SetEventOnCompletion(n,e))&&WaitForSingleObject(e,2000)==WAIT_OBJECT_0;
    CloseHandle(e);v=f->GetCompletedValue();return ok&&v!=UINT64_MAX&&v>=n;}
}
struct GlConnection::Impl {
    struct Ticket{GlObservedFrame source;PendingFrame inner;bool live=true;};
    std::shared_ptr<GpuBudget::Reservation> importBudget,probeBudget;
    ComPtr<ID3D12Device> device;D3D12Connection transport;ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12Fence> producer,output;UINT64 value=0;
    ComPtr<ID3D12CommandAllocator> probeAllocator;ComPtr<ID3D12GraphicsCommandList> probeList;ComPtr<ID3D12Resource> probeReadback,probeUpload;
    GlImportOwnership ownership;
    FeedGl gl{};HMODULE module=nullptr;std::shared_ptr<GlContextLease> context;
    std::array<ComPtr<ID3D12Resource>,5> images;
    std::array<GLuint,5> textures{},memory{};GLuint producerSem=0,outputSem=0,readFbo=0,drawFbo=0;
    UINT w=0,h=0;bool deriveMotion=true;std::shared_ptr<Ticket> active;nrpg::CpuMainlineClient::GpuOutput held;bool restart=false;
    explicit Impl(ID3D12Device* d):device(d),transport(d){}
    bool Current()const{return context&&context->IsCurrent();}
    void ClearImports(){
        if(!gl.ok)return;
        gl.DeleteTextures(5,textures.data());gl.DeleteMemoryObjectsEXT(5,memory.data());textures={};memory={};
        if(producerSem)gl.DeleteSemaphoresEXT(1,&producerSem);if(outputSem)gl.DeleteSemaphoresEXT(1,&outputSem);producerSem=outputSem=0;
        if(readFbo)gl.DeleteFramebuffers(1,&readFbo);if(drawFbo)gl.DeleteFramebuffers(1,&drawFbo);readFbo=drawFbo=0;
        images={};ownership.Clear();importBudget.reset();probeList.Reset();probeAllocator.Reset();probeReadback.Reset();probeUpload.Reset();probeBudget.reset();w=h=0;
    }
    ~Impl(){if(Current())ClearImports();if(module)FreeLibrary(module);}
    bool Init(const GlObservedFrame& f,std::string& r){
        if(!f.context||!f.context->IsCurrent())return FailGl(r,"OpenGL context/thread lease unavailable");
        if(context&&context.get()!=f.context.get())return FailGl(r,"OpenGL context incarnation changed; new owner required");
        if(!context){
            context=f.context;
            if(!FeedGlLoad(&gl)){gl={};context.reset();return FailGl(r,"OpenGL memory/semaphore functions unavailable");}
            GLint maxDrawBuffers=0;gl.GetIntegerv(0x8824,&maxDrawBuffers);
            if(maxDrawBuffers<1||maxDrawBuffers>64){gl={};context.reset();return FailGl(r,"OpenGL draw-buffer state cannot be preserved");}
            const char* required[]{"GL_EXT_memory_object","GL_EXT_memory_object_win32","GL_EXT_semaphore","GL_EXT_semaphore_win32"};bool have[4]{};
            FeedGlSurveyExtensions(&gl,required,4,have);for(bool yes:have)if(!yes){gl={};context.reset();return FailGl(r,"OpenGL required extension not advertised");}
            // The native module must stay loaded as long as its function table is used.
            if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(gl.wglGetCurrentContext),&module)){gl={};context.reset();return FailGl(r,"OpenGL module lifetime unavailable");}
            auto proc=gl.wglGetProcAddress("glGetUnsignedBytevEXT");
            if(device->GetNodeCount()!=1)return FailGl(r,"OpenGL multiple host nodes unsupported");
            if(ValidProc(proc)){
                using GetBytes=void(APIENTRY*)(GLenum,GLubyte*);LUID luid{};FeedGlDrainErrors(&gl);
                reinterpret_cast<GetBytes>(proc)(0x9599,reinterpret_cast<GLubyte*>(&luid));GLint nodes=0;gl.GetIntegerv(0x959A,&nodes);
                const auto expected=device->GetAdapterLuid();
                if(FeedGlDrainErrors(&gl)==GL_NO_ERROR&&(std::memcmp(&luid,&expected,sizeof(luid))||nodes!=1))return FailGl(r,"OpenGL observed adapter/node mismatch");
            }
            // Missing identity query admits only this candidate's private pixel
            // roundtrip probe below; it does not establish a source adapter fact.
            D3D12_COMMAND_QUEUE_DESC q{};if(FAILED(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue))))return FailGl(r,"OpenGL host queue unavailable");
        }
        if(!queue)return FailGl(r,"OpenGL capability probe incomplete");
        const auto width=f.description.color.width,height=f.description.color.height;
        if(w==width&&h==height&&deriveMotion==f.deriveMotion)return true;ClearImports();
        std::uint64_t bytes=0;for(UINT n=0;n<(f.deriveMotion?3u:5u);++n){auto amount=GpuBudget::ImageBytes(device.Get(),width,height,n==1?DXGI_FORMAT_R32_FLOAT:n==3?DXGI_FORMAT_R16G16_FLOAT:n==4?DXGI_FORMAT_R8_UNORM:DXGI_FORMAT_R8G8B8A8_UNORM,D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS);if(!amount)return FailGl(r,"OpenGL import size unavailable");bytes+=amount;}
        importBudget=GpuBudget::Reserve(bytes);if(!importBudget)return FailGl(r,"OpenGL aggregate GPU budget exceeded");
        gl.GenFramebuffers(1,&readFbo);gl.GenFramebuffers(1,&drawFbo);
        auto shareFence=[&](ComPtr<ID3D12Fence>& fence,GLuint& semaphore){HANDLE handle=nullptr;
            if(FAILED(device->CreateFence(0,D3D12_FENCE_FLAG_SHARED,IID_PPV_ARGS(&fence)))||FAILED(device->CreateSharedHandle(fence.Get(),nullptr,GENERIC_ALL,nullptr,&handle)))return false;
            semaphore=FeedGlImportFence(&gl,handle);CloseHandle(handle);return semaphore!=0;};
        if(!shareFence(producer,producerSem)||!shareFence(output,outputSem)){ClearImports();return FailGl(r,"OpenGL D3D12 fence import unavailable");}
        for(UINT n=0;n<(f.deriveMotion?3u:5u);++n){D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=width;desc.Height=height;desc.DepthOrArraySize=1;desc.MipLevels=1;desc.SampleDesc.Count=1;
            desc.Format=n==1?DXGI_FORMAT_R32_FLOAT:n==3?DXGI_FORMAT_R16G16_FLOAT:n==4?DXGI_FORMAT_R8_UNORM:DXGI_FORMAT_R8G8B8A8_UNORM;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
            D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;HANDLE handle=nullptr;
            if(FAILED(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_SHARED,&desc,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&images[n])))||
               FAILED(device->CreateSharedHandle(images[n].Get(),nullptr,GENERIC_ALL,nullptr,&handle))){ClearImports();return FailGl(r,"OpenGL host shared texture unavailable");}
            bool ok=FeedGlImportImage(&gl,handle,0,width,height,n==1?GL_R32F:n==3?GL_RG16F:n==4?GL_R8:GL_RGBA8,&textures[n],&memory[n]);CloseHandle(handle);
            if(!ok){ClearImports();return FailGl(r,"OpenGL exact texture format import unavailable");}
        }
        if(!readFbo||!drawFbo||FeedGlDrainErrors(&gl)!=GL_NO_ERROR){ClearImports();return FailGl(r,"OpenGL import object creation failed");}
        w=width;h=height;deriveMotion=f.deriveMotion;
        if(!Probe(r)){if(!restart)ClearImports();return false;}ownership.ProbeCompleted();return true;
    }
    bool Probe(std::string& r){
        // Exact-format GL writes -> D3D12 reads/writes -> GL reads, private imports only.
        // Probe commands and resources are retained if either API cannot prove completion.
        using Clear=void(APIENTRY*)(GLenum,GLint,const GLfloat*);
        using GetBoolean=void(APIENTRY*)(GLenum,GLuint,GLboolean*);using ColorMask=void(APIENTRY*)(GLuint,GLboolean,GLboolean,GLboolean,GLboolean);
        auto getBoolean=reinterpret_cast<GetBoolean>(FeedGlProc(&gl,"glGetBooleani_v"));auto colorMask=reinterpret_cast<ColorMask>(FeedGlProc(&gl,"glColorMaski"));
        auto clear=reinterpret_cast<Clear>(FeedGlProc(&gl,"glClearBufferfv"));if(!clear||!getBoolean||!colorMask)return FailGl(r,"OpenGL exact-format roundtrip probe unavailable");
        using BindBuffer=void(APIENTRY*)(GLenum,GLuint);using PixelStore=void(APIENTRY*)(GLenum,GLint);using ReadPixels=void(APIENTRY*)(GLint,GLint,GLsizei,GLsizei,GLenum,GLenum,void*);
        auto bindBuffer=reinterpret_cast<BindBuffer>(FeedGlProc(&gl,"glBindBuffer"));auto pixelStore=reinterpret_cast<PixelStore>(GetProcAddress(gl.lib,"glPixelStorei"));auto readPixels=reinterpret_cast<ReadPixels>(GetProcAddress(gl.lib,"glReadPixels"));
        if(!bindBuffer||!pixelStore||!readPixels)return FailGl(r,"OpenGL probe readback functions unavailable");
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=5*512;desc.Height=desc.DepthOrArraySize=1;desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        probeBudget=GpuBudget::Reserve(2*GpuBudget::TextureBytes(device.Get(),desc));if(!probeBudget)return FailGl(r,"OpenGL probe GPU budget exceeded");
        D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_READBACK;
        if(FAILED(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&probeReadback)))||
           FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&probeAllocator)))||
           FAILED(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,probeAllocator.Get(),nullptr,IID_PPV_ARGS(&probeList))))return FailGl(r,"OpenGL probe command allocation unavailable");
        hp.Type=D3D12_HEAP_TYPE_UPLOAD;
        if(FAILED(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&probeUpload))))return FailGl(r,"OpenGL probe upload unavailable");
        void* upload=nullptr;D3D12_RANGE noRead{};if(FAILED(probeUpload->Map(0,&noRead,&upload)))return FailGl(r,"OpenGL probe upload mapping unavailable");std::memset(upload,0,5*512);probeUpload->Unmap(0,nullptr);
        const UINT count=deriveMotion?3:5;bool valid=true;
        {FeedGlStateGuard state(&gl);const GLfloat pattern[]{1,0,0,1};GLboolean mask[4]{};getBoolean(GL_COLOR_WRITEMASK,0,mask);colorMask(0,TRUE,TRUE,TRUE,TRUE);
            for(UINT n=0;n<count;++n){if(!FeedGlAttach(&gl,GL_DRAW_FRAMEBUFFER,drawFbo,textures[n],true)){valid=false;break;}clear(GL_COLOR,0,pattern);}
            colorMask(0,mask[0],mask[1],mask[2],mask[3]);}
        FeedGlSignal(&gl,producerSem,++value,textures.data(),count);
        if(!valid||FeedGlDrainErrors(&gl)!=GL_NO_ERROR||!WaitGl(producer.Get(),value)){restart=true;return FailGl(r,"OpenGL format probe producer completion unknown");}
        for(UINT n=0;n<count;++n){D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition={images[n].Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_SOURCE};probeList->ResourceBarrier(1,&barrier);
            D3D12_TEXTURE_COPY_LOCATION dst{},src{};dst.pResource=probeReadback.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint.Offset=n*512;
            dst.PlacedFootprint.Footprint={images[n]->GetDesc().Format,1,1,1,256};src.pResource=images[n].Get();src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            D3D12_BOX box{0,0,0,1,1,1};probeList->CopyTextureRegion(&dst,0,0,0,&src,&box);
            barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_SOURCE;barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_DEST;probeList->ResourceBarrier(1,&barrier);
            auto uploadSource=dst;uploadSource.pResource=probeUpload.Get();probeList->CopyTextureRegion(&src,0,0,0,&uploadSource,nullptr);
            barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_DEST;barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_COMMON;probeList->ResourceBarrier(1,&barrier);}
        if(FAILED(probeList->Close())){restart=true;return FailGl(r,"OpenGL probe recording failed");}ID3D12CommandList* lists[]{probeList.Get()};queue->ExecuteCommandLists(1,lists);
        if(FAILED(queue->Signal(output.Get(),value))||!WaitGl(output.Get(),value)){restart=true;return FailGl(r,"OpenGL format probe consumer completion unknown");}
        void* mapped=nullptr;D3D12_RANGE range{0,5*512};if(FAILED(probeReadback->Map(0,&range,&mapped)))return FailGl(r,"OpenGL format probe readback unavailable");
        const auto* bytes=static_cast<const BYTE*>(mapped);
        for(UINT n=0;n<count;++n){const auto* p=bytes+n*512;
            if(n==1){float probeValue=0;std::memcpy(&probeValue,p,4);valid=valid&&probeValue==1.f;}
            else if(n==3){std::uint16_t x=0,y=0;std::memcpy(&x,p,2);std::memcpy(&y,p+2,2);valid=valid&&x==0x3c00&&y==0;}
            else if(n==4)valid=valid&&p[0]==255;
            else valid=valid&&p[0]==255&&p[1]==0&&p[2]==0&&p[3]==255;}
        D3D12_RANGE none{};probeReadback->Unmap(0,&none);
        // Restore GL ownership only after proving the actual host signal.
        FeedGlWait(&gl,outputSem,value,textures.data(),count);
        {FeedGlStateGuard state(&gl);
            const GLenum names[]{GL_PACK_ALIGNMENT,GL_PACK_ROW_LENGTH,GL_PACK_SKIP_PIXELS,GL_PACK_SKIP_ROWS,GL_PACK_SWAP_BYTES,GL_PACK_LSB_FIRST};GLint saved[6]{},buffer=0;
            gl.GetIntegerv(0x88ED,&buffer);bindBuffer(0x88EB,0); // GL_PIXEL_PACK_BUFFER_BINDING / GL_PIXEL_PACK_BUFFER
            for(UINT n=0;n<6;++n){gl.GetIntegerv(names[n],&saved[n]);pixelStore(names[n],n==0?1:0);}
            for(UINT n=0;n<count;++n){float pixel=-1;
                if(!FeedGlAttach(&gl,GL_READ_FRAMEBUFFER,readFbo,textures[n],true)){valid=false;break;}
                readPixels(0,0,1,1,GL_RED,GL_FLOAT,&pixel);valid=valid&&pixel==0.f;}
            for(UINT n=0;n<6;++n)pixelStore(names[n],saved[n]);bindBuffer(0x88EB,buffer);
        }
        if(FeedGlDrainErrors(&gl)!=GL_NO_ERROR||!FeedGlWaitIdle(&gl,2000)){restart=true;return FailGl(r,"OpenGL format probe local reacquire unknown");}
        probeList.Reset();probeAllocator.Reset();probeReadback.Reset();probeUpload.Reset();
        return valid?true:FailGl(r,"OpenGL exact-format probe pixels differ");
    }
    bool ReacquireAll(std::string& r){
        if(!Current()||!ownership.HostOwns()){restart=true;ownership.Unknown();return FailGl(r,"OpenGL import ownership unavailable");}
        // All private D3D12 references were submitted before this queue signal;
        // inner Capture/Finish already proved their independent queue completion.
        if(FAILED(queue->Signal(output.Get(),value))||!WaitGl(output.Get(),value)){restart=true;ownership.Unknown();return FailGl(r,"OpenGL import release completion unknown");}
        FeedGlWait(&gl,outputSem,value,textures.data(),deriveMotion?3:5);
        if(FeedGlDrainErrors(&gl)!=GL_NO_ERROR||!FeedGlWaitIdle(&gl,2000)){restart=true;ownership.Unknown();return FailGl(r,"OpenGL full import reacquire completion unknown");}
        return ownership.ReacquireAll();
    }
    bool Blit(std::uint64_t source,bool ours,std::uint64_t target,bool targetOurs,bool flip){
        if(!FeedGlAttach(&gl,GL_READ_FRAMEBUFFER,readFbo,source,ours)||!FeedGlAttach(&gl,GL_DRAW_FRAMEBUFFER,drawFbo,target,targetOurs))return false;
        gl.BlitFramebuffer(0,flip?GLint(h):0,w,flip?0:GLint(h),0,0,w,h,GL_COLOR_BUFFER_BIT,GL_NEAREST);return gl.GetError()==GL_NO_ERROR;
    }
    bool Shape(std::uint64_t handle,GLenum required){
        GLint width=0,height=0,format=0,samples=0;
        if(!handle){
            if(required!=GL_RGBA8)return false;
            auto getDC=reinterpret_cast<HDC(WINAPI*)()>(GetProcAddress(gl.lib,"wglGetCurrentDC"));
            RECT rect{};if(!getDC||!GetClientRect(WindowFromDC(getDC()),&rect))return false;
            width=rect.right-rect.left;height=rect.bottom-rect.top;
            gl.BindFramebuffer(GL_READ_FRAMEBUFFER,0);gl.GetIntegerv(0x80A9,&samples);format=GL_RGBA8;
        }else if(FeedGlHandleType(handle)==GL_TEXTURE_2D){
            using Query=void(APIENTRY*)(GLenum,GLint,GLenum,GLint*);
            auto query=reinterpret_cast<Query>(GetProcAddress(gl.lib,"glGetTexLevelParameteriv"));if(!query)return false;
            GLint old=0;gl.GetIntegerv(GL_TEXTURE_BINDING_2D,&old);gl.BindTexture(GL_TEXTURE_2D,FeedGlHandleName(handle));
            query(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&width);query(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&height);query(GL_TEXTURE_2D,0,GL_TEXTURE_INTERNAL_FORMAT,&format);
            gl.BindTexture(GL_TEXTURE_2D,old);
        }else if(FeedGlHandleType(handle)==GL_RENDERBUFFER){
            using Bind=void(APIENTRY*)(GLenum,GLuint);using Query=void(APIENTRY*)(GLenum,GLenum,GLint*);
            auto bind=reinterpret_cast<Bind>(FeedGlProc(&gl,"glBindRenderbuffer"));auto query=reinterpret_cast<Query>(FeedGlProc(&gl,"glGetRenderbufferParameteriv"));if(!bind||!query)return false;
            GLint old=0;gl.GetIntegerv(0x8CA7,&old);bind(GL_RENDERBUFFER,FeedGlHandleName(handle));
            query(GL_RENDERBUFFER,0x8D42,&width);query(GL_RENDERBUFFER,0x8D43,&height);query(GL_RENDERBUFFER,0x8D44,&format);query(GL_RENDERBUFFER,0x8CAB,&samples);bind(GL_RENDERBUFFER,old);
        }else return false;
        return gl.GetError()==GL_NO_ERROR&&width==GLint(w)&&height==GLint(h)&&samples==0&&GLenum(format)==required;
    }
};
GlConnection::GlConnection(ID3D12Device* d):impl_(std::make_unique<Impl>(d)){}
GlConnection::~GlConnection(){if(RequiresRestart()||(impl_->context&&!impl_->Current()))(void)impl_.release();}
bool GlConnection::RequiresRestart()const{return impl_->restart||impl_->transport.RequiresRestart();}
bool GlConnection::Capture(const GlObservedFrame& f,PendingFrame& out,std::string& r){
    auto& i=*impl_;if(RequiresRestart()||i.active||out.ownerLease)return FailGl(r,"OpenGL capture busy or quarantined");
    auto valid=[](std::uint64_t h){return !h||FeedGlHandleType(h)==GL_TEXTURE_2D||FeedGlHandleType(h)==GL_RENDERBUFFER;};
    const auto& d=f.description;
    if(!GpuBudget::AdmitRaster(d.color.width,d.color.height,f.deriveMotion))return FailGl(r,"OpenGL raster exceeds bounded GPU admission");
    if(!i.device||!f.lease||!f.depthIsR32Float||!f.depth||!valid(f.color)||!valid(f.depth)||!valid(f.outputTarget)||!d.device||!d.generation||!d.capture||!d.color.width||!d.color.height||
       d.color.width!=d.color.allocationWidth||d.color.height!=d.color.allocationHeight||d.color.x||d.color.y||d.depth!=d.color||
       (d.rasterOrigin!=Neurotic::Contracts::RasterOrigin::TopLeft&&d.rasterOrigin!=Neurotic::Contracts::RasterOrigin::BottomLeft)||
       d.colorDomain!=Neurotic::Contracts::ColorDomain::EncodedDisplay)return FailGl(r,"OpenGL requires exact leased SDR raster and R32F depth representation");
    if(!f.deriveMotion&&(!f.motion||!f.distrust||!valid(f.motion)||!valid(f.distrust)))return FailGl(r,"OpenGL supplied guides unavailable");
    if(!i.Init(f,r))return false;
    if(!i.ownership.GlOwns()){i.restart=true;return FailGl(r,"OpenGL import set not reacquired before capture");}
    // Complete framebuffer/format checks before commands reference the source.
    {FeedGlStateGuard state(&i.gl);
        if(!i.Shape(f.color,GL_RGBA8)||!i.Shape(f.depth,GL_R32F)||!i.Shape(f.separateOutput?f.outputTarget:f.color,GL_RGBA8)||
           (!f.deriveMotion&&(!i.Shape(f.motion,GL_RG16F)||!i.Shape(f.distrust,GL_R8))))return FailGl(r,"OpenGL measured raster/format mismatch or multisample source");
        for(auto handle:{f.color,f.depth,f.separateOutput?f.outputTarget:f.color})if(!FeedGlAttach(&i.gl,GL_READ_FRAMEBUFFER,i.readFbo,handle,false))return FailGl(r,"OpenGL incomplete source framebuffer");
        if(FeedGlColorEncoding(&i.gl,i.readFbo,f.color)==GL_SRGB)return FailGl(r,"OpenGL sRGB attachment needs explicit encoded copy policy");
    }
    auto t=std::make_shared<Impl::Ticket>();t->source=f;i.active=t;
    bool copied=false;{FeedGlStateGuard state(&i.gl);const bool flip=d.rasterOrigin==Neurotic::Contracts::RasterOrigin::BottomLeft;
        copied=i.Blit(f.color,false,i.textures[0],true,flip)&&i.Blit(f.depth,false,i.textures[1],true,flip);
        if(copied&&!f.deriveMotion)copied=i.Blit(f.motion,false,i.textures[3],true,flip)&&i.Blit(f.distrust,false,i.textures[4],true,flip);}
    // Release output too: the host writes it later in this same ticket.
    if(!i.ownership.ReleaseAll()){i.restart=true;return FailGl(r,"OpenGL import release state invalid");}
    FeedGlSignal(&i.gl,i.producerSem,++i.value,i.textures.data(),f.deriveMotion?3:5);
    if(!copied||FeedGlDrainErrors(&i.gl)!=GL_NO_ERROR||!WaitGl(i.producer.Get(),i.value)){i.restart=true;return FailGl(r,"OpenGL capture completion unknown; restart required");}
    D3D12ObservedFrame inner{};inner.description=d;inner.description.rasterOrigin=Neurotic::Contracts::RasterOrigin::TopLeft;
    inner.deriveMotion=f.deriveMotion;inner.invertMotionY=d.rasterOrigin==Neurotic::Contracts::RasterOrigin::BottomLeft;
    if(!f.deriveMotion){inner.motion=i.images[3];inner.distrust=i.images[4];}
    inner.color=i.images[0];inner.depth=i.images[1];inner.outputTarget=i.images[2];inner.sourceQueue=i.queue;inner.producer=i.producer;inner.producerValue=i.value;inner.statesKnown=true;inner.lease=f.lease;
    if(!i.transport.Capture(inner,t->inner,r)){
        if(!RequiresRestart()){const auto failure=r;if(!i.ReacquireAll(r))return false;i.active.reset();r=failure;}return false;}
    out=t->inner;out.ownerLease=t;r.clear();return true;
}
bool GlConnection::Finish(PendingFrame& p,nrpg::CpuMainlineClient::GpuOutput& output,CopybackReceipt& receipt,std::string& r){
    receipt={};auto& i=*impl_;auto t=i.active;
    if(RequiresRestart()||!i.Current()||!t||p.ownerLease.get()!=t.get()||!t->live)return FailGl(r,"invalid OpenGL context/copyback ticket");
    auto local=output;i.held=output;CopybackReceipt inner;
    if(!i.transport.Finish(t->inner,local,inner,r)){if(!RequiresRestart())i.held={};return false;}
    if(!i.ReacquireAll(r))return false;
    bool copied=false;
    {FeedGlStateGuard state(&i.gl);copied=i.Blit(i.textures[2],true,t->source.separateOutput?t->source.outputTarget:t->source.color,false,
        t->source.description.rasterOrigin==Neurotic::Contracts::RasterOrigin::BottomLeft);}
    if(!copied||FeedGlDrainErrors(&i.gl)!=GL_NO_ERROR||!FeedGlWaitIdle(&i.gl,2000)){i.restart=true;return FailGl(r,"OpenGL local copyback completion unknown; restart required");}
    receipt=inner;t->live=false;t->source={};i.active.reset();p={};output={};i.held={};r.clear();return true;
}
bool GlConnection::Cancel(PendingFrame& p,std::string& r){auto& i=*impl_;auto t=i.active;
    if(RequiresRestart()||!i.Current()||!t||p.ownerLease.get()!=t.get()||!t->live)return FailGl(r,"invalid OpenGL cancellation");
    if(!i.transport.Cancel(t->inner,r)||!i.ReacquireAll(r))return false;t->live=false;t->source={};i.active.reset();p={};return true;}
}
