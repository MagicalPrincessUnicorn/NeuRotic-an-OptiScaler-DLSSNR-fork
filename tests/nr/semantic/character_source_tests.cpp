#include "nr/semantic/character/CharacterSourcePolicy.h"
#include "nr/semantic/character/CharacterQueuePolicy.h"
#include <iostream>
#include <stdexcept>
using namespace Neurotic::Semantic::Character;
static unsigned checks=0;
static void Check(bool value,const char* label){++checks;if(!value)throw std::runtime_error(label);}
int main(){try{
    Check(CharacterResizeBufferCount(1,true)==1,"one-entry resize arrays never expanded");
    Check(CharacterResizeBufferCount(0,true)==0,"zero resize count with arrays preserved");
    Check(CharacterResizeBufferCount(1,false)==2,"unbound resize count permits tearing minimum");
    Check(CharacterResizeBufferCount(3,true)==3,"larger resize arrays retain extent");
    int a=1,b=2;int* uniform[]{&a,&a};int* mixed[]{&a,&b};int* missing[]{nullptr,nullptr};
    Check(CharacterQueueUpdate(false,2,mixed)==QueueUpdate::Keep,"failed resize retains queue association");
    Check(CharacterQueueUpdate<int>(true,2,nullptr)==QueueUpdate::Keep,"null array leaves existing association");
    Check(CharacterQueueUpdate(true,2,uniform)==QueueUpdate::Uniform,"same queue qualifies all buffers");
    Check(CharacterQueueUpdate(true,2,mixed)==QueueUpdate::Unsupported,"different queues refuse Inspector");
    Check(CharacterQueueUpdate(true,2,missing)==QueueUpdate::Unsupported,"missing queue refuses Inspector");
    Check(CharacterQueueUpdate(true,0,uniform)==QueueUpdate::Unsupported,"unknown count refuses Inspector");
    Check(CharacterQueueUpdate(true,17,uniform)==QueueUpdate::Unsupported,"oversized array refused without reading");
    DlssNr::HdrObservation::Snapshot source;
    source.format=DXGI_FORMAT_R8G8B8A8_UNORM;source.colorSpace=DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
    Check(!CharacterSourceColor(source),"unregistered default rejected");
    source.registered=true;
    Check(CharacterSourceColor(source)&&CharacterSourceColor(source)->dxgiDefault,"fresh R8 default admitted as derived");
    source.format=DXGI_FORMAT_B8G8R8A8_UNORM;
    Check(CharacterSourceColor(source)&&CharacterSourceColor(source)->conversion==0,"fresh B8 default admitted");
    Check(!source.colorSpaceObserved,"default admission does not fabricate setter observation");
    source.transitioning=true;Check(!CharacterSourceColor(source),"resize transition refuses default");source.transitioning=false;
    source.requestedColorSpace=DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;source.colorSpaceResult=E_FAIL;
    Check(CharacterSourceColor(source)&&CharacterSourceColor(source)->dxgiDefault,"failed setter retains actual default");
    source.colorSpaceObserved=true;source.colorSpace=DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
    Check(!CharacterSourceColor(source),"explicit PQ on byte format rejected");
    source.colorSpace=DXGI_COLOR_SPACE_CUSTOM;Check(!CharacterSourceColor(source),"explicit unknown never falls back");
    source.colorSpace=DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
    Check(CharacterSourceColor(source)&&!CharacterSourceColor(source)->dxgiDefault,"observed SDR stays observed");
    source.format=DXGI_FORMAT_R10G10B10A2_UNORM;source.colorSpace=DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
    Check(CharacterSourceColor(source)&&CharacterSourceColor(source)->conversion==1,"observed PQ admitted");
    source.colorSpaceObserved=false;Check(!CharacterSourceColor(source),"R10 requires observed policy");
    source.colorSpace=DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
    Check(CharacterSourceColor(source)&&CharacterSourceColor(source)->conversion==0&&CharacterSourceColor(source)->dxgiDefault,"default 10-bit SDR admitted");
    source.colorSpaceObserved=true;
    Check(CharacterSourceColor(source)&&CharacterSourceColor(source)->conversion==0&&!CharacterSourceColor(source)->dxgiDefault,"observed 10-bit SDR admitted");
    source.colorSpaceObserved=false;
    source.format=DXGI_FORMAT_R16G16B16A16_FLOAT;source.colorSpace=DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
    Check(CharacterSourceColor(source)&&CharacterSourceColor(source)->conversion==2&&CharacterSourceColor(source)->dxgiDefault,"DXGI default linear float admitted");
    source.colorSpaceObserved=true;Check(CharacterSourceColor(source)&&CharacterSourceColor(source)->conversion==2,"observed scRGB admitted");
    Check(!CharacterFgReported(false,false,false),"selected route without active output is not activity");
    Check(!CharacterFgReported(false,false,true),"inactive paused generator not activity");
    Check(CharacterFgReported(true,false,false),"observed native work blocks capture");
    Check(!CharacterFgReported(false,false,false),"no recent native work admits absent managed output");
    Check(!CharacterFgReported(false,false,false),"requested FSR input with inactive output is not activity");
    Check(CharacterFgReported(false,true,false),"active managed output blocks capture");
    Check(!CharacterFgReported(false,true,true),"paused managed output alone is inactive");
    Check(!CharacterFgReported(false,false,false),"teardown after enabled input request cannot latch FG");
    std::cout<<"PASS "<<checks<<" source provenance and FG activity checks\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
