#include "PreparedHelperFrame.h"
#include "DlaaHost.h"
#include "SourceGuidedColor.h"
#include <dlssnr/PreparedContentProbe.h>
#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <io.h>
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <cstring>
#include <memory>
namespace H=Neurotic::Feed::Prepared::Helper;
namespace P=Neurotic::Feed::Prepared;
namespace {
void Need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
bool ReadAll(HANDLE pipe,void* bytes,std::size_t count){auto* output=static_cast<unsigned char*>(bytes);
    while(count){DWORD done=0;if(!ReadFile(pipe,output,static_cast<DWORD>((std::min)(count,std::size_t(1024*1024))),&done,nullptr)||!done)return false;
        output+=done;count-=done;}return true;}
bool WriteAll(HANDLE pipe,const void* bytes,std::size_t count){auto* input=static_cast<const unsigned char*>(bytes);
    while(count){DWORD done=0;if(!WriteFile(pipe,input,static_cast<DWORD>((std::min)(count,std::size_t(1024*1024))),&done,nullptr)||!done)return false;
        input+=done;count-=done;}return true;}
bool Payload(HANDLE input,const H::Request& request,H::Payload& payload){
    const auto& d=request.guides;const auto count=std::size_t(d.color.width)*d.color.height;
    payload.color.resize(count*4);payload.depth.resize(count);payload.motion.resize(std::size_t(d.motion.width)*d.motion.height*2);
    payload.distrust.resize((d.flags&P::HasDistrust)?count:0);
    return ReadAll(input,payload.color.data(),payload.color.size())&&ReadAll(input,payload.depth.data(),payload.depth.size()*sizeof(float))&&
        ReadAll(input,payload.motion.data(),payload.motion.size()*sizeof(float))&&ReadAll(input,payload.distrust.data(),payload.distrust.size()*sizeof(float));
}
LUID ParseLuid(const std::wstring& value){const auto separator=value.find(L':');Need(separator!=std::wstring::npos,"Adapter LUID must be high32:low32");
    std::size_t used=0;auto high=std::stoull(value.substr(0,separator),&used,0);Need(used==separator&&high<=UINT32_MAX,"Invalid adapter LUID high32");
    auto lowText=value.substr(separator+1);auto low=std::stoull(lowText,&used,0);Need(used==lowText.size()&&low<=UINT32_MAX,"Invalid adapter LUID low32");
    return {static_cast<DWORD>(low),static_cast<LONG>(static_cast<DWORD>(high))};}
struct ReadbackOwner {
    nrw::ComPtr<ID3D11Texture2D> staging,source;nrw::ComPtr<ID3D11Query> query;bool pending=false;
};
class OutputReader {
    std::unique_ptr<ReadbackOwner> owner_=std::make_unique<ReadbackOwner>();
public:
    ~OutputReader(){if(owner_->pending)(void)owner_.release();}
    std::vector<std::uint8_t> Read(ID3D11Device* device,ID3D11DeviceContext* context,ID3D11Texture2D* source){
        auto& o=*owner_;Need(!o.pending,"Output readback generation quarantined");D3D11_TEXTURE2D_DESC desc{};source->GetDesc(&desc);
        Need(desc.Format==DXGI_FORMAT_B8G8R8A8_UNORM&&desc.SampleDesc.Count==1,"NR output is not BGRA8");
        if(o.staging){D3D11_TEXTURE2D_DESC previous{};o.staging->GetDesc(&previous);
            if(previous.Width!=desc.Width||previous.Height!=desc.Height)o.staging.Reset();}
        if(!o.staging){desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=desc.MiscFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
            Need(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&o.staging)),"Output staging creation refused");}
        if(!o.query){D3D11_QUERY_DESC q{D3D11_QUERY_EVENT,0};Need(SUCCEEDED(device->CreateQuery(&q,&o.query)),"Readback completion query unavailable");}
        o.source=source;o.pending=true;context->CopyResource(o.staging.Get(),source);context->End(o.query.Get());context->Flush();
        const auto deadline=GetTickCount64()+5000;BOOL completed=FALSE;
        for(;;){const auto result=context->GetData(o.query.Get(),&completed,sizeof(completed),D3D11_ASYNC_GETDATA_DONOTFLUSH);
            if(result==S_OK&&completed)break;
            Need(result==S_FALSE||result==S_OK,"Output readback query failed; retained");
            Need(GetTickCount64()<deadline,"Output readback completion timeout; retained");Sleep(1);}
        o.pending=false;o.source.Reset();D3D11_MAPPED_SUBRESOURCE mapped{};
        Need(SUCCEEDED(context->Map(o.staging.Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&mapped)),"Completed output map refused");
        struct Unmap {ID3D11DeviceContext* context;ID3D11Texture2D* texture;~Unmap(){context->Unmap(texture,0);}} unmap{context,o.staging.Get()};
        Need(mapped.pData&&mapped.RowPitch>=std::uint64_t(desc.Width)*4,"Output mapped row pitch invalid");
        std::vector<std::uint8_t> output(std::size_t(desc.Width)*desc.Height*4);
        for(UINT y=0;y<desc.Height;++y)std::memcpy(output.data()+std::size_t(y)*desc.Width*4,
            static_cast<const unsigned char*>(mapped.pData)+std::size_t(y)*mapped.RowPitch,std::size_t(desc.Width)*4);
        return output;
    }
};
H::Reply Failure(const H::Request& request,H::Reason reason,P::Ipc::Outcome state=P::Ipc::Outcome::Failed){
    H::Reply reply;reply.result.identity=P::Ipc::IdentityOf(request.guides);reply.result.state=state;
    reply.result.reason=static_cast<std::uint32_t>(reason);return reply;}
