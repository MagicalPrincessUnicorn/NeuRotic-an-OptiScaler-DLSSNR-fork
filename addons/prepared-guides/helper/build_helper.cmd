@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if not "%errorlevel%"=="0" exit /b 1
set CL=
set _CL_=
set "ROOT=%~dp0..\..\.."
set "OUT=%ROOT%\builds\universal-guides\helper"
if not defined NR_DEPENDENCIES set "NR_DEPENDENCIES=C:\NeuRotic\Optiscaler-Stuff\external"
if not exist "%OUT%\generated" mkdir "%OUT%\generated"
pushd "%OUT%"
cl /nologo /std:c++20 /EHsc /W4 /DNOMINMAX /DUNICODE /D_UNICODE /MT /LD "%ROOT%\OptiScaler\dlssnr\forwarder\dlssnr_forwarder.cpp" /Fe:nvngx.dll_dlssnr.dll /Fo:forwarder.obj /link /INCREMENTAL:NO /Brepro d3d12.lib advapi32.lib
if not "%errorlevel%"=="0" (popd & exit /b 1)
powershell -NoProfile -Command "$ErrorActionPreference='Stop'; $digest=[System.Security.Cryptography.SHA256]::Create(); try{$hash=[BitConverter]::ToString($digest.ComputeHash([System.IO.File]::ReadAllBytes('nvngx.dll_dlssnr.dll'))).Replace('-','').ToLowerInvariant()}finally{$digest.Dispose()}; [System.IO.File]::WriteAllText([System.IO.Path]::GetFullPath('generated/WindowNrForwarderIdentity.h'), '#pragma once'+[Environment]::NewLine+'inline constexpr char WindowNrForwarderSha256[] = '+[char]34+$hash+[char]34+';'+[Environment]::NewLine)"
if not "%errorlevel%"=="0" (popd & exit /b 1)
cl /nologo /std:c++20 /EHsc /W4 /DNOMINMAX /DUNICODE /D_UNICODE /MT /I"%ROOT%\OptiScaler" /I"%NR_DEPENDENCIES%\nvngx_dlss_sdk" /I"%OUT%\generated" "%~dp0PreparedHelperMain.cpp" "%~dp0DlaaHost.cpp" "%ROOT%\apps\NeuRoticWindowWorker\nr\NrHost.cpp" "%ROOT%\apps\NeuRoticWindowWorker\nr\NrPolicy.cpp" /Fe:NeuRotic.PreparedHelper.exe /Fo:.\ /link /INCREMENTAL:NO d3d11.lib d3d12.lib dxgi.lib d3dcompiler.lib advapi32.lib
set "NR_HELPER_RESULT=%errorlevel%"
popd
if not "%NR_HELPER_RESULT%"=="0" exit /b %NR_HELPER_RESULT%
call "%~dp0build_gpu_helper.cmd"
exit /b %errorlevel%
