@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /std:c++20 /EHsc /DNR_D3D11_OBSERVER_TEST /I OptiScaler /I OptiScaler/include /I external tests/universal_connections/native_d3d11_observer.cpp OptiScaler/dlssnr/NativeD3D11Guides.cpp OptiScaler/dlssnr/PreparedGuideStatusV2.cpp OptiScaler/dlssnr/connections/ConnectionPolicy.cpp /Fobuilds/universal-connections/policy/ /Febuilds/universal-connections/policy/observer.exe /link OptiScaler/library/detours/detours.lib d3d11.lib d3dcompiler.lib dxgi.lib
if errorlevel 1 exit /b %errorlevel%
builds\universal-connections\policy\observer.exe
