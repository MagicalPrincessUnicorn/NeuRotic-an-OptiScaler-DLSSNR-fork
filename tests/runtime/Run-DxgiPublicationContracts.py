"""Execute the production factory-hook publication transaction with real Detours."""
from pathlib import Path
import subprocess
ROOT = Path(__file__).resolve().parents[2]
WORK = ROOT / 'builds/startup-probe/dxgi-contracts'
WORK.mkdir(parents=True, exist_ok=True)
source = (ROOT / 'OptiScaler/hooks/Dxgi_Hooks.cpp').read_text()
state = source[source.index('static std::atomic<DxgiProxy::PFN_CreateDxgiFactory>'):source.index('static bool creatingD3D12DeviceForLuma')]
transaction = source[source.index('    if (o_CreateDXGIFactory.load', source.index('void DxgiHooks::Hook()')):]
preamble = r'''#include <windows.h>
#include <detours.h>
#include <atomic>
#include <mutex>
#include <cstdio>
using Fn=int(*)();
HMODULE dllModule;
enum class WorkingMode { Dxgi };
struct State { WorkingMode workingMode=WorkingMode::Dxgi; static State& Instance(){static State s;return s;} };
struct DxgiProxy {using PFN_CreateDxgiFactory=Fn;using PFN_CreateDxgiFactory1=Fn;using PFN_CreateDxgiFactory2=Fn;static HMODULE Module(){return dllModule;}};
bool missing=true;
FARPROC WINAPI Resolve(HMODULE m,LPCSTR n){return missing && !strcmp(n,"CreateDXGIFactory2") ? nullptr : GetProcAddress(m,n);}
struct KernelBaseProxy{static auto GetProcAddress_(){return Resolve;}};
#define LOG_ERROR(...) ((void)0)
'''
hooks = r'''
int hkCreateDXGIFactory(){return ReadFactoryDispatch().create()+100;}
int hkCreateDXGIFactory1(){return ReadFactoryDispatch().create1()+100;}
int hkCreateDXGIFactory2(){return ReadFactoryDispatch().create2()+100;}
void Install(){std::lock_guard<std::mutex> lock(hookMutex);
'''
main = r'''
int main(){
 dllModule=LoadLibraryW(L"factory.dll");if(!dllModule)return 2;
 auto f0=reinterpret_cast<Fn>(GetProcAddress(dllModule,"CreateDXGIFactory"));
 auto f1=reinterpret_cast<Fn>(GetProcAddress(dllModule,"CreateDXGIFactory1"));
 auto f2=reinterpret_cast<Fn>(GetProcAddress(dllModule,"CreateDXGIFactory2"));
 Install();
 if(ReadFactoryDispatch().create || f0()!=11 || f1()!=22 || f2()!=33)return 3;
 puts("PASS failed preparation leaves all exports and originals unchanged");
 missing=false; Install();
 if(f0()!=111 || f1()!=122 || f2()!=133)return 4;
 puts("PASS all three active hooks call their published original trampolines");
 auto d=ReadFactoryDispatch();if(d.create()!=11||d.create1()!=22||d.create2()!=33)return 5;
 puts("PASS prepared originals bypass interception");
 Install();if(f0()!=111||f1()!=122||f2()!=133)return 6;
 puts("PASS repeated hook setup remains idempotent");
 return 0;
}
'''
(WORK/'publication.cpp').write_text(preamble+state+hooks+transaction+main)
(WORK/'factory.cpp').write_text('extern "C" __declspec(dllexport) int CreateDXGIFactory(){return 11;}\nextern "C" __declspec(dllexport) int CreateDXGIFactory1(){return 22;}\nextern "C" __declspec(dllexport) int CreateDXGIFactory2(){return 33;}\n')
(WORK/'run.cmd').write_text(r'''@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
cl /nologo /LD /MD factory.cpp /Fe:factory.dll /Fo:factory.obj
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX /MD /I"C:\NeuRotic\Optiscaler-Stuff\OptiScaler\include\detours" publication.cpp /Fe:publication.exe /Fo:publication.obj /link "C:\NeuRotic\Optiscaler-Stuff\OptiScaler\library\detours\detours.lib"
if errorlevel 1 exit /b 1
publication.exe
exit /b %errorlevel%
''')
raise SystemExit(subprocess.run(['cmd.exe','/d','/c','run.cmd'],cwd=WORK).returncode)
