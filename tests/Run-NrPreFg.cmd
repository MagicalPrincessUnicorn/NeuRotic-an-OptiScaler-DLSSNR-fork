@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
pushd "%~dp0.."
if not exist "builds\core-runtime-audit\pre-fg" mkdir "builds\core-runtime-audit\pre-fg"
cl /nologo /std:c++20 /EHsc /W4 /I OptiScaler /I OptiScaler\include tests\nr_pre_fg.cpp /Fe:builds\core-runtime-audit\pre-fg\nr_pre_fg_test.exe /Fo:builds\core-runtime-audit\pre-fg\nr_pre_fg_test.obj /link d3d12.lib dxgi.lib
if errorlevel 1 (popd & exit /b 1)
builds\core-runtime-audit\pre-fg\nr_pre_fg_test.exe
if not "%ERRORLEVEL%"=="0" (popd & exit /b 1)
cl /nologo /std:c++20 /EHsc /W4 /I OptiScaler /I OptiScaler\include tests\nr_pre_fg_dxgi.cpp /Fe:builds\core-runtime-audit\pre-fg\nr_pre_fg_dxgi_test.exe /Fo:builds\core-runtime-audit\pre-fg\nr_pre_fg_dxgi_test.obj /link d3d12.lib dxgi.lib user32.lib
if errorlevel 1 (popd & exit /b 1)
builds\core-runtime-audit\pre-fg\nr_pre_fg_dxgi_test.exe
set "testResult=%ERRORLEVEL%"
popd
exit /b %testResult%
