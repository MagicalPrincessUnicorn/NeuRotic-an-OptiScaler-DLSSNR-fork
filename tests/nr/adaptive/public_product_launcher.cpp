// Full product entry launcher. This links no renderer/owner implementation and
// has no API that sets ReadyFor, published content, output tokens or retirement.
#include "public_provider_api.h"
#include "public_composition_oracle.h"
#include "public_fixture_binary_preflight.h"
#include <dlssnr/NativeHostReturnObservation.h>
#include <dlssnr/NativeTeardownObservation.h>
#include <dlssnr/NativeRendererShutdownObservation.h>
#include <windows.h>
#include <dxgi1_6.h>
#include <d3d12sdklayers.h>
#include <wrl/client.h>
#include <cstdio>
#include <cmath>
#include <bit>
#include <stdexcept>
#include <vector>
#include <array>
#include <algorithm>
using Microsoft::WRL::ComPtr;
using namespace DlssNr::AdaptivePublicFixture;
static void Need(bool condition,const char* why){if(!condition)throw std::runtime_error(why);}
static void Hr(HRESULT result,const char* why){Need(SUCCEEDED(result),why);}
template<class T>static T Export(HMODULE module,const char* name)
{auto value=GetProcAddress(module,name);Need(value!=nullptr,name);return reinterpret_cast<T>(value);}
static UINT64 Address(const void* value){return reinterpret_cast<UINT64>(value);}
static void Transition(ID3D12GraphicsCommandList* list,ID3D12Resource* resource,D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to)
{D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
 b.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,from,to};list->ResourceBarrier(1,&b);}
