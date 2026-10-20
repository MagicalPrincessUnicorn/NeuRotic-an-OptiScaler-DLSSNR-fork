@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
cl /nologo /std:c++20 /EHsc tests\universal_connections\shared_format_probe.cpp /Fo"builds\universal-guides\adapter-tests\shared_format_probe.obj" /Fe"builds\universal-guides\adapter-tests\shared_format_probe.exe" /link d3d11.lib d3d10.lib dxgi.lib
if errorlevel 1 exit /b 1
builds\universal-guides\adapter-tests\shared_format_probe.exe
