#include "DlaaHost.h"
#include <nr/context/PreparedRasterBounds.h>
#include <nvsdk_ngx.h>
#include <nvsdk_ngx_params.h>
#include <d3dcompiler.h>
#include <filesystem>
#include <array>
#include <cmath>
#include <stdexcept>
namespace nrpg
{
namespace {
template<class T>using Ptr=Microsoft::WRL::ComPtr<T>;
void Need(bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);}
template<class T>T Export(HMODULE module,const char* name){return reinterpret_cast<T>(GetProcAddress(module,name));}
// Runtime export order is the existing NVNGX_Proxy ProjectID ABI (the SDK's
// static-link Init_with_ProjectID wrapper uses a different final argument order).
using InitProject=NVSDK_NGX_Result(*)(const char*,NVSDK_NGX_EngineType,const char*,const wchar_t*,ID3D11Device*,NVSDK_NGX_Version,const NVSDK_NGX_FeatureCommonInfo*);
constexpr char BlitSource[]=R"(
Texture2D<float4> source:register(t0);
struct V{float4 p:SV_Position;};
V vs(uint id:SV_VertexID){V o;float2 p=float2((id<<1)&2,id&2);o.p=float4(p*float2(2,-2)+float2(-1,1),0,1);return o;}
float4 ps(V i):SV_Target{return source.Load(int3(int2(i.p.xy),0));}
)";
}
struct DlaaHost::Impl
{
    HMODULE module=nullptr;bool initialized=false,uncertain=false,pending=false;
    Ptr<ID3D11Device> device;Ptr<ID3D11DeviceContext> context;
    Ptr<ID3D11Query> query;Ptr<ID3D11VertexShader> vs;Ptr<ID3D11PixelShader> ps;
    Ptr<ID3D11RasterizerState> raster;
    Ptr<ID3D11Texture2D> input,output,returned;
    std::array<Ptr<ID3D11Texture2D>,4> borrowed;
    NVSDK_NGX_Handle* feature=nullptr;NVSDK_NGX_Parameter *params=nullptr,*caps=nullptr;
    UINT width=0,height=0;bool reversed=false;
    decltype(&NVSDK_NGX_D3D11_AllocateParameters) allocate=nullptr;
    decltype(&NVSDK_NGX_D3D11_GetCapabilityParameters) capabilities=nullptr;
    decltype(&NVSDK_NGX_D3D11_DestroyParameters) destroy=nullptr;
    decltype(&NVSDK_NGX_D3D11_GetScratchBufferSize) scratch=nullptr;
    decltype(&NVSDK_NGX_D3D11_CreateFeature) create=nullptr;
    decltype(&NVSDK_NGX_D3D11_EvaluateFeature) evaluate=nullptr;
    decltype(&NVSDK_NGX_D3D11_ReleaseFeature) release=nullptr;
    decltype(&NVSDK_NGX_D3D11_Shutdown1) shutdown=nullptr;
    ~Impl(){if(module)FreeLibrary(module);}
    void Drain()
    {
        pending=true;context->End(query.Get());context->Flush();
        const auto end=GetTickCount64()+5000;BOOL done=FALSE;
        for(;;){auto hr=context->GetData(query.Get(),&done,sizeof(done),D3D11_ASYNC_GETDATA_DONOTFLUSH);
            if(hr==S_OK && done){pending=false;return;}
            Need(hr==S_FALSE || hr==S_OK,"DLAA completion query failed; helper restart required");
            Need(GetTickCount64()<end,"DLAA GPU completion unknown; helper restart required");Sleep(1);}
    }
    bool Owns(ID3D11Texture2D* image,UINT w,UINT h,DXGI_FORMAT format)
    {
        if(!image)return false;Ptr<ID3D11Device> parent;image->GetDevice(&parent);
        Ptr<IUnknown> a,b;parent.As(&a);device.As(&b);D3D11_TEXTURE2D_DESC d{};image->GetDesc(&d);
        return a && b && a.Get()==b.Get() && d.Width==w && d.Height==h && d.Format==format &&
            d.ArraySize==1 && d.MipLevels==1 && d.SampleDesc.Count==1 && (d.BindFlags&D3D11_BIND_SHADER_RESOURCE);
    }
    void Texture(Ptr<ID3D11Texture2D>& image,DXGI_FORMAT format,UINT flags)
    {
        D3D11_TEXTURE2D_DESC d{};d.Width=width;d.Height=height;d.MipLevels=d.ArraySize=1;
        d.SampleDesc.Count=1;d.Format=format;d.BindFlags=flags;
        Need(SUCCEEDED(device->CreateTexture2D(&d,nullptr,&image)),"DLAA owned texture creation refused");
    }
    void Blit(ID3D11Texture2D* from,ID3D11Texture2D* to)
    {
        // NGX may leave UAVs bound. This context belongs exclusively to the
        // helper; clear those bindings before sampling the completed output.
        context->ClearState();
        Ptr<ID3D11ShaderResourceView> srv;Ptr<ID3D11RenderTargetView> rtv;
        Need(SUCCEEDED(device->CreateShaderResourceView(from,nullptr,&srv)) &&
             SUCCEEDED(device->CreateRenderTargetView(to,nullptr,&rtv)),"DLAA color conversion view refused");
        auto* rt=rtv.Get();auto* sr=srv.Get();D3D11_VIEWPORT vp{0,0,float(width),float(height),0,1};
        context->OMSetRenderTargets(1,&rt,nullptr);context->OMSetBlendState(nullptr,nullptr,UINT_MAX);
        context->OMSetDepthStencilState(nullptr,0);context->RSSetState(raster.Get());context->RSSetViewports(1,&vp);
        context->IASetInputLayout(nullptr);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vs.Get(),nullptr,0);context->PSSetShader(ps.Get(),nullptr,0);
        context->GSSetShader(nullptr,nullptr,0);context->HSSetShader(nullptr,nullptr,0);context->DSSetShader(nullptr,nullptr,0);
        context->PSSetShaderResources(0,1,&sr);pending=true;context->Draw(3,0);
        sr=nullptr;context->PSSetShaderResources(0,1,&sr);context->OMSetRenderTargets(0,nullptr,nullptr);
    }
    void ReleaseFeature()
    {
        Need(!pending && !uncertain,"DLAA owner has unresolved work");
        if(feature){uncertain=true;Need(release(feature)==NVSDK_NGX_Result_Success,"DLAA feature release failed");feature=nullptr;uncertain=false;}
        borrowed={};input.Reset();output.Reset();returned.Reset();width=height=0;
    }
};
DlaaHost::DlaaHost():impl_(std::make_unique<Impl>()){}
DlaaHost::~DlaaHost(){Stop();if(impl_->uncertain || impl_->pending)(void)impl_.release();}
bool DlaaHost::RequiresRestart()const{return impl_->uncertain || impl_->pending;}
bool DlaaHost::Initialize(ID3D11Device* device,ID3D11DeviceContext* context,const std::wstring& path,std::string& reason)
{
    auto& s=*impl_;try{
        Need(!s.module && device && context && std::filesystem::path(path).is_absolute(),"DLAA needs explicit absolute NGX runtime and private device");
        Ptr<ID3D11Device> parent;context->GetDevice(&parent);Need(parent.Get()==device && context->GetType()==D3D11_DEVICE_CONTEXT_IMMEDIATE,"DLAA private immediate context mismatch");
        s.device=device;s.context=context;
        s.module=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        Need(s.module!=nullptr,"Explicit NGX runtime could not load");
        // A configured NeuRotic proxy is not an official public DLSS runtime.
        Need(!GetProcAddress(s.module,"NeuRotic_RecordGuidesV1"),"DLAA compatibility requires genuine NGX runtime, not the NeuRotic proxy");
        auto init=Export<InitProject>(s.module,"NVSDK_NGX_D3D11_Init_ProjectID");
#define NRPG_BIND(member,name) s.member=Export<decltype(s.member)>(s.module,"NVSDK_NGX_D3D11_" name)
        NRPG_BIND(allocate,"AllocateParameters");NRPG_BIND(capabilities,"GetCapabilityParameters");NRPG_BIND(destroy,"DestroyParameters");
        NRPG_BIND(scratch,"GetScratchBufferSize");NRPG_BIND(create,"CreateFeature");NRPG_BIND(evaluate,"EvaluateFeature");
        NRPG_BIND(release,"ReleaseFeature");NRPG_BIND(shutdown,"Shutdown1");
#undef NRPG_BIND
        Need(init && s.allocate && s.capabilities && s.destroy && s.scratch && s.create && s.evaluate && s.release && s.shutdown,"NGX D3D11 public API incomplete");
        Ptr<ID3DBlob> v,p,error;
        Need(SUCCEEDED(D3DCompile(BlitSource,sizeof(BlitSource)-1,nullptr,nullptr,nullptr,"vs","vs_5_0",0,0,&v,&error)) &&
             SUCCEEDED(D3DCompile(BlitSource,sizeof(BlitSource)-1,nullptr,nullptr,nullptr,"ps","ps_5_0",0,0,&p,&error)),"DLAA color conversion compile failed");
        Need(SUCCEEDED(device->CreateVertexShader(v->GetBufferPointer(),v->GetBufferSize(),nullptr,&s.vs)) &&
             SUCCEEDED(device->CreatePixelShader(p->GetBufferPointer(),p->GetBufferSize(),nullptr,&s.ps)),"DLAA conversion shaders refused");
        D3D11_RASTERIZER_DESC raster{};raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;raster.DepthClipEnable=TRUE;
        Need(SUCCEEDED(device->CreateRasterizerState(&raster,&s.raster)),"DLAA raster state refused");
        D3D11_QUERY_DESC query{D3D11_QUERY_EVENT,0};Need(SUCCEEDED(device->CreateQuery(&query,&s.query)),"DLAA completion query unavailable");
        const auto directory=std::filesystem::path(path).parent_path().wstring();const wchar_t* paths[]={directory.c_str()};
        NVSDK_NGX_FeatureCommonInfo common{};common.PathListInfo.Path=paths;common.PathListInfo.Length=1;
        // Same existing NeuRotic application identifier; no game identity spoof.
        s.uncertain=true;
        Need(init("24480451-f00d-face-1304-0308dabad187",NVSDK_NGX_ENGINE_TYPE_CUSTOM,"NeuRotic.Prepared",directory.c_str(),device,NVSDK_NGX_Version_API,&common)==NVSDK_NGX_Result_Success,"Genuine NGX D3D11 initialization refused");
        s.initialized=true;s.uncertain=false;
        Need(s.allocate(&s.params)==NVSDK_NGX_Result_Success && s.params,"DLAA parameter allocation refused");
        Need(s.capabilities(&s.caps)==NVSDK_NGX_Result_Success && s.caps && s.caps!=s.params,"DLAA capabilities unavailable");
        int available=0;Need(s.caps->Get(NVSDK_NGX_Parameter_SuperSampling_Available,&available)==NVSDK_NGX_Result_Success && available==1,"Genuine DLSS SR/DLAA is unavailable on this adapter");
        reason.clear();return true;
    }catch(const std::exception& e){reason=e.what();Stop();return false;}
}
Ptr<ID3D11Texture2D> DlaaHost::Process(ID3D11Texture2D* color,ID3D11Texture2D* depth,ID3D11Texture2D* motion,
    UINT width,UINT height,bool reset,bool reversed,float scaleX,float scaleY,std::string& reason,ID3D11Texture2D* distrust)
{
    auto& s=*impl_;try{
        Need(s.initialized && !RequiresRestart() && width>=32 && height>=32 && Neurotic::PreparedRaster::Admit(width,height) &&
             std::isfinite(scaleX) && std::isfinite(scaleY) && scaleX!=0 && scaleY!=0,"DLAA session/profile refused");
        Need(s.Owns(color,width,height,DXGI_FORMAT_B8G8R8A8_UNORM) && s.Owns(depth,width,height,DXGI_FORMAT_R32_FLOAT) &&
             s.Owns(motion,width,height,DXGI_FORMAT_R32G32_FLOAT) && (!distrust || s.Owns(distrust,width,height,DXGI_FORMAT_R32_FLOAT)),"DLAA input device/shape/format mismatch");
        if(!s.feature || s.width!=width || s.height!=height || s.reversed!=reversed){
            s.ReleaseFeature();s.width=width;s.height=height;s.reversed=reversed;reset=true;
            s.Texture(s.input,DXGI_FORMAT_R16G16B16A16_FLOAT,D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET);
            s.Texture(s.output,DXGI_FORMAT_R16G16B16A16_FLOAT,D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS);
            s.Texture(s.returned,DXGI_FORMAT_B8G8R8A8_UNORM,D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET);
            auto* p=s.params;p->Set(NVSDK_NGX_Parameter_Width,width);p->Set(NVSDK_NGX_Parameter_Height,height);
            p->Set(NVSDK_NGX_Parameter_OutWidth,width);p->Set(NVSDK_NGX_Parameter_OutHeight,height);
            p->Set(NVSDK_NGX_Parameter_PerfQualityValue,int(NVSDK_NGX_PerfQuality_Value_DLAA));
            p->Set(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags,int(NVSDK_NGX_DLSS_Feature_Flags_AutoExposure|NVSDK_NGX_DLSS_Feature_Flags_MVLowRes|(reversed?NVSDK_NGX_DLSS_Feature_Flags_DepthInverted:0)));
            p->Set(NVSDK_NGX_Parameter_DLSS_Enable_Output_Subrects,0);
            p->Set("NR.Prepared.ContractVersion",1u);p->Set("DFC.Feeder.ContractVersion",1u);
            size_t scratch=0;Need(s.scratch(NVSDK_NGX_Feature_SuperSampling,p,&scratch)==NVSDK_NGX_Result_Success && !scratch,"DLAA runtime requires unsupported external scratch");
            s.pending=true;
            Need(s.create(s.context.Get(),NVSDK_NGX_Feature_SuperSampling,p,&s.feature)==NVSDK_NGX_Result_Success && s.feature,"Genuine DLAA feature creation failed; retained");
            s.Drain();
        }
        s.borrowed={color,depth,motion,distrust};s.Blit(color,s.input.Get());
        auto* p=s.params;p->Set(NVSDK_NGX_Parameter_Color,s.input.Get());p->Set(NVSDK_NGX_Parameter_Output,s.output.Get());
        p->Set(NVSDK_NGX_Parameter_Depth,depth);p->Set(NVSDK_NGX_Parameter_MotionVectors,motion);
        p->Set(NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_Mask,distrust);
        p->Set(NVSDK_NGX_Parameter_Jitter_Offset_X,0.f);p->Set(NVSDK_NGX_Parameter_Jitter_Offset_Y,0.f);
        p->Set(NVSDK_NGX_Parameter_MV_Scale_X,scaleX);p->Set(NVSDK_NGX_Parameter_MV_Scale_Y,scaleY);
        p->Set(NVSDK_NGX_Parameter_Reset,reset?1:0);
        p->Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width,width);p->Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height,height);
        s.pending=true;Need(s.evaluate(s.context.Get(),s.feature,p,nullptr)==NVSDK_NGX_Result_Success,"Genuine DLAA evaluate failed; retained");
        s.Blit(s.output.Get(),s.returned.Get());s.Drain();reason.clear();return s.returned;
    }catch(const std::exception& e){reason=e.what();if(s.pending)s.uncertain=true;return {};}
}
void DlaaHost::Stop()
{
    auto& s=*impl_;if(s.uncertain || s.pending)return;
    try{
        s.ReleaseFeature();
        if(s.caps){s.uncertain=true;Need(s.caps!=s.params && s.destroy(s.caps)==NVSDK_NGX_Result_Success,"DLAA capabilities release failed");s.caps=nullptr;s.uncertain=false;}
        if(s.params){s.uncertain=true;Need(s.destroy(s.params)==NVSDK_NGX_Result_Success,"DLAA parameters release failed");s.params=nullptr;s.uncertain=false;}
        if(s.initialized){s.uncertain=true;Need(s.shutdown(s.device.Get())==NVSDK_NGX_Result_Success,"DLAA runtime shutdown failed");s.initialized=false;s.uncertain=false;}
    }catch(...){s.uncertain=true;}
}
}
