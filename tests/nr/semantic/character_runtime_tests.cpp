#define NOMINMAX
#include <windows.h>
#include "nr/semantic/character/CharacterRuntime.h"
#include "nr/semantic/character/CharacterFgActivity.h"
#include <dlssnr/HdrObservation.h>
#include <dxgi1_6.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <chrono>
#include <thread>
#include <iostream>
#include "character_test_errors.h"
using namespace Neurotic::Semantic::Character;
using Microsoft::WRL::ComPtr;
static void HR(HRESULT r){if(FAILED(r))throw std::runtime_error("D3D12 host operation failed");}
static NativeFgWorkTracker::Token racedFg;
static void BeginFgDuringAdmission(){
    SetCharacterAdmissionTestHook(nullptr);
    racedFg=NativeFgWork().Begin(77,1);
    CharacterNativeFgStarted();
}
int main(int argc,char** argv){try{
    CharacterTestQuietFailures();
    if(argc!=2)throw std::runtime_error("worker folder required");
    InspectorSettings off;
    CharacterPresent(nullptr,nullptr,false,true,off);
    if(TryCharacterRuntimeView().captures||CharacterWorkerRequested())throw std::runtime_error("off path started work");
    StartCharacterWorker(std::filesystem::absolute(argv[1]));
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(TryCharacterRuntimeView().worker==WorkerState::Starting&&std::chrono::steady_clock::now()<end)std::this_thread::sleep_for(std::chrono::milliseconds(5));
    if(TryCharacterRuntimeView().worker!=WorkerState::Ready){std::cerr<<TryCharacterRuntimeView().reason<<'\n';throw std::runtime_error("child handshake failed");}
    ComPtr<IDXGIFactory4> factory;HR(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));ComPtr<IDXGIAdapter> adapter;HR(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
    ComPtr<ID3D12Device> device;HR(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
    ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC q{};HR(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)));
    HWND window=CreateWindowExW(0,L"STATIC",L"Character test host",WS_OVERLAPPED,0,0,640,480,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);if(!window)throw std::runtime_error("hidden host window failed");
    DXGI_SWAP_CHAIN_DESC1 desc{};desc.Width=320;desc.Height=240;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT|DXGI_USAGE_SHADER_INPUT;desc.BufferCount=2;desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
    ComPtr<IDXGISwapChain1> base;HR(factory->CreateSwapChainForHwnd(queue.Get(),window,&desc,nullptr,nullptr,&base));ComPtr<IDXGISwapChain3> chain;HR(base.As(&chain));
    DlssNr::HdrObservation::Registry::Instance().Register(chain.Get(),desc.Format);
    // Crimson Desert SDR uses DXGI's default color space without SetColorSpace1.
    // Keep that provenance distinct from a successfully observed setter call.
    InspectorSettings settings;settings.enabled=true;
    while(std::chrono::steady_clock::now()<end&&TryCharacterRuntimeView().results<1){
        DlssNr::HdrObservation::Registry::Instance().RecordMetadata(chain.Get(),DXGI_HDR_METADATA_TYPE_NONE,0,nullptr,S_OK);
        CharacterPresent(chain.Get(),queue.Get(),false,true,settings);std::this_thread::sleep_for(std::chrono::milliseconds(5));}
    const auto view=TryCharacterRuntimeView();if(!view.sourceQualified||!view.captures||!view.results){std::cerr<<view.reason<<" captures="<<view.captures<<" results="<<view.results<<'\n';throw std::runtime_error("source-to-worker pipeline failed");}
    if(!view.dxgiDefaultColor||!view.processed||!view.noPose||DlssNr::HdrObservation::Registry::Instance().Read(chain.Get()).colorSpaceObserved)
        throw std::runtime_error("default SDR provenance or processing counters incorrect");
    const auto beforeRace=TryCharacterRuntimeView().captures;
    SetCharacterAdmissionTestHook(BeginFgDuringAdmission);
    CharacterPresent(chain.Get(),queue.Get(),false,true,settings,true);
    HeldSnapshot racedBoxes;DisplayContext racedDisplay;
    if(!TryCharacterRuntimeView().sourceQualified||TryCharacterRuntimeView().captures<beforeRace)
        throw std::runtime_error("native FG evaluation invalidated physical output capture");
    NativeFgWork().Finish(racedFg,false,1,CharacterActivityNow());NativeFgWork().Reset();
    CharacterSourceInvalidated();Snapshot old;DisplayContext oldDisplay;
    if(TryCharacterDisplay(old,oldDisplay))throw std::runtime_error("resize did not revoke display");
    ComPtr<ID3D12Fence> hold;HR(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&hold)));HR(queue->Wait(hold.Get(),1));
    const auto captured=TryCharacterRuntimeView().captures;
    const auto captureEnd=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while(TryCharacterRuntimeView().captures==captured&&std::chrono::steady_clock::now()<captureEnd){CharacterPresent(chain.Get(),queue.Get(),false,true,settings);std::this_thread::sleep_for(std::chrono::milliseconds(5));}
    if(TryCharacterRuntimeView().captures==captured)throw std::runtime_error("held swapchain capture missing");
    DlssNr::HdrObservation::Registry::Instance().BeginResize(chain.Get());
    const auto retirementStart=std::chrono::steady_clock::now();
    if(CharacterBeforeResize())throw std::runtime_error("pending resize was admitted");
    if(std::chrono::steady_clock::now()-retirementStart<std::chrono::milliseconds(490))throw std::runtime_error("retirement pressure not exercised");
    // The wrapper returns WAS_STILL_DRAWING here and never calls native resize.
    DlssNr::HdrObservation::Registry::Instance().CompleteResize(chain.Get(),DXGI_ERROR_WAS_STILL_DRAWING,DXGI_FORMAT_UNKNOWN);
    HR(hold->Signal(1));
    DlssNr::HdrObservation::Registry::Instance().BeginResize(chain.Get());
    if(!CharacterBeforeResize())throw std::runtime_error("completed sources did not retire");
    HR(chain->ResizeBuffers(2,400,300,desc.Format,0));
    DlssNr::HdrObservation::Registry::Instance().CompleteResize(chain.Get(),S_OK,desc.Format);
    if(TryCharacterDisplay(old,oldDisplay))throw std::runtime_error("old resize result admitted");
    CharacterPresent(chain.Get(),queue.Get(),true,true,settings);Snapshot snapshot;DisplayContext display;
    if(TryCharacterDisplay(snapshot,display))throw std::runtime_error("FG did not revoke display");
    if(TryCharacterRuntimeView().sourceQualified)throw std::runtime_error("FG left source qualified");
    HR(chain->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709));
    DlssNr::HdrObservation::Registry::Instance().RecordColorSpace(chain.Get(),DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709,S_OK);
    const auto beforeRecovery=TryCharacterRuntimeView().results;
    const auto recoveryEnd=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(TryCharacterRuntimeView().results==beforeRecovery&&std::chrono::steady_clock::now()<recoveryEnd){
        CharacterPresent(chain.Get(),queue.Get(),false,true,settings);std::this_thread::sleep_for(std::chrono::milliseconds(5));}
    const auto recovered=TryCharacterRuntimeView();
    if(!recovered.sourceQualified||recovered.dxgiDefaultColor||recovered.results==beforeRecovery)
        throw std::runtime_error("resize/FG recovery into observed SDR failed");
    // One to six physical outputs per application evaluation. The activity
    // token deliberately stays in flight while outputs are captured; it owns
    // provider input work, not this queue's physical display surface.
    const auto fgStart=TryCharacterRuntimeView().results;
    for(unsigned ratio=1;ratio<=6;++ratio){
        auto token=NativeFgWork().Begin(77,1);CharacterNativeFgStarted();
        for(unsigned output=0;output<ratio;++output){
            CharacterPresent(chain.Get(),queue.Get(),true,true,settings,true);
            if(!TryCharacterRuntimeView().sourceQualified)throw std::runtime_error("MFG physical output lost qualification");
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        NativeFgWork().Finish(token,true,1,CharacterActivityNow());
    }
    const auto fgDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while(TryCharacterRuntimeView().results==fgStart&&std::chrono::steady_clock::now()<fgDeadline){
        CharacterPresent(chain.Get(),queue.Get(),true,true,settings,true);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if(TryCharacterRuntimeView().results==fgStart||!TryCharacterDisplay(snapshot,display)||!display.fgActive||!display.postFgQualified)
        throw std::runtime_error("MFG physical output failed to produce admitted worker result");
    CharacterPresent(chain.Get(),queue.Get(),true,false,settings,true);
    if(TryCharacterDisplay(snapshot,display)||TryCharacterRuntimeView().sourceQualified)throw std::runtime_error("MFG focus loss retained display");
    NativeFgWork().Reset();
    // Exercise physical capture with FG active through SDR10 -> HDR10 ->
    // scRGB transitions. This headless fixture supplies observed color metadata;
    // it does not claim monitor/driver HDR negotiation or proprietary generation.
    for(unsigned policy=0;policy<3;++policy){
        const auto format=policy<2?DXGI_FORMAT_R10G10B10A2_UNORM:DXGI_FORMAT_R16G16B16A16_FLOAT;
        DlssNr::HdrObservation::Registry::Instance().BeginResize(chain.Get());
        if(!CharacterBeforeResize())throw std::runtime_error("HDR transition did not retire");
        HR(chain->ResizeBuffers(2,400,300,format,0));
        DlssNr::HdrObservation::Registry::Instance().CompleteResize(chain.Get(),S_OK,format);
        DlssNr::HdrObservation::Registry::Instance().RecordColorSpace(chain.Get(),policy==0?DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709:policy==1?DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020:DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709,S_OK);
        const auto beforeHdr=TryCharacterRuntimeView().results;
        const auto hdrDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
        while(TryCharacterRuntimeView().results==beforeHdr&&std::chrono::steady_clock::now()<hdrDeadline){
            CharacterPresent(chain.Get(),queue.Get(),true,true,settings,true);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        if(TryCharacterRuntimeView().results==beforeHdr||!TryCharacterDisplay(snapshot,display)||!display.postFgQualified)
            throw std::runtime_error("FG color transition failed to resume with current result");
    }
    // The known application boundary admits real images; its physical outputs
    // must not submit more analysis work, including repeated 6x groups.
    const auto beforeReal=TryCharacterRuntimeView().captures;
    std::uint64_t appToken=0;const auto realDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    do {CharacterPresent(chain.Get(),queue.Get(),true,true,settings,true,77,true,++appToken);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    } while(TryCharacterRuntimeView().captures==beforeReal&&std::chrono::steady_clock::now()<realDeadline);
    if(TryCharacterRuntimeView().captures==beforeReal)throw std::runtime_error("application real image was not captured");
    const auto realCaptures=TryCharacterRuntimeView().captures;
    for(unsigned i=0;i<12;++i)CharacterPresent(chain.Get(),queue.Get(),true,true,settings,true,77);
    if(TryCharacterRuntimeView().captures!=realCaptures||!TryCharacterRuntimeView().realFrameSource)
        throw std::runtime_error("generated output resampled a bound real image");
    CharacterPresent(chain.Get(),queue.Get(),true,true,settings,true,77,true,appToken);
    if(TryCharacterRuntimeView().captures!=realCaptures)throw std::runtime_error("duplicate real token captured twice");
    CharacterPresent(chain.Get(),queue.Get(),true,true,settings,true);
    if(TryCharacterRuntimeView().captures!=realCaptures)throw std::runtime_error("missing owner association sampled generated output");
    CharacterPresent(chain.Get(),queue.Get(),true,false,settings,true,77);
    if(TryCharacterRuntimeView().sourceQualified)throw std::runtime_error("focus loss retained real display admission");
    CharacterSwapchainReleased(chain.Get(),queue.Get());
    if(TryCharacterRuntimeView().realFrameSource)throw std::runtime_error("released application owner blocks later unbound D3D12 capture");
    const auto afterRelease=TryCharacterRuntimeView().captures;
    const auto unboundDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while(TryCharacterRuntimeView().captures==afterRelease&&std::chrono::steady_clock::now()<unboundDeadline){
        CharacterPresent(chain.Get(),queue.Get(),false,true,settings);std::this_thread::sleep_for(std::chrono::milliseconds(5));}
    if(TryCharacterRuntimeView().captures==afterRelease)throw std::runtime_error("unbound D3D12 capture did not recover after source release");
    // Actual DX11 device/swapchain -> private GPU thumbnail -> same worker.
    ComPtr<ID3D11Device> device11;ComPtr<ID3D11DeviceContext> context11;D3D_FEATURE_LEVEL level11{};
    HR(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device11,&level11,&context11));
    ComPtr<IDXGIDevice> dxgi11;HR(device11.As(&dxgi11));ComPtr<IDXGIAdapter> adapter11;HR(dxgi11->GetAdapter(&adapter11));
    ComPtr<IDXGIFactory2> factory11;HR(adapter11->GetParent(IID_PPV_ARGS(&factory11)));
    HWND window11=CreateWindowExW(0,L"STATIC",L"Inspector DX11 runtime",WS_OVERLAPPED,0,0,640,480,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(!window11)throw std::runtime_error("DX11 hidden window failed");
    ComPtr<IDXGISwapChain1> chain11;HR(factory11->CreateSwapChainForHwnd(device11.Get(),window11,&desc,nullptr,nullptr,&chain11));
    DlssNr::HdrObservation::Registry::Instance().Register(chain11.Get(),desc.Format);
    const auto dx11Before=TryCharacterRuntimeView().results;
    const auto dx11End=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(TryCharacterRuntimeView().results==dx11Before&&std::chrono::steady_clock::now()<dx11End){
        CharacterPresentDx11(chain11.Get(),device11.Get(),true,true,settings,true);
        context11->Flush();std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if(TryCharacterRuntimeView().results==dx11Before||TryCharacterRuntimeView().sourceApi!=1||!TryCharacterDisplay(snapshot,display))
        throw std::runtime_error("actual DX11-to-worker pipeline failed");
    HWND auxiliaryWindow=CreateWindowExW(0,L"STATIC",L"Inspector auxiliary",WS_OVERLAPPED,0,0,640,480,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(!auxiliaryWindow)throw std::runtime_error("auxiliary hidden window failed");
    ComPtr<IDXGISwapChain1> auxiliaryChain;
    HR(factory11->CreateSwapChainForHwnd(device11.Get(),auxiliaryWindow,&desc,nullptr,nullptr,&auxiliaryChain));
    DlssNr::HdrObservation::Registry::Instance().Register(auxiliaryChain.Get(),desc.Format);
    CharacterSwapchainReleased(auxiliaryChain.Get(),device11.Get());
    if(!TryCharacterRuntimeView().sourceQualified||!TryCharacterDisplay(snapshot,display))
        throw std::runtime_error("unrelated same-device swapchain release invalidated active stream");
    DlssNr::HdrObservation::Registry::Instance().Unregister(auxiliaryChain.Get());auxiliaryChain.Reset();DestroyWindow(auxiliaryWindow);
    if(!CharacterBeforeResize(device11.Get()))throw std::runtime_error("DX11 runtime sources did not retire");
    CharacterSwapchainReleased(chain11.Get(),device11.Get());
    if(TryCharacterRuntimeView().sourceQualified)throw std::runtime_error("released DX11 source remained qualified");
    DlssNr::HdrObservation::Registry::Instance().Unregister(chain11.Get());chain11.Reset();DestroyWindow(window11);
    // Generic completed Vulkan image handoff exercises the same real worker;
    // separate Vulkan tests prove its actual GPU capture and fence ordering.
    ExternalCharacterSource external;external.api=2;external.width=320;external.height=240;external.deviceIdentity=777;
    external.swapchainGeneration=100;external.resizeGeneration=1;external.qualified=true;external.fgActive=true;external.srgbAttachment=true;
    const auto extBefore=TryCharacterRuntimeView().results;CpuFrame stale;
    const auto extEnd=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while(TryCharacterRuntimeView().results==extBefore&&std::chrono::steady_clock::now()<extEnd){
        if(auto frame=CharacterBeginExternalFrame(external,settings)){
            frame->width=320;frame->height=240;frame->stride=1280;frame->pixels.resize(1280*240);
            stale=*frame;
            auto malformed=*frame;malformed.stride=1;
            if(CharacterPublishExternalFrame(std::move(malformed)))throw std::runtime_error("malformed external pixels admitted");
            if(!CharacterPublishExternalFrame(std::move(*frame)))throw std::runtime_error("valid external capture refused");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if(TryCharacterRuntimeView().results==extBefore||!TryCharacterDisplay(snapshot,display)||display.colorPolicy!=4)
        throw std::runtime_error("external frame worker/output encoding handoff failed");
    ++external.swapchainGeneration;CharacterBeginExternalFrame(external,settings);
    auto staleCopy=stale;
    if(CharacterPublishExternalFrame(std::move(staleCopy))||TryCharacterDisplay(snapshot,display))throw std::runtime_error("external old generation survived source replacement");
    --external.swapchainGeneration;CharacterBeginExternalFrame(external,settings);
    if(CharacterPublishExternalFrame(std::move(stale)))throw std::runtime_error("A-B-A external source transition resurrected old epoch");
    external.qualified=false;CharacterBeginExternalFrame(external,settings);
    if(TryCharacterRuntimeView().sourceQualified)throw std::runtime_error("unqualified external source remained enabled");
    const auto start=std::chrono::steady_clock::now();StopCharacterWorker();if(std::chrono::steady_clock::now()-start>std::chrono::milliseconds(10))throw std::runtime_error("stop blocked host");
    if(TryCharacterDisplay(snapshot,display))throw std::runtime_error("disable did not revoke display");
    const auto stopEnd=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while(TryCharacterRuntimeView().worker==WorkerState::Stopping&&std::chrono::steady_clock::now()<stopEnd)std::this_thread::sleep_for(std::chrono::milliseconds(5));
    if(TryCharacterRuntimeView().worker!=WorkerState::Stopped)throw std::runtime_error("worker stop failed");
    DlssNr::HdrObservation::Registry::Instance().Unregister(chain.Get());chain.Reset();base.Reset();DestroyWindow(window);
    std::cout<<"PASS actual worker/WARP: D3D12 and DX11 pipelines; external Vulkan handoff/epoch/shape checks; default SDR; repeated HDR metadata; native evaluation preserves output epoch; unknown FG owner refuses; 1x-6x physical outputs; focus revoke; pending resize >500ms defer/retire/retry; active FG with SDR10/PQ/scRGB transitions; nonblocking stop; fgCaptures="<<TryCharacterRuntimeView().fgCaptures<<"\n";return 0;
}catch(const std::exception& e){StopCharacterWorker();std::cerr<<e.what()<<'\n';std::this_thread::sleep_for(std::chrono::milliseconds(150));return 1;}}
