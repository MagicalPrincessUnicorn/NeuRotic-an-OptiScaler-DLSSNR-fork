@echo off
if "%~1"=="" (
  echo Usage: build_w03_lab.cmd C:\NeuRotic\builds\w03-lab-build
  exit /b 2
)
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul
if errorlevel 1 exit /b %errorlevel%
set VSTEL_EWDK=1
set CL=
set _CL_=
if not exist "%~1" mkdir "%~1"
cl /nologo /std:c++20 /EHsc /W4 /WX /Y- "%~dp0w03_lab_host.cpp" /Fe:"%~1\w03_lab_host.exe" /Fo:"%~1\w03_lab_host.obj" /link d3d12.lib dxgi.lib ole32.lib
exit /b %errorlevel%
