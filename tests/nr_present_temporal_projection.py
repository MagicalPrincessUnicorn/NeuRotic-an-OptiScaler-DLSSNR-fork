"""Compile the actual Present-to-compositor projection, without executing a model."""
from pathlib import Path
import subprocess,sys
root=Path(__file__).resolve().parents[1]
s=(root/'OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp').read_text(encoding='utf-8')
start=s.index('    DlssNrFrameInfo frame {};',s.index('bool EvaluateImageOnlyCommandList('))
end=s.index('    if (colorContract && colorContract->hdr)',start)
body=s[start:end]
out=root/'builds/native-color';out.mkdir(exist_ok=True)
cpp=r'''#include <shaders/dlssnr/DlssNr_Common.h>
#include <cstdio>
#include <cmath>
using UINT=unsigned;
static int failures=0;
void Check(bool value,const char* name){if(!value){++failures;std::printf("FAIL %s\n",name);}}
DlssNrFrameInfo Project(const DlssNrFrameInfo* nativeGuideFrame, unsigned workWidth,unsigned workHeight){
 struct {unsigned Width=3840,Height=2160;} frameDesc;
 bool resetHistory=false;
'''+body+r'''
 return frame;
}
int main(){
 DlssNrFrameInfo native;native.RenderSubrectWidth=2258;native.RenderSubrectHeight=1270;
 native.DepthSubrectWidth=2258;native.DepthSubrectHeight=1270;
 native.MotionSubrectWidth=2258;native.MotionSubrectHeight=1270;
 native.MvScaleX=2258;native.MvScaleY=-1270;
 native.JitterX=.25f;native.JitterY=-.375f;native.JitterSupplied=true;
 native.DepthSubrectX=3;native.MotionSubrectY=5;native.Reset=true;
 native.ExposureTexture=reinterpret_cast<void*>(1);native.PreExposure=8;native.ColourIsLinearHdr=true;
 for(auto work: {std::pair{3840u,2160u},std::pair{2256u,1264u},std::pair{1920u,1080u}}){
  auto f=Project(&native,work.first,work.second);
  auto scale=DlssNrWorkingMotionScale(3840,2160,work.first,work.second);
  auto temporal=DlssNrModelTemporal({{f.MvScaleX,f.MvScaleY},{f.JitterX,f.JitterY}},
   f.PresentNativeGuides,scale,3840,2160,work.first,work.second);
  Check(temporal.values.motionScale.x==2258.f,"retained native horizontal motion scale");
  Check(temporal.values.motionScale.y==-1270.f,"retained native vertical motion scale");
  for(auto pass: {std::pair{work.first,work.second},std::pair{1280u,720u},std::pair{2560u,1440u}}){
   auto j=DlssNrModelJitter(f,3840,2160,pass.first,pass.second);
   Check(std::abs(j.x-.25f*pass.first/2258)<.00001f&&std::abs(j.y+.375f*pass.second/1270)<.00001f,"each pass maps render jitter once to its own actual raster");
  }
  Check(f.DepthSubrectX==3&&f.MotionSubrectY==5&&f.Reset,"guide origins/reset preserved");
  Check(!f.ColourIsLinearHdr&&!f.ExposureTexture&&f.PreExposure==1,"display color never borrows scene exposure");
 }
 native.MotionSubrectWidth=3840;native.MotionSubrectHeight=2160;native.MvScaleX=3840;native.MvScaleY=-2160;
 auto high=Project(&native,1920,1080);auto sc=DlssNrWorkingMotionScale(3840,2160,1920,1080);
 auto ht=DlssNrModelTemporal({{high.MvScaleX,high.MvScaleY},{high.JitterX,high.JitterY}},
  high.PresentNativeGuides,sc,3840,2160,1920,1080);
 Check(ht.values.motionScale.x==3840.f&&ht.values.motionScale.y==-2160.f,"output-resolution motion retains its own guide scale");
 auto same=native;same.JitterX=-.4f;same.Reset=!native.Reset;
 Check(DlssNrSameNativeGuideHistory(native,same),"normal jitter/reset values do not redefine source identity");
 same.NativeFeatureGeneration=9;
 Check(!DlssNrSameNativeGuideHistory(native,same),"recreated Native feature invalidates history");
 same=native;same.NativeFeatureHandle=7;
 Check(!DlssNrSameNativeGuideHistory(native,same),"different Native view/feature invalidates history");
 same=native;same.MotionSubrectWidth/=2;
 Check(!DlssNrSameNativeGuideHistory(native,same),"motion raster convention change invalidates history");
 same=native;same.MotionVectorsJittered=true;
 Check(!DlssNrSameNativeGuideHistory(native,same),"motion jitter convention change invalidates history");
 auto legacy=DlssNrModelJitter(native,3840,2160,1920,1080);
 Check(legacy.x==native.JitterX&&legacy.y==native.JitterY,"Native compositor jitter remains unchanged");
 auto prepared=native;prepared.PreparedSource.emplace();prepared.JitterX=prepared.JitterY=0;
 auto preparedOutput=Project(&prepared,1920,1080);
 Check(!preparedOutput.PresentNativeGuides&&preparedOutput.PreparedSource.has_value(),"prepared inputs are never relabeled Native");
 Check(preparedOutput.MvScaleX==prepared.MvScaleX&&preparedOutput.MvScaleY==prepared.MvScaleY,"prepared motion retains its established normalization");
 std::printf("Present Native projection: %d failures\n",failures);return failures?1:0;
}
'''
(out/'present-projection.cpp').write_text(cpp,encoding='utf-8')
cmd='@echo off\nsetlocal\ncall "C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat" -vcvars_ver=14.44 >nul 2>&1\nif not "%errorlevel%"=="0" exit /b %errorlevel%\ncl /nologo /std:c++20 /EHsc /I OptiScaler /I OptiScaler/include builds/native-color/present-projection.cpp /Fo:builds/native-color/present-projection.obj /Fe:builds/native-color/present-projection.exe\nif not "%errorlevel%"=="0" exit /b %errorlevel%\nbuilds\\native-color\\present-projection.exe\nexit /b %errorlevel%\n'
(out/'present-projection.cmd').write_text(cmd,encoding='utf-8')
sys.exit(subprocess.call(['cmd','/c',str(out/'present-projection.cmd')],cwd=root))
