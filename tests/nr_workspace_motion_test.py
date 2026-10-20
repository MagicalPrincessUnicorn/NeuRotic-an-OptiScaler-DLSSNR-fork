"""Exercise the real placement tween, including reversal and motion-off. No GPU/game."""
from pathlib import Path
import subprocess, sys
root=Path(__file__).resolve().parents[1]
out=root/'builds/universal-input/workspace-tests';out.mkdir(parents=True,exist_ok=True)
cpp=r'''
#include <array>
#include <cmath>
#include <cassert>
#include <iostream>
#if __has_include("dlssnr/NrPipelineMotion.h")
#include "dlssnr/NrPipelineMotion.h"
int main(){using namespace DlssNr::PipelineUi;Motion m;std::array<float,7> a{0,0,3,1,2,0,4},b{0,0,1,2,3,0,4};
auto p=m.Update(a,0,false);assert(p==a);p=m.Update(b,1,false);assert(p==a);
p=m.Update(b,1.09,false);assert(p[2]>1&&p[2]<3);const auto halfway=p;
p=m.Update(a,1.09,false);assert(p==halfway);p=m.Update(a,1.30,false);assert(p==a);
p=m.Update(b,2,true);assert(p==b);p=m.Update(a,3,false);assert(p==b);
p=m.Update(a,3.01,true);assert(p==a);p=m.Update(b,4,false,true);assert(p==b);
p=m.Update(a,3.9,false);assert(p==a);std::cout<<"PASS initial placement, interpolation, interruption, completion, reduced motion, resize, clock reversal\n";}
#else
int main(){std::cerr<<"FAIL processing-order changes have no motion implementation\n";return 1;}
#endif
'''
(out/'motion.cpp').write_text(cpp)
cmd='@echo off\ncall "C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Auxiliary\\Build\\vcvars64.bat" -vcvars_ver=14.44 >nul\ncl /nologo /std:c++20 /EHsc /I"'+str(root/'OptiScaler')+'" "'+str(out/'motion.cpp')+'" /Fo:"'+str(out/'motion.obj')+'" /Fe:"'+str(out/'motion.exe')+'"\nif errorlevel 1 exit /b %errorlevel%\n"'+str(out/'motion.exe')+'"\n'
(out/'motion.cmd').write_text(cmd)
r=subprocess.run(['cmd','/d','/c',str(out/'motion.cmd')],capture_output=True,text=True);print(r.stdout+r.stderr);sys.exit(r.returncode)
