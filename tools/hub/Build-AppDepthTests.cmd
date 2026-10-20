@echo off
set "NR_APP_BUILD_PATH=%PATH%"
set PATH=
set "Path=%NR_APP_BUILD_PATH%"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b %ERRORLEVEL%
cd /d "%~dp0..\.."
if not exist builds\window-worker\guidance\intermediate mkdir builds\window-worker\guidance\intermediate
call tools\depth-anything-v2\Build-Shaders.cmd
if errorlevel 1 exit /b %ERRORLEVEL%
cl /nologo /O2 /EHsc /MD /std:c++20 /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /Iapps\NeuRoticWindowWorker\guidance\vendor /Iexternal\nlohmann /Ibuilds\depth-anything-v2\shaders tests\window-worker\GuidanceTests.cpp apps\NeuRoticWindowWorker\guidance\GuidanceHost.cpp apps\NeuRoticWindowWorker\nr\NrPolicy.cpp apps\NeuRoticWindowWorker\guidance\dav2\Dav2Host.cpp apps\NeuRoticWindowWorker\guidance\dav2\Dav2Module.cpp /Fobuilds\window-worker\guidance\intermediate\ /Febuilds\window-worker\guidance\GuidanceTests.exe /link d3d11.lib d3d12.lib dxgi.lib bcrypt.lib d3dcompiler.lib >builds\window-worker\guidance\build.log 2>&1
exit /b %ERRORLEVEL%
