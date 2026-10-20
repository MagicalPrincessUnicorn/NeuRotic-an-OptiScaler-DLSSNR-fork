@echo off
setlocal
set "ROOT=%~dp0.."
set "OUT=%ROOT%\builds\character-inspector\native-state"
if not exist "%OUT%" mkdir "%OUT%"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
pushd "%ROOT%"
cl /nologo /std:c++20 /EHsc /W4 /DNOMINMAX /I OptiScaler /I OptiScaler\include /I external\nvngx_dlss_sdk tests\nr\vertical-slice\native_recording_state.cpp /Fe:"%OUT%\test.exe" /Fo:"%OUT%\test.obj" /link d3d12.lib dxgi.lib d3dcompiler.lib >"%OUT%\build.log" 2>&1
if errorlevel 1 (type "%OUT%\build.log" & popd & exit /b 1)
"%OUT%\test.exe" >"%OUT%\test.log" 2>&1
set "RESULT=%ERRORLEVEL%"
type "%OUT%\test.log"
popd
exit /b %RESULT%
