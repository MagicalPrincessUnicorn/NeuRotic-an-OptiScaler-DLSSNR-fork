@echo off
set "NRW_CAPTURE_PATH=%PATH%"
set PATH=
set "Path=%NRW_CAPTURE_PATH%"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b %ERRORLEVEL%
cd /d "%~dp0..\..\.."
if not exist builds\window-worker\capture\obj mkdir builds\window-worker\capture\obj
if not exist builds\window-worker\capture\bin mkdir builds\window-worker\capture\bin
cl /nologo /std:c++20 /EHsc /O2 /W4 /DUNICODE /D_UNICODE /DNOMINMAX /DNRW_CAPTURE_TEST /Fo"builds\window-worker\capture\obj\\" /Fe"builds\window-worker\capture\bin\CaptureTests.exe" tests\window-worker\CaptureTests.cpp apps\NeuRoticWindowWorker\capture\CaptureHost.cpp apps\NeuRoticWindowWorker\capture\ProcessingAdapter.cpp /link d3d11.lib d3d12.lib dxgi.lib dcomp.lib dwmapi.lib user32.lib gdi32.lib windowsapp.lib runtimeobject.lib shell32.lib ole32.lib
exit /b %ERRORLEVEL%
