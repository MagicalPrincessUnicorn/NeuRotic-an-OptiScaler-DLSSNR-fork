"""Compile real proxy class bodies against Windows and the existing Detours lib."""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
WORK = ROOT / 'builds/startup-probe/contracts'
WORK.mkdir(parents=True, exist_ok=True)
classes = []
for name in ('KernelBase_Proxy.h', 'Ntdll_Proxy.h'):
    text = (ROOT / 'OptiScaler/proxies' / name).read_text()
    classes.append('\n'.join(line for line in text.splitlines() if not line.startswith(('#include', '#pragma once'))))
source = (Path(__file__).with_name('loader_proxy_tests.cpp.in')).read_text().replace('// PRODUCTION_PROXY_CLASSES', '\n'.join(classes))
(WORK / 'loader-contracts.cpp').write_text(source)
(WORK / 'shadow.cpp').write_text('#include <Windows.h>\nBOOL WINAPI DllMain(HINSTANCE,DWORD,LPVOID){return TRUE;}\n')
command = r'''@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
cl /nologo /LD /MD shadow.cpp /Fe:version.dll /Fo:shadow.obj
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX /MD /DNOMINMAX /DUNICODE /D_UNICODE /I"C:\NeuRotic\Optiscaler-Stuff\OptiScaler\include\detours" loader-contracts.cpp /Fe:loader-contracts.exe /Fo:loader-contracts.obj /link "C:\NeuRotic\Optiscaler-Stuff\OptiScaler\library\detours\detours.lib"
if errorlevel 1 exit /b 1
loader-contracts.exe
exit /b %errorlevel%
'''
(WORK / 'run.cmd').write_text(command)
raise SystemExit(subprocess.run(['cmd.exe', '/d', '/c', 'run.cmd'], cwd=WORK).returncode)
