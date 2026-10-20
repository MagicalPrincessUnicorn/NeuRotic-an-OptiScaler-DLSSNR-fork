@echo off
setlocal EnableDelayedExpansion
set "ROOT=%~dp0.."
set "OUT=%ROOT%\builds\ui-release\audit-config"
if not exist "%OUT%" mkdir "%OUT%"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if not "!ERRORLEVEL!"=="0" exit /b !ERRORLEVEL!
pushd "%ROOT%"
cl /nologo /std:c++20 /EHsc /O2 /W4 /WX /I OptiScaler tests\nr_depth_barrier_consumer.cpp /Fe:"%OUT%\depth-consumer.exe" /Fo:"%OUT%\depth-consumer.obj"
set "RESULT=!ERRORLEVEL!"
if not "!RESULT!"=="0" (popd & exit /b !RESULT!)
"%OUT%\depth-consumer.exe"
set "RESULT=!ERRORLEVEL!"
if not "!RESULT!"=="0" (popd & exit /b !RESULT!)
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests\Test-NrDepthBarrierConsumers.ps1
set "RESULT=!ERRORLEVEL!"
popd
exit /b !RESULT!
