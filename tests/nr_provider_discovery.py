"""Execute the real D3D12 availability check with inert filesystem/provider calls."""
from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[1]
out=root/'builds/native-color/provider-discovery'
out.mkdir(parents=True,exist_ok=True)
source=(root/'OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp').read_text()
start=source.index('static bool DirectD3D12AvailableLocked(')
body=source[start:source.index('\n}',start)+2]
header=root/'OptiScaler/dlssnr/NrProviderPathCache.h'
unit='''#include <filesystem>
#include <optional>
#include <string>
#include <cassert>
#include <iostream>
#include <cstdint>
#include <windows.h>
#include <dlssnr/ProviderProbeDiagnostics.h>
#define LOG_WARN(...) ((void)0)
using namespace DlssNr;
''' + ('#include <dlssnr/NrProviderPathCache.h>\n' if header.exists() else '') + '''
using namespace std;
struct ID3D12Device{};bool g_sessionClosed=false,g_shutdownFailed=false;
uint64_t g_lifecycleGeneration=1,resume=1;int searches=0,probes=0,caps=0;
bool found=false,capable=true,forwarder=true;int probeResult=31;
struct Config { static Config* Instance(){static Config c;return &c;}
 struct Snapshot{uint64_t resumeGeneration;};Snapshot GetDlssNrRuntimeSnapshot(){return {resume};} };
struct Nr{void* forwarder=reinterpret_cast<void*>(1);const char* reason="capability failed";}g_nr;
filesystem::path g_dllDir="C:/game", discovered="C:/game/nvngx_dlssnr.dll";
bool EnsureForwarder(){return forwarder;}
bool EnsureCapabilityParams(ID3D12Device*){++caps;return capable;}
int Probe(const wchar_t*){++probes;return probeResult;}
void* GetProcAddress(void*,const char* name){return string(name)=="dlssnr_call_probe_d3d12"?reinterpret_cast<void*>(&Probe):nullptr;}
namespace Util {
 filesystem::path ExePath(){return "C:/game/game.exe";}
 optional<filesystem::path> FindFilePath(const filesystem::path&,const char*){++searches;return found?optional<filesystem::path>{discovered}:nullopt;}
}
''' + ('DlssNr::NrProviderPathCache g_directModelPath;\n' if header.exists() else '') + ('#include <dlssnr/NrProviderProbeCache.h>\nDlssNr::NrProviderProbeCache g_directProbe;\n' if (root/'OptiScaler/dlssnr/NrProviderProbeCache.h').exists() else '') + body + '''
int main(){
 ID3D12Device device;
 const char* reason=nullptr;
 assert(!DirectD3D12AvailableLocked(&device,&reason)&&string(reason).find("nvngx_dlssnr.dll is missing")!=string::npos);
 assert(!DirectD3D12AvailableLocked(&device));int initial=searches;
 for(int i=0;i<100;++i)assert(!DirectD3D12AvailableLocked(&device));
 if(searches!=initial){cerr<<"FAIL missing model repeats filesystem search each frame\\n";return 1;}
 assert(probes==0&&caps==0);
 found=true;++resume;
 assert(DirectD3D12AvailableLocked(&device));int located=searches;
 assert(DirectD3D12AvailableLocked(&device));assert(searches==located&&caps==2);
 for(int i=0;i<100;++i)assert(DirectD3D12AvailableLocked(&device));
 if(probes!=1){cerr<<"FAIL stable Present repeatedly scans provider capability: "<<probes<<" probes for 102 calls\\n";return 1;}
 auto prior=probes;++resume;assert(DirectD3D12AvailableLocked(&device)&&probes==prior+1);
 prior=probes;g_nr.forwarder=reinterpret_cast<void*>(2);assert(DirectD3D12AvailableLocked(&device)&&probes==prior+1);
 ID3D12Device second;prior=probes;assert(DirectD3D12AvailableLocked(&second)&&probes==prior+1);
 ++resume;probeResult=7;prior=probes;
 assert(!DirectD3D12AvailableLocked(&device));assert(!DirectD3D12AvailableLocked(&device)&&probes==prior+2);
 probeResult=31;assert(DirectD3D12AvailableLocked(&device)&&probes==prior+3);
 prior=probes;assert(!DirectD3D12AvailableLocked(nullptr)&&probes==prior);
 g_directProbe.Reset();assert(DirectD3D12AvailableLocked(&device)&&probes==prior+1);
 prior=probes;discovered="C:/game/_storage_/nvngx_dlssnr.dll";g_directModelPath.Reset();
 assert(DirectD3D12AvailableLocked(&device)&&probes==prior+1);
 capable=false;assert(!DirectD3D12AvailableLocked(&device));
 g_nr.reason="";assert(!DirectD3D12AvailableLocked(&device,&reason)&&reason&&reason[0]);
 forwarder=false;assert(!DirectD3D12AvailableLocked(&device,&reason)&&reason&&reason[0]);forwarder=true;
 g_sessionClosed=true;auto before=probes;assert(!DirectD3D12AvailableLocked(&device)&&probes==before);
 g_sessionClosed=false;g_shutdownFailed=true;assert(!DirectD3D12AvailableLocked(&device)&&probes==before);
 g_shutdownFailed=false;capable=true;++g_lifecycleGeneration;prior=probes;
 assert(DirectD3D12AvailableLocked(&device)&&searches>located&&probes==prior+1);
 cout<<"PASS stable capability discovery runs once; resume, path, module, device, manual retry and lifecycle invalidation; failed probes retry; live capability and shutdown gates\\n";
}
'''
(out/'discovery.cpp').write_text(unit)
(out/'run.cmd').write_text(f'''@echo off
call "C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul
if not "%errorlevel%"=="0" exit /b %errorlevel%
cd /d "{root}"
cl /nologo /std:c++20 /EHsc /utf-8 /W3 /I OptiScaler /I external/nlohmann "{out/'discovery.cpp'}" /Fe:"{out/'discovery.exe'}" /Fo:"{out/'discovery.obj'}"
if not "%errorlevel%"=="0" exit /b %errorlevel%
"{out/'discovery.exe'}"
''')
raise SystemExit(subprocess.run(['cmd','/d','/c',str(out/'run.cmd')],cwd=root).returncode)