nrw::ComPtr<ID3D11Texture2D> GuideTexture(ID3D11Device* device,const std::vector<float>& values,UINT width,UINT height,UINT components){
    nrw::ComPtr<ID3D11Texture2D> texture;D3D11_TEXTURE2D_DESC desc{};desc.Width=width;desc.Height=height;
    desc.MipLevels=desc.ArraySize=1;desc.Format=components==2?DXGI_FORMAT_R32G32_FLOAT:DXGI_FORMAT_R32_FLOAT;
    desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initial{values.data(),width*components*static_cast<UINT>(sizeof(float)),0};
    Need(values.size()==std::size_t(width)*height*components&&SUCCEEDED(device->CreateTexture2D(&desc,&initial,&texture)),"DLAA guide texture creation refused");return texture;
}
void ContentProbe(DlssNr::PreparedContent::Probe& probe,const H::Request& request,const H::Payload& payload,const std::vector<std::uint8_t>& prepared,
    const std::vector<std::uint8_t>& processed,ID3D11Device* device,ID3D11Texture2D* input,ID3D11Texture2D* output){
    namespace D=DlssNr::PreparedContent;D::Bindings bindings{};
    bindings[0]={request.guides.device,request.guides.generation,reinterpret_cast<std::uintptr_t>(payload.color.data())};
    const auto actualDevice=reinterpret_cast<std::uintptr_t>(device);
    bindings[1]={actualDevice,1,reinterpret_cast<std::uintptr_t>(input)};
    for(std::size_t i=2;i<5;++i)bindings[i]={actualDevice,1,reinterpret_cast<std::uintptr_t>(output)};
    const auto begin=probe.Begin(request.guides,bindings,D::Mode::Processed);
    if(!begin.accepted){std::cerr<<"helper-probe refused code="<<static_cast<unsigned>(begin.code)<<'\n';return;}
    const std::array<const std::vector<std::uint8_t>*,3> data{&payload.color,&prepared,&processed};
    for(std::size_t i=0;i<data.size();++i){D::View view{std::as_bytes(std::span(*data[i])),std::uint64_t(request.guides.color.width)*4,
        request.guides.color.width,request.guides.color.height,4,1,true};
        auto report=probe.Observe({P::Ipc::IdentityOf(request.guides),static_cast<D::Stage>(i),bindings[i],
            request.guides.maskCapture,view,D::Evidence::CompletedReadback});
        std::cerr<<"helper-probe source="<<request.guides.device<<" generation="<<request.guides.generation<<
            " capture="<<request.guides.capture<<" stage="<<i<<" hash="<<report.digest<<" sampled="<<report.sampleBytes<<
            " accepted="<<report.accepted<<" code="<<static_cast<unsigned>(report.code)<<" displayed=false\n";}
}
}
int wmain(int argc,wchar_t** argv){
    HANDLE protocol=nullptr;
    try{
        nrw::NrOptions options;options.guides=nrw::GuideMode::Experimental;std::wstring ngxPath;LUID luid{};bool selected=false,probeContent=false;
        float colorStrength=0;
        for(int i=1;i<argc;++i){std::wstring key=argv[i];
            if(key==L"--help"){std::cout<<"Prepared helper: --model <absolute DLL> --forwarder <absolute DLL> --adapter-luid <high32:low32> [--ngx <absolute public NGX runtime>]\n"
                "Optional --color-strength 0..1 --probe-content 0|1. Explicit opt-in CPU stdin/stdout transport; no model download.\n";return 0;}
            Need(i+1<argc,"Missing helper option value");std::wstring value=argv[++i];
            if(key==L"--model")options.modelPath=value;else if(key==L"--forwarder")options.forwarderPath=value;else if(key==L"--ngx")ngxPath=value;
            else if(key==L"--adapter-luid"){luid=ParseLuid(value);selected=true;}
            else if(key==L"--color-strength"){std::size_t used=0;colorStrength=std::stof(value,&used);
                Need(used==value.size()&&std::isfinite(colorStrength)&&colorStrength>=0&&colorStrength<=1,"Color strength must be finite 0..1");}
            else if(key==L"--probe-content"){Need(value==L"0"||value==L"1","Content probe must be 0 or 1");probeContent=value==L"1";}
            else throw std::runtime_error("Unknown helper option");}
        Need(selected&&!options.modelPath.empty()&&!options.forwarderPath.empty()&&
            std::filesystem::path(options.modelPath).is_absolute()&&std::filesystem::path(options.forwarderPath).is_absolute()&&
            (ngxPath.empty()||std::filesystem::path(ngxPath).is_absolute()),
            "Explicit absolute model/forwarder and adapter LUID are required");
        auto input=GetStdHandle(STD_INPUT_HANDLE),output=GetStdHandle(STD_OUTPUT_HANDLE);
        Need(input&&input!=INVALID_HANDLE_VALUE&&output&&output!=INVALID_HANDLE_VALUE,"Inherited protocol pipes unavailable");
        Need(DuplicateHandle(GetCurrentProcess(),output,GetCurrentProcess(),&protocol,0,FALSE,DUPLICATE_SAME_ACCESS)!=FALSE,"Protocol output handle unavailable");
        // Preserve the wire handle while routing incidental provider/CRT stdout to
        // diagnostics. A provider's log cannot masquerade as a message envelope.
        Need(SetStdHandle(STD_OUTPUT_HANDLE,GetStdHandle(STD_ERROR_HANDLE))!=FALSE,"Helper diagnostic routing failed");
        Need(_dup2(_fileno(stderr),_fileno(stdout))==0,"Helper CRT diagnostic routing failed");
        nrw::ComPtr<ID3D11Device> device;nrw::ComPtr<ID3D11DeviceContext> context;nrw::NrHost host;
        OutputReader reader;H::Sequence sequence;nrpg::DlaaHost dlaa;bool initialized=false;
        DlssNr::PreparedContent::Probe probe;unsigned probeRemaining=16;
        for(;;){H::Request request;if(!ReadAll(input,&request,sizeof(request)))break;
            if(!H::Validate(request)){
                auto failure=Failure(request,H::Reason::InvalidMessage);(void)WriteAll(protocol,&failure,sizeof(failure));
                std::cerr<<"Invalid helper message; stream closed\n";break;}
            if(request.flags&H::Cancel){auto cancelled=Failure(request,H::Reason::Cancelled,P::Ipc::Outcome::Cancelled);
                if(!WriteAll(protocol,&cancelled,sizeof(cancelled)))break;continue;}
            H::Payload payload;if(!Payload(input,request,payload))break;
            const bool reset=sequence.NeedsReset(request);
            if(!sequence.Admit(request)){auto failure=Failure(request,H::Reason::StaleFrame);
                if(!WriteAll(protocol,&failure,sizeof(failure)))break;continue;}
            nrw::CapturedFrame captured;captured.stamp={request.guides.session,request.guides.capture,request.timestampQpc,
                request.guides.color.width,request.guides.color.height,reset};
            nrw::GuideResult guides;std::string reason;
            if(!H::Normalize(request,payload,captured.stamp,guides,reason)){auto failure=Failure(request,H::Reason::InvalidPayload);
                std::cerr<<reason<<'\n';if(!WriteAll(protocol,&failure,sizeof(failure)))break;continue;}
            H::Reply reply=Failure(request,H::Reason::HostRefused);std::vector<std::uint8_t> pixels;
            try{
                if(!initialized){nrw::ComPtr<IDXGIFactory4> factory;nrw::ComPtr<IDXGIAdapter1> adapter;
                    Need(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))),"DXGI factory unavailable");
                    Need(SUCCEEDED(factory->EnumAdapterByLuid(luid,IID_PPV_ARGS(&adapter))),"Explicit adapter LUID unavailable");
                    constexpr D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0};
                    Need(SUCCEEDED(D3D11CreateDevice(adapter.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                        levels,2,D3D11_SDK_VERSION,&device,nullptr,&context)),"Same-adapter D3D11 device creation refused");
                    const bool hostReady=host.Initialize(device.Get(),context.Get(),options,reason);Need(hostReady,reason.c_str());
                    if(!ngxPath.empty()){const bool dlaaReady=dlaa.Initialize(device.Get(),context.Get(),ngxPath,reason);Need(dlaaReady,reason.c_str());}
                    initialized=true;}
                D3D11_TEXTURE2D_DESC desc{};desc.Width=captured.stamp.width;desc.Height=captured.stamp.height;
                desc.MipLevels=desc.ArraySize=1;desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;desc.SampleDesc.Count=1;
                desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
                D3D11_SUBRESOURCE_DATA source{payload.color.data(),desc.Width*4,0};
                Need(SUCCEEDED(device->CreateTexture2D(&desc,&source,&captured.texture)),"Owned helper input creation refused");
                if(!ngxPath.empty()){
                    auto depth=GuideTexture(device.Get(),guides.depth,desc.Width,desc.Height,1);
                    auto motion=GuideTexture(device.Get(),guides.motionXY,desc.Width,desc.Height,2);
                    nrw::ComPtr<ID3D11Texture2D> distrust;
                    if(request.guides.flags&P::HasDistrust)distrust=GuideTexture(device.Get(),payload.distrust,desc.Width,desc.Height,1);
                    auto evaluated=dlaa.Process(captured.texture.Get(),depth.Get(),motion.Get(),desc.Width,desc.Height,
                        captured.stamp.reset,guides.rawDeviceDepthReversed,1,1,reason,distrust.Get());
                    Need(evaluated!=nullptr,reason.c_str());captured.texture=evaluated;
                }
                std::vector<std::uint8_t> prepared;
                if(probeContent&&probeRemaining)prepared=reader.Read(device.Get(),context.Get(),captured.texture.Get());
                const auto result=host.Process(captured,&guides);
                Need(result.recorded&&result.submitted&&result.completed&&result.texture&&result.estimatedGuidesUsed&&
                    result.completionValue&&result.completionValue!=UINT64_MAX,result.reason.c_str());
                reply.result.facts=P::Ipc::Recorded|P::Ipc::Submitted|P::Ipc::GpuCompleted;
                reply.result.completionValue=result.completionValue;
                try{pixels=reader.Read(device.Get(),context.Get(),result.texture.Get());}
                catch(...){reply.result.reason=static_cast<std::uint32_t>(H::Reason::ReadbackFailed);throw;}
                if(colorStrength!=0)Need(nrpg::StabilizeColor(payload.color,pixels,desc.Width,desc.Height,colorStrength,payload.distrust),"Source-guided color admission refused");
                if(probeContent&&probeRemaining){--probeRemaining;ContentProbe(probe,request,payload,prepared,pixels,device.Get(),captured.texture.Get(),result.texture.Get());}
                reply.result.state=P::Ipc::Outcome::CompletedSuccess;reply.result.facts|=P::Ipc::OutputValid;
                reply.result.reason=0;reply.result.outputCapture=request.guides.capture;
                reply.width=desc.Width;reply.height=desc.Height;reply.header.payloadBytes=pixels.size();
                reply.diagnostics=ngxPath.empty()?0:H::DlaaEvaluated;
                if(request.guides.flags&P::HasDistrust)reply.diagnostics|=ngxPath.empty()?H::DistrustDiagnosticOnly:H::DistrustPublicDlaa;
                Need(H::CompletedReply(reply,request),"Internal completed helper receipt mismatch");
            }catch(const std::exception& error){std::cerr<<error.what()<<'\n';pixels.clear();
                reply.header.payloadBytes=0;reply.result.state=P::Ipc::Outcome::Failed;reply.result.facts&=~P::Ipc::OutputValid;}
            if(!WriteAll(protocol,&reply,sizeof(reply))||!WriteAll(protocol,pixels.data(),pixels.size()))break;
            if(!initialized||host.RequiresRestart()||dlaa.RequiresRestart())break;
        }
        dlaa.Stop();host.Stop();CloseHandle(protocol);return host.RequiresRestart()||dlaa.RequiresRestart()?2:0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';if(protocol)CloseHandle(protocol);return 1;}
}
