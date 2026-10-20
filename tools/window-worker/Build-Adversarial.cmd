@echo off
set "NR_AUDIT_PATH=%PATH%"
set PATH=
set "Path=%NR_AUDIT_PATH%"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b %ERRORLEVEL%
cd /d "%~dp0..\.."
if not exist builds\window-worker\audit mkdir builds\window-worker\audit
cl /nologo /std:c++20 /EHsc /O2 /DUNICODE /D_UNICODE /DNOMINMAX /Fo"builds\window-worker\audit\\" /Fe"builds\window-worker\audit\AdversarialMedia.exe" tests\window-worker\AdversarialMedia.cpp apps\NeuRoticWindowWorker\nr\NrPolicy.cpp
if errorlevel 1 exit /b %ERRORLEVEL%
cl /nologo /std:c++20 /EHsc /O2 /DUNICODE /D_UNICODE /DNOMINMAX /DNRW_CAPTURE_TEST /Fo"builds\window-worker\audit\\" /Fe"builds\window-worker\audit\AdversarialCapture.exe" tests\window-worker\AdversarialCapture.cpp apps\NeuRoticWindowWorker\capture\CaptureHost.cpp apps\NeuRoticWindowWorker\capture\ProcessingAdapter.cpp apps\NeuRoticWindowWorker\nr\NrPolicy.cpp /link d3d11.lib d3d12.lib dxgi.lib dcomp.lib shell32.lib ole32.lib dwmapi.lib user32.lib gdi32.lib windowsapp.lib runtimeobject.lib
exit /b %ERRORLEVEL%
