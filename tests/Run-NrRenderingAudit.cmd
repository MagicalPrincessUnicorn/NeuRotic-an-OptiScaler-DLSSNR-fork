@echo off
setlocal EnableDelayedExpansion
set "ROOT=%~dp0.."
set "OUT=%ROOT%\builds\ui-release\audit-rendering"
if not exist "%OUT%" mkdir "%OUT%"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if not "!ERRORLEVEL!"=="0" exit /b 1
pushd "%ROOT%"
set "RESULT=0"
for %%T in (nr_ui_reader_cancellation nr_prepared_depth nr_depth_arrival_snapshot nr_depth_barrier_consumer) do (
 cl /nologo /std:c++20 /EHsc /W4 /I OptiScaler /I OptiScaler\include tests\%%T.cpp /Fe:"%OUT%\%%T.exe" /Fo:"%OUT%\%%T.obj" /link d3d12.lib dxgi.lib OptiScaler\library\detours\detours.lib >"%OUT%\%%T-build.log" 2>&1
 if not "!ERRORLEVEL!"=="0" (type "%OUT%\%%T-build.log" & set "RESULT=1") else (
  "%OUT%\%%T.exe" >"%OUT%\%%T-test.log" 2>&1
  if not "!ERRORLEVEL!"=="0" set "RESULT=1"
  type "%OUT%\%%T-test.log"
 )
)
powershell -NoProfile -ExecutionPolicy Bypass -File tests\Test-NrDepthBarrierConsumers.ps1
if not "!ERRORLEVEL!"=="0" set "RESULT=1"
popd
exit /b %RESULT%
