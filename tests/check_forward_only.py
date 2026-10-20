"""Compile the actual diagnostic DllMain against counted Win32 boundary stubs."""
import pathlib, subprocess, sys

vulkan = '--vulkan' in sys.argv
define = 'NR_DIAG_VULKAN_ONLY' if vulkan else 'NR_DIAG_FORWARD_ONLY'
root = pathlib.Path(__file__).resolve().parents[1]
out = root/('builds/vulkan-experiment/tests/vulkan-only' if vulkan else 'builds/vulkan-experiment/tests/forward-only')
out.mkdir(parents=True, exist_ok=True)
source = (root/'OptiScaler/dllmain.cpp').read_text(encoding='utf-8-sig')
entry = source[source.index('BOOL APIENTRY DllMain('):]
fixture = r'''
#include <cwchar>
#include <string>
#include <cassert>
#include <cstdio>
using HMODULE=void*; using LPVOID=void*; using DWORD=unsigned long; using BOOL=int;
#define APIENTRY
#define TRUE 1
#define FALSE 0
#define MAX_PATH 260
#define DLL_PROCESS_ATTACH 1
#define DLL_PROCESS_DETACH 0
#define DLL_THREAD_ATTACH 2
#define DLL_THREAD_DETACH 3
#define LOAD_LIBRARY_SEARCH_SYSTEM32 0x800
HMODULE self=(void*)1, systemModule=(void*)2, originalModule=nullptr;
std::wstring name=L"C:/fixture/winmm.dll";
int loads=0, binds=0, proxyInits=0, threadDisables=0; bool resolver=true, truncate=false;
bool badSystemPath=false;
DWORD GetSystemDirectoryW(wchar_t* dst, DWORD cap) {
 if(badSystemPath) return cap;
 std::wcscpy(dst,L"C:/Windows/System32"); return (DWORD)std::wcslen(dst);
}
DWORD GetModuleFileNameW(HMODULE, wchar_t* dst, DWORD cap) {
 if(truncate) return cap;
 if(name.empty()) return 0;
 std::wcscpy(dst,name.c_str()); return (DWORD)name.size();
}
BOOL DisableThreadLibraryCalls(HMODULE m) { assert(m==self); ++threadDisables; return TRUE; }
HMODULE LoadLibraryExW(const wchar_t* path, void* file, DWORD flags) {
 assert(std::wcscmp(path,LR"(C:/Windows/System32\winmm.dll)")==0 && !file && flags==LOAD_LIBRARY_SEARCH_SYSTEM32);
 ++loads; return systemModule;
}
struct KernelBaseProxy {
 static void Init() { ++proxyInits; }
 static void* GetProcAddress_() { return resolver ? (void*)3 : nullptr; }
};
int diagnosticStarts=0;
bool StartVulkanHookDiagnostic(HMODULE m) { assert(m==self); ++diagnosticStarts; return true; }
struct Winmm { void LoadOriginalLibrary(HMODULE m) { assert(m==systemModule && m!=self); ++binds; } } winmm;
'''
main = '''
int main() {
 assert(DllMain(self,DLL_PROCESS_ATTACH,nullptr)==TRUE);
 assert(loads==1 && binds==1 && proxyInits==1 && originalModule==systemModule);
 for(auto event:{DLL_PROCESS_DETACH,DLL_THREAD_ATTACH,DLL_THREAD_DETACH,99})
  assert(DllMain(self,event,nullptr)==TRUE);
 assert(loads==1 && binds==1 && proxyInits==1);
 name=L"C:/fixture/dxgi.dll";
 assert(DllMain(self,DLL_PROCESS_ATTACH,nullptr)==FALSE && loads==1);
 name=L"C:/fixture/WINMM.DLL"; systemModule=nullptr;
 assert(DllMain(self,DLL_PROCESS_ATTACH,nullptr)==FALSE && binds==1);
 systemModule=self;
 assert(DllMain(self,DLL_PROCESS_ATTACH,nullptr)==FALSE && binds==1);
 systemModule=(void*)2; resolver=false;
 assert(DllMain(self,DLL_PROCESS_ATTACH,nullptr)==FALSE && binds==1);
 resolver=true; truncate=true;
 assert(DllMain(self,DLL_PROCESS_ATTACH,nullptr)==FALSE && binds==1);
 truncate=false; name=L"";
 assert(DllMain(self,DLL_PROCESS_ATTACH,nullptr)==FALSE && binds==1);
 name=L"C:/fixture/winmm.dll"; badSystemPath=true;
 assert(DllMain(self,DLL_PROCESS_ATTACH,nullptr)==FALSE && binds==1);
 std::puts("PASS: actual forward-only DllMain; system-only forwarding, no hook/config path, failure and detach cases");
}
'''
if vulkan:
    main = main.replace('std::puts(', 'assert(diagnosticStarts==1); std::puts(').replace('forward-only DllMain; system-only forwarding, no hook/config path', 'Vulkan-only DllMain; forwarding and one diagnostic bootstrap, no ordinary startup')
(out/'entry.cpp').write_text(fixture+entry+main)
vcvars=r'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'
(out/'preprocess.cmd').write_text('@echo off\ncall "'+vcvars+'" -vcvars_ver=14.44 >nul\ncl /nologo /EP /D'+define+'=1 entry.cpp > entry.i\nexit /b %errorlevel%\n')
r=subprocess.run(['cmd','/d','/c','preprocess.cmd'],cwd=out,capture_output=True,text=True)
assert r.returncode==0,r.stdout+r.stderr
processed=(out/'entry.i').read_text(); body=processed[processed.index('DllMain('):processed.index('int main()')]
for forbidden in ['Config::','CheckWorkingMode','CheckForExcludedProcess','PrepareLogger','CreateThread','CandidateObserver','Detour','Hooks::']:
 if forbidden in body:
  print('FAIL: diagnostic entry still executes ordinary startup: '+forbidden)
  sys.exit(1)
(out/'compile.cmd').write_text('@echo off\ncall "'+vcvars+'" -vcvars_ver=14.44 >nul\ncl /nologo /std:c++20 /EHsc /O2 /D'+define+'=1 entry.cpp /Fe:entry.exe /Fo:entry.obj /link /INCREMENTAL:NO\nif errorlevel 1 exit /b %errorlevel%\nentry.exe\nexit /b %errorlevel%\n')
r=subprocess.run(['cmd','/d','/c','compile.cmd'],cwd=out,capture_output=True,text=True)
print(r.stdout+r.stderr)
sys.exit(r.returncode)
