@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if not "%errorlevel%"=="0" exit /b 1
set CL=
set _CL_=
set "ROOT=%~dp0..\..\.."
set "OUT=%ROOT%\builds\universal-guides\helper\host-tests"
if not defined NR_DEPENDENCIES set "NR_DEPENDENCIES=C:\NeuRotic\Optiscaler-Stuff\external"
if not exist "%OUT%" mkdir "%OUT%"
pushd "%OUT%"
cl /nologo /std:c++20 /EHsc /W4 /DNOMINMAX /MD /I"%ROOT%\OptiScaler" "%ROOT%\tests\window-worker\NrHostTests.cpp" "%ROOT%\apps\NeuRoticWindowWorker\nr\NrPolicy.cpp" /Fe:NrPolicyTests.exe /Fo:.\ /link /INCREMENTAL:NO
if not "%errorlevel%"=="0" (popd & exit /b 1)
NrPolicyTests.exe
if not "%errorlevel%"=="0" (popd & exit /b 1)
cl /nologo /std:c++20 /EHsc /W4 /DNOMINMAX /DUNICODE /D_UNICODE /MD /I"%ROOT%\OptiScaler" /I"%NR_DEPENDENCIES%\nvngx_dlss_sdk" /I"%ROOT%\builds\universal-guides\helper\generated" "%ROOT%\tests\window-worker\NrNativeTests.cpp" "%ROOT%\apps\NeuRoticWindowWorker\nr\NrPolicy.cpp" /Fe:NrNativeTests.exe /Fo:.\ /link /INCREMENTAL:NO d3d11.lib d3d12.lib dxgi.lib d3dcompiler.lib advapi32.lib
if not "%errorlevel%"=="0" (popd & exit /b 1)
NrNativeTests.exe --gpu-only
set "NR_HOST_RESULT=%errorlevel%"
popd
exit /b %NR_HOST_RESULT%