static float Half(unsigned short value)
{
    const int exponent=(value>>10)&31;const int mantissa=value&1023;
    if(exponent==31)return mantissa?NAN:(value&0x8000?-INFINITY:INFINITY);
    const float magnitude=exponent?std::ldexp(1.f+float(mantissa)/1024,exponent-15):std::ldexp(float(mantissa),-24);
    return value&0x8000?-magnitude:magnitude;
}
struct Host
{
    ComPtr<ID3D12Device> device;ComPtr<ID3D12CommandQueue> queue;ComPtr<ID3D12Fence> fence;
    ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;ComPtr<ID3D12InfoQueue> info;
    UINT64 serial=0;static constexpr unsigned width=127,height=95;
    Host(){ComPtr<IDXGIFactory4> factory;ComPtr<IDXGIAdapter> warp;
        Hr(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"factory");Hr(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)),"WARP");
        Hr(D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)),"device");Hr(device.As(&info),"debug queue");
        D3D12_COMMAND_QUEUE_DESC q{};q.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
        Hr(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)),"queue");Hr(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"fence");}
    void CreateList(){Hr(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)),"allocator");
        Hr(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)),"list");}
    void SubmitAndReset(){Hr(list->Close(),"close");ID3D12CommandList* lists[]={list.Get()};queue->ExecuteCommandLists(1,lists);
        const auto value=++serial;HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        if(!event||FAILED(queue->Signal(fence.Get(),value))||FAILED(fence->SetEventOnCompletion(value,event))||
           WaitForSingleObject(event,20000)!=WAIT_OBJECT_0||fence->GetCompletedValue()<value||FAILED(device->GetDeviceRemovedReason()))
        {std::fputs("FAIL unresolved submission; retain resources\n",stderr);std::fflush(stderr);TerminateProcess(GetCurrentProcess(),3);ExitProcess(3);}
        CloseHandle(event);Hr(allocator->Reset(),"allocator reset");Hr(list->Reset(allocator.Get(),nullptr),"list reset");}
    ComPtr<ID3D12Resource> Texture(DXGI_FORMAT format,D3D12_RESOURCE_STATES state){
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC d{};
        d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=width;d.Height=height;d.DepthOrArraySize=d.MipLevels=1;
        d.Format=format;d.SampleDesc.Count=1;d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;ComPtr<ID3D12Resource> r;
        Hr(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,state,nullptr,IID_PPV_ARGS(&r)),"texture");return r;}
    void CheckDebug(){for(UINT64 i=0;i<info->GetNumStoredMessages();++i){SIZE_T size=0;info->GetMessage(i,nullptr,&size);
        std::vector<unsigned char> bytes(size);auto* message=reinterpret_cast<D3D12_MESSAGE*>(bytes.data());
        Hr(info->GetMessage(i,message,&size),"debug message");
        if(message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){std::fprintf(stderr,"D3D12 %s\n",message->pDescription);Need(false,"D3D12 error");}}}
};
int wmain(int argc,wchar_t** argv)
{
    if(argc!=7){std::fputs("public-provider.dll test-product.dll provider-receipt product-receipt provider-manifest-sha product-manifest-sha\n",stderr);return 2;}
    // On uncertainty keep modules/objects until process termination. No DLL
    // unload or destructor is treated as proof that a product owner retired.
    Host* host=nullptr;
    try{
        const auto manifest=[](const wchar_t* value){std::string text;
            for(;*value;++value){Need(*value<=127,"manifest must be ASCII");text+=char(*value);}return text;};
        // Do this before any executable DLL load. These read-only mappings pin
        // exact 05-built artifacts, not a commercial provider reference build.
        BinaryPreflight productFile(argv[2]);productFile.RequireExports(ProductFixtureExports);
        productFile.RequireProvenance(argv[4],"product",manifest(argv[6]));
        BinaryPreflight providerFile(argv[1]);providerFile.RequireExports(ProviderFixtureExports);
        providerFile.RequireProvenance(argv[3],"provider",manifest(argv[5]));
        std::puts("PREFLIGHT both dedicated fixture artifacts verified as data; executableLoads=0");
        ComPtr<ID3D12Debug> debug;Hr(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)),"debug interface");debug->EnableDebugLayer();
        host=new Host;auto& gpu=*host;
        HMODULE provider=LoadLibraryExW(providerFile.Path(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
        Need(provider!=nullptr,"load explicit public provider");
        const auto identity=Export<decltype(&nr_adaptive_public_fixture_identity_v1)>(provider,"nr_adaptive_public_fixture_identity_v1");
        Need(ResolveLifetime(identity()).has_value(),"public provider identity");
        HMODULE product=LoadLibraryExW(productFile.Path(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
        Need(product!=nullptr,"load explicitly flagged product");
        const auto initialize=Export<int(*)()>(product,"NR_AdaptiveFixtureInitializeRuntime");
        const auto bind=Export<int(*)(HMODULE)>(product,"NR_AdaptiveFixtureBindPublicProvider");
        const auto configure=Export<int(*)()>(product,"NR_AdaptiveFixtureConfigureNativePostSr");
        Need(initialize()==1,"explicit service initialization");
        Need(bind(nullptr)==0,"null provider binding refused");
        const auto identityFault=Export<decltype(&nr_adaptive_public_fixture_identity_fault_v1)>(provider,"nr_adaptive_public_fixture_identity_fault_v1");
        Need(identityFault(1)==1&&bind(provider)==0,"wrong public identity binding refused");
        Need(identityFault(0)==1&&bind(provider)==1,"valid binding after refused attempt");
        Need(configure()==1,"normal native post-SR configuration");
        using Init=NVSDK_NGX_Result(*)(unsigned long long,const wchar_t*,ID3D12Device*,NVSDK_NGX_Version,const NVSDK_NGX_FeatureCommonInfo*);
        using Params=NVSDK_NGX_Result(*)(NVSDK_NGX_Parameter**);
        using Create=NVSDK_NGX_Result(*)(ID3D12GraphicsCommandList*,NVSDK_NGX_Feature,NVSDK_NGX_Parameter*,NVSDK_NGX_Handle**);
        using Scene=NVSDK_NGX_Result(*)(ID3D12GraphicsCommandList*,const NVSDK_NGX_Handle*,NVSDK_NGX_Parameter*,ID3D12Resource*,unsigned,unsigned);
        auto init=Export<Init>(product,"NVSDK_NGX_D3D12_Init_Ext");
        Need(init(1,L"",gpu.device.Get(),NVSDK_NGX_Version_API,nullptr)==NVSDK_NGX_Result_Success,"actual product init");
        // Startup negatives use real initialized flags reached by this call.
        Need(bind(provider)==0,"initialized D3D12 rebind refused");
        auto getParams=Export<Params>(product,"NVSDK_NGX_D3D12_GetCapabilityParameters");
        NVSDK_NGX_Parameter* parameters=nullptr;Need(getParams(&parameters)==NVSDK_NGX_Result_Success&&parameters,"actual product parameters");
        auto create=Export<Create>(product,"NVSDK_NGX_D3D12_CreateFeature");
        auto scene=Export<Scene>(product,"OptiScaler_W03_RecordSceneEvaluateV1");
        auto query=Export<DlssNr::QueryNativeReturnV1>(product,"OptiScaler_W03_QueryNativeReturnV1");
        auto recording=Export<DlssNr::QueryNativeRecordingV1>(product,"OptiScaler_W03_QueryRecordingV1");
        auto readStats=Export<decltype(&nr_adaptive_public_fixture_stats_v1)>(provider,"nr_adaptive_public_fixture_stats_v1");
        gpu.CreateList();
        // The real device/list hooks must see a genuine new recording before
        // any controlled producer is admitted. No observation fields are set.
        gpu.SubmitAndReset();
        for(const char* key:{"Width","OutWidth"})parameters->Set(key,Host::width);
        for(const char* key:{"Height","OutHeight"})parameters->Set(key,Host::height);
        parameters->Set("DLSS.Feature.Create.Flags",unsigned(NVSDK_NGX_DLSS_Feature_Flags_IsHDR|NVSDK_NGX_DLSS_Feature_Flags_MVLowRes));
        parameters->Set("PerfQualityValue",unsigned(NVSDK_NGX_PerfQuality_Value_DLAA));
        parameters->Set("CreationNodeMask",1u);parameters->Set("VisibilityNodeMask",1u);
        NVSDK_NGX_Handle* feature=nullptr;
        Need(create(gpu.list.Get(),NVSDK_NGX_Feature_SuperSampling,parameters,&feature)==NVSDK_NGX_Result_Success&&feature,"actual SR feature create");
        gpu.SubmitAndReset();
        std::array<ComPtr<ID3D12Resource>,4> images;
        const DXGI_FORMAT formats[]={DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R32_FLOAT,DXGI_FORMAT_R16G16_FLOAT,DXGI_FORMAT_R32_UINT};
        for(unsigned i=0;i<4;++i)images[i]=gpu.Texture(formats[i],D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        auto output=gpu.Texture(DXGI_FORMAT_R16G16B16A16_FLOAT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        const char* names[]={"Color","Depth","MotionVectors"};for(unsigned i=0;i<3;++i)parameters->Set(names[i],images[i].Get());
        parameters->Set("Output",output.Get());
        for(const char* key:{"ExposureTexture","DLSS.Input.Bias.Current.Color.Mask"})parameters->Set(key,static_cast<void*>(nullptr));
        for(const char* key:{"DLSS.Input.Color.Subrect.Base.X","DLSS.Input.Color.Subrect.Base.Y","DLSS.Input.Depth.Subrect.Base.X",
            "DLSS.Input.Depth.Subrect.Base.Y","DLSS.Input.MV.Subrect.Base.X","DLSS.Input.MV.Subrect.Base.Y",
            "DLSS.Output.Subrect.Base.X","DLSS.Output.Subrect.Base.Y"})parameters->Set(key,0u);
        parameters->Set("DLSS.Render.Subrect.Dimensions.Width",Host::width);parameters->Set("DLSS.Render.Subrect.Dimensions.Height",Host::height);
        for(const char* key:{"Jitter.Offset.X","Jitter.Offset.Y"})parameters->Set(key,0.f);
        for(const char* key:{"MV.Scale.X","MV.Scale.Y","DLSS.Pre.Exposure","DLSS.Exposure.Scale"})parameters->Set(key,1.f);
        parameters->Set("Reset",1u);
        const auto desc=output->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 bytes=0;
        gpu.device->GetCopyableFootprints(&desc,0,1,0,&footprint,nullptr,nullptr,&bytes);
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;D3D12_RESOURCE_DESC buffer{};
        buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=bytes;buffer.Height=buffer.DepthOrArraySize=buffer.MipLevels=1;
        buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;ComPtr<ID3D12Resource> readback;
        Hr(gpu.device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback)),"readback");
        UINT64 sequence=0;unsigned changed=0;
        for(unsigned phase=0;phase<2;++phase){
            DlssNr::NativeRecordingObservationV1 coverage;
            Need(recording(Address(gpu.list.Get()),&coverage,sizeof(coverage))==1&&coverage.flags==7,"real recording coverage");
            const auto result=scene(gpu.list.Get(),feature,parameters,images[3].Get(),0,0);
            DlssNr::NativeHostReturnObservationV1 returned;
            const auto queried=query(sequence,feature->Id,Address(gpu.list.Get()),Address(output.Get()),&returned,sizeof(returned));
            ProviderStats stats;Need(readStats(&stats)==1,"provider observations");
            std::printf("PHASE %u api=%u query=%u status=%u outcome=%u preparation=%u restoration=%u provenance=%u sequence=%llu modelCreate=%u modelEvaluate=%u srEvaluate=%u reason=%s\n",
                phase,unsigned(result),queried,returned.status,returned.stageOutcome,returned.runtimePreparation,returned.restorationFlags,
                returned.provenanceFlags,static_cast<unsigned long long>(returned.sequence),stats.modelCreates,stats.modelEvaluations,stats.srEvaluations,returned.reason);
            std::fflush(stdout);
            Need(result==NVSDK_NGX_Result_Success,"actual product evaluation refused");
            if(phase==0)Need(stats.modelCreates==1&&stats.modelEvaluations==0,"first call records real model creation only");
            else{
                Need(queried==1&&DlssNr::MatchesNativeReturnObservation(returned,sequence,feature->Id,Address(gpu.list.Get()),Address(output.Get())),"current host return identity");
                Need(returned.status==4&&returned.stageOutcome==1&&returned.runtimePreparation==4&&
                     returned.restorationFlags==15&&returned.provenanceFlags==3&&returned.outputContentRevision>0,"actual native output and restored outer return");
                Need(stats.modelCreates==1&&stats.modelEvaluations==1&&stats.srEvaluations==2,"real creation readiness and evaluation counts");
            }
            if(queried==1&&returned.sequence>sequence)sequence=returned.sequence;
            Transition(gpu.list.Get(),output.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
            D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=output.Get();from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            to.pResource=readback.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=footprint;
            gpu.list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
            Transition(gpu.list.Get(),output.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            gpu.SubmitAndReset();void* mapped=nullptr;D3D12_RANGE read{0,SIZE_T(bytes)},empty{};
            Hr(readback->Map(0,&read,&mapped),"map consumer pixels");unsigned wrongBaseline=0;changed=0;
            for(unsigned y=0;y<Host::height;++y)for(unsigned x=0;x<Host::width;++x){
                auto* pixel=reinterpret_cast<const unsigned short*>(static_cast<unsigned char*>(mapped)+footprint.Offset+y*footprint.Footprint.RowPitch+x*8);
                const bool front=x>=24&&x<56&&y>=24&&y<48;
                const float expected[]={front?.75f:(((x>>3)^(y>>3))&1?.25f:.125f),front?((((x-24)>>2)^((y-24)>>2))&1?.5f:.25f):.125f,front?.125f:.25f,1};
                for(unsigned c=0;c<4;++c){const float value=Half(pixel[c]);Need(std::isfinite(value)&&value>=0&&value<=4,"finite bounded output pixels");
                    if(std::abs(value-expected[c])>0.002f){++wrongBaseline;if(c<3)++changed;}}
            }
            unsigned numericSamples=0;
            if(phase){
                for(const auto& sample:CompositionSamples){
                    const auto* pixel=reinterpret_cast<const unsigned short*>(static_cast<unsigned char*>(mapped)+
                        footprint.Offset+sample.y*footprint.Footprint.RowPitch+sample.x*8);
                    std::array<float,4> actual{Half(pixel[0]),Half(pixel[1]),Half(pixel[2]),Half(pixel[3])};
                    std::printf("NUMERIC x=%u y=%u actual=%.7f,%.7f,%.7f expected=%.7f,%.7f,%.7f tolerance=.003\n",
                        sample.x,sample.y,actual[0],actual[1],actual[2],ExpectedEditedChannel(sample.original[0]),
                        ExpectedEditedChannel(sample.original[1]),ExpectedEditedChannel(sample.original[2]));
                    Need(MatchesCompositionSample(actual,sample),"independent downstream numerical sample");++numericSamples;
                }
                Need(numericSamples==4,"complete numeric sample set");
            }
            readback->Unmap(0,&empty);
            Need(phase?changed>Host::width*Host::height:wrongBaseline==0,phase?"NR changed downstream consumer pixels":"warmup preserves known public SR image");
            std::printf("PIXELS phase=%u changedChannels=%u\n",phase,changed);gpu.CheckDebug();
        }
        // Fence and actual Reset above precede the genuine product retirement
        // calls. The launcher does not supply a completion/terminal receipt.
        auto prepare=Export<DlssNr::PrepareNativeTeardownV1>(product,"OptiScaler_W03_PrepareTeardownV1");
        DlssNr::NativeTeardownObservationV1 retired;
        Need(prepare(&retired,sizeof(retired))==1&&DlssNr::SourceCapturesDrained(retired)&&retired.nrShutdownAttempted==1&&retired.nrShutdownSucceeded==1,"real native provider retirement");
        auto release=Export<NVSDK_NGX_Result(*)(NVSDK_NGX_Handle*)>(product,"NVSDK_NGX_D3D12_ReleaseFeature");
        Need(release(feature)==NVSDK_NGX_Result_Success,"actual SR release");
        auto complete=Export<DlssNr::PrepareNativeTeardownV1>(product,"OptiScaler_W03_CompleteTeardownV1");
        DlssNr::NativeTeardownObservationV1 closed;
        Need(complete(&closed,sizeof(closed))==1&&DlssNr::SourceCapturesDrained(closed)&&closed.scopeClosed&&closed.finalizerStopped,"actual aggregate closure");
        auto destroy=Export<NVSDK_NGX_Result(*)(NVSDK_NGX_Parameter*)>(product,"NVSDK_NGX_D3D12_DestroyParameters");
        Need(destroy(parameters)==NVSDK_NGX_Result_Success,"actual outer parameter destruction");
        auto shutdown=Export<unsigned(*)()>(product,"OptiScaler_W03_ShutdownCheckedV1");
        Need(shutdown()==unsigned(NVSDK_NGX_Result_Success),"actual selected core shutdown");
        ProviderStats finalStats;Need(readStats(&finalStats)&&finalStats.liveModels==0&&finalStats.liveSr==0&&
            finalStats.modelReleases==1&&finalStats.srReleases==1,"actual provider release counters");
        gpu.CheckDebug();
        std::printf("RESULT actual-product nativeReturns=1 changedChannels=%u ownerRetired=1 failures=0\n",changed);
        return 0;
    }catch(const std::exception& error){std::fprintf(stderr,"FAIL %s\n",error.what());return 2;}
}
