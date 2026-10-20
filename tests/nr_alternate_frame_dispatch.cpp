#define NR_AFNR_TEST
#define NR_GPU_SAFETY_TEST
#include "../OptiScaler/dlssnr/NrGpuSafety.cpp"
#include "../OptiScaler/dlssnr/NrAlternateFrameDx12.cpp"
#include <dlssnr/NrAlternateFrameSession.h>
#include <nr/protocol/NativeEvaluationResult.h>
#include "afnr_warp_fixture.h"
using namespace DlssNr::AlternateFrame;
using namespace DirectX::PackedVector;
static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::cerr<<__LINE__<<": " #x "\n";std::exit(1);}}while(false)
int main(){
    AfnrWarp w;
    auto base=w.Texture(DXGI_FORMAT_R16G16B16A16_FLOAT,8,8),enhanced=w.Texture(DXGI_FORMAT_R16G16B16A16_FLOAT,8,8);
    auto depth=w.Texture(DXGI_FORMAT_R32_FLOAT,8,8),motion=w.Texture(DXGI_FORMAT_R32G32_FLOAT,8,8);
    std::array<XMHALF4,64>b,e;std::array<float,64>z;std::array<std::array<float,2>,64>mv{};
    for(unsigned i=0;i<64;++i){b[i]=XMHALF4(10,20,30,.25f);e[i]=XMHALF4(12,19,31,.25f);z[i]=.5f;}
    w.Upload(base.Get(),b.data(),8,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    w.Upload(enhanced.Get(),e.data(),8,D3D12_RESOURCE_STATE_COPY_SOURCE);
    w.Upload(depth.Get(),z.data(),4,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    w.Upload(motion.Get(),mv.data(),8,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    // Native supplies no queue during Evaluate. A real host Reset ends the old
    // recording incarnation even while its submitted GPU work remains pending.
    // Missing the execution-time dependency loses all five expected carries.
    for(unsigned hostMode=0;hostMode<3;++hostMode)for(unsigned flight=1;flight<=4;++flight)for(bool enabled:{false,true}) {
        const bool nativeQueueUnknown=hostMode!=0;
        Session session;unsigned providerCalls=0,commonCalls=0,omissions=0;
        std::array<ComPtr<ID3D12Resource>,12>targets;
        for(auto& target:targets){target=w.Texture(DXGI_FORMAT_R16G16B16A16_FLOAT,8,8);w.Upload(target.Get(),b.data(),8,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);}
        ComPtr<ID3D12Fence> gate;HR(w.device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate)));
        unsigned gateValue=0;
        for(unsigned n=1;n<=12;++n){
            if((n-1)%flight==0)HR(w.queue->Wait(gate.Get(),++gateValue));
            w.Submit([&](auto* list){
                GpuRequest r;r.list=list;r.queue=nativeQueueUnknown?nullptr:w.queue.Get();r.base=base.Get();r.target=targets[n-1].Get();r.depth=depth.Get();r.motion=motion.Get();
                auto& v=r.source;v.source=SourceRef::Fixture(n);if(n>1)v.predecessor=SourceRef::Fixture(n-1);
                v.identityObserved=v.d3d12=v.postSr=v.rgba16f=v.currentAccess=v.clockKnown=v.originalMetadata=true;
                v.width=v.height=v.workWidth=v.workHeight=8;v.samples=1;v.timeUs=n*10000;v.scope=v.linearDomain=v.scale.domain=1;
                v.motion=v.depth={0,0,8,8,8,8};v.motionConvention=v.depthEncoding=Provenance::SchemaDefined;
                session.Begin(w.device.Get(),v,enabled,false);
                bool common=false;auto maintain=[&](){if(!common){++commonCalls;common=true;}};
                // Scripted provider writes the exact stored E into the actual
                // current output; the same production branch decides entry.
                const auto result=session.Dispatch(r,maintain,[&](){
                    maintain();++providerCalls;
                    AfnrWarp::Barrier(list,r.target,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_DEST);
                    list->CopyResource(r.target,enhanced.Get());
                    AfnrWarp::Barrier(list,r.target,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                    CHECK(session.CompleteFull(r));
                });
                if(result==CarryResult::Carried) {
                    ++omissions;CHECK(session.Output());CHECK(session.Output()->Target()==r.target);
                    Neurotic::Protocol::NativeExecutionFacts facts;facts.commandsRecorded=true;facts.outputPass=session.Output();
                    facts.composition=Neurotic::Contracts::CompositionStatus::Recorded;
                    CHECK(Neurotic::Protocol::NativeFactsCoherent(facts));CHECK(!facts.modelAttempted&&!facts.modelSucceeded);
                    facts.actualResetRequested=false;CHECK(!Neurotic::Protocol::NativeFactsCoherent(facts));
                }
                CHECK(result!=CarryResult::FailedAfterEffects);
                CHECK(DlssNr::GpuSafety::DerivedAllocatedBytes()<=AllocationCap);
            },hostMode==1,hostMode==2);
            if(n%flight==0||n==12){HR(gate->Signal(gateValue));w.Wait();}
        }
        CHECK(providerCalls==(enabled?7u:12u));CHECK(omissions==(enabled?5u:0u));CHECK(commonCalls==12);
        if(enabled){CHECK(session.State().lastModelSource==SourceRef::Fixture(11));CHECK(session.State().lastSource==SourceRef::Fixture(12));}
        // Exact current resources, including every carried output, are read by
        // the test consumer after submission; a proxy/source-ID match cannot pass.
        for(auto& target:targets){const auto pixels=w.Read(target.Get(),8);CHECK(pixels.size()==sizeof(e));CHECK(memcmp(pixels.data(),e.data(),sizeof(e))==0);}
        session.Disable();DlssNr::GpuSafety::Pending();
        CHECK(DlssNr::GpuSafety::DerivedAllocatedBytes()==0);
    }
    w.Clean();std::cout<<"AFNR dispatch PASS "<<checks<<" checks; owned/reset/replayable host; known/unknown queue; 1/2/3/4 pending frames; 7 provider calls / 5 omissions; off 12/0; exact current outputs\n";
}
