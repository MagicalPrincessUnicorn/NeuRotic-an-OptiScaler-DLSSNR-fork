"""Compile and run the actual menu timing functions with controlled clocks; no game/GPU."""
from pathlib import Path
import subprocess,sys
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'builds/native-color/frame-cadence';OUT.mkdir(parents=True,exist_ok=True)
src=(ROOT/'OptiScaler/menu/menu_common.cpp').read_text()
def function(name):
    start=src.index('void MenuCommon::'+name+'(');brace=src.index('{',start);depth=1;i=brace+1
    while depth:
        depth+=(src[i]=='{')-(src[i]=='}');i+=1
    return src[start:i]
header=ROOT/'OptiScaler/menu/FrameCadence.h'
cpp=r'''#define LOG_INFO(...) ((void)0)
#include <array>
#include <deque>
#include <cmath>
#include <iostream>
#include <algorithm>
#include <cstdint>
#include <cassert>
#include <mutex>
#if __has_include("menu/FrameCadence.h")
#include "menu/FrameCadence.h"
#else
namespace Neurotic::FrameCadence {struct Snapshot{};struct Clock{void Observe(uintptr_t,uint64_t,double){}};inline Clock& GameFrames(){static Clock c;return c;}inline thread_local uintptr_t outputOwner=0;}
#endif
struct Optional{double v=1;double value_or_default()const{return v;}double value_or(double)const{return v;}};
struct Config{Optional OverlayMenu,ShowFps,FpsScale;};
enum class FGInput{NoFG,Active};enum class FGOutput{NoFG,Active};
struct State{FGInput activeFgInput=FGInput::Active;FGOutput activeFgOutput=FGOutput::Active;std::deque<double> frameTimes=std::deque<double>(300,0),upscaleTimes=std::deque<double>(300,0);};
struct Buffer{std::deque<float> values;void Push(float f){values.push_back(f);}float Average(){double s=0;for(auto f:values)s+=f;return values.empty()?0:s/values.size();}};
Buffer gFrameTimes,gUpscalerTimes;double lastTime=0,lastFrameTime=0;int _frameCount=0;void* _handle=nullptr;bool _isVisible=true;
void UpdateManualInput(void*){}bool HasUpscalerGpuTiming(State&){return false;}
struct MenuCommon{struct RenderMenuContext{State& state;Config* config;double now=0,frameTime=0,frameRate=0;bool frameTimesCalculated=false;float menuResScale=1,fpsScale=1,averageFrameTime=0,averageUpscalerFT=0;Neurotic::FrameCadence::Snapshot gameTiming;double callbackFrameTime=0;};static void Present(){};static void UpdateRenderTiming(RenderMenuContext&);static void UpdateFrameTimeAverages(RenderMenuContext&);};
'''
cpp+=function('UpdateRenderTiming')+'\n'+function('UpdateFrameTimeAverages')
cpp+=r'''
int main(){State state;Config config;MenuCommon::RenderMenuContext ctx{state,&config};Neurotic::FrameCadence::outputOwner=1;
for(int i=0;i<120;i++){double t=100.+i*22.;Neurotic::FrameCadence::GameFrames().Observe(1,1,t);for(double d:{0.,1.}){ctx.now=t+d;MenuCommon::UpdateRenderTiming(ctx);MenuCommon::UpdateFrameTimeAverages(ctx);}}
if(std::abs(ctx.frameTime-22.)>.001){std::cerr<<"FAIL game frame expected 22 ms, got "<<ctx.frameTime<<" ms from output callbacks\n";return 1;}
if(std::abs(state.frameTimes.back()-1.)>.001){std::cerr<<"FAIL raw callback interval lost\n";return 1;}

#if __has_include("menu/FrameCadence.h")
auto& clock=Neurotic::FrameCadence::GameFrames();
auto snap=clock.Read(1,ctx.now);assert(snap.count==119);assert(snap.Latest()==22.);
// A real 200 ms stall must survive both the selected number and the graph.
clock.Observe(1,1,ctx.now+199.);ctx.now+=199.;MenuCommon::UpdateRenderTiming(ctx);MenuCommon::UpdateFrameTimeAverages(ctx);
assert(ctx.gameTiming.Latest()==200.);assert(ctx.gameTiming.At(ctx.gameTiming.count-1)==200.);
// FG transitions, resize, and ownership changes seed a new clock, not a spike.
clock.Observe(1,2,ctx.now+3.);assert(clock.Read(1,ctx.now+3.).count==0);
clock.Observe(1,2,ctx.now+23.);assert(clock.Read(1,ctx.now+23.).Latest()==20.);
clock.Invalidate(1);assert(clock.Read(1,ctx.now+23.).owner==0);
clock.Observe(2,2,5000.);clock.Observe(2,2,5020.);assert(clock.Read(1,5020.).owner==0);
assert(clock.Read(2,5271.).owner==0);assert(clock.Read(2,5019.).owner==0);
// Resuming a stale stream retains the genuine stall; no clamp or smoothing.
clock.Observe(2,2,5500.);assert(clock.Read(2,5500.).Latest()==480.);
for(int i=1;i<=400;++i)clock.Observe(2,2,5500.+i*20.);
snap=clock.Read(2,13500.);assert(snap.count==360);assert(snap.Average()==20.);assert(snap.At(0)==20.);
clock.Observe(2,2,13500.);assert(clock.Read(2,13500.).count==360);
clock.Observe(2,2,100.);assert(clock.Read(2,100.).count==0);
clock.Observe(2,2,NAN);assert(clock.Read(2,100.).owner==0);
// Unknown/stale owners retain the callback clock and never infer base FPS.
Neurotic::FrameCadence::outputOwner=99;ctx.now+=22.;MenuCommon::UpdateRenderTiming(ctx);MenuCommon::UpdateFrameTimeAverages(ctx);
assert(ctx.gameTiming.owner==0);assert(ctx.frameTime==ctx.callbackFrameTime);
{Neurotic::FrameCadence::OutputScope a(1);{Neurotic::FrameCadence::OutputScope b(2);assert(Neurotic::FrameCadence::outputOwner==2);}assert(Neurotic::FrameCadence::outputOwner==1);}assert(Neurotic::FrameCadence::outputOwner==99);
std::cout<<"PASS stalls, FG epoch, resize invalidation, foreign/stale ownership, rollover, duplicate timestamp, backwards/invalid clock, fallback and nested output scope\n";
#endif
std::cout<<"PASS production timing: game 22 ms, raw callback 1 ms; no FG division or repeated game samples\n";
}
'''
(OUT/'test.cpp').write_text(cpp)
batch='@echo off\ncall "C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Auxiliary\\Build\\vcvars64.bat" -vcvars_ver=14.44 >nul\ncl /nologo /std:c++20 /EHsc /I OptiScaler builds\\native-color\\frame-cadence\\test.cpp /Fe:builds\\native-color\\frame-cadence\\test.exe /Fo:builds\\native-color\\frame-cadence\\test.obj\nif not "%errorlevel%"=="0" exit /b %errorlevel%\nbuilds\\native-color\\frame-cadence\\test.exe\nexit /b %errorlevel%\n'
(OUT/'test.cmd').write_text(batch)
r=subprocess.run(['cmd','/d','/c',str(OUT/'test.cmd')],cwd=ROOT,capture_output=True,text=True);print(r.stdout+r.stderr);sys.exit(r.returncode)
