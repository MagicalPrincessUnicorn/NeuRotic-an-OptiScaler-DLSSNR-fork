@echo off
setlocal
set "ROOT=%~dp0.."
set "OUT=%ROOT%\builds\ui-release\afnr-tests"
set "MODE=%~1"
if "%MODE%"=="" set "MODE=all"
if not "%MODE%"=="stability" if not "%MODE%"=="snapshots" if not "%MODE%"=="formats" if not "%MODE%"=="policy" if not "%MODE%"=="reference" if not "%MODE%"=="inputs" if not "%MODE%"=="owners" if not "%MODE%"=="harness" if not "%MODE%"=="dispatch" if not "%MODE%"=="vectors" if not "%MODE%"=="sources" if not "%MODE%"=="gpu" if not "%MODE%"=="all" exit /b 2
if not exist "%OUT%" mkdir "%OUT%"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
pushd "%ROOT%"
if "%MODE%"=="all" (call :test policy & if errorlevel 1 goto failed) else goto selected
call :test reference
if errorlevel 1 goto failed
call :test vectors
if errorlevel 1 goto failed
call :test inputs
if errorlevel 1 goto failed
call :test sources
if errorlevel 1 goto failed
call :test owners
if errorlevel 1 goto failed
call :test gpu
if errorlevel 1 goto failed
call :test dispatch
if errorlevel 1 goto failed
call :test formats
if errorlevel 1 goto failed
call :test snapshots
if errorlevel 1 goto failed
call :test stability
if errorlevel 1 goto failed
call :test harness
if errorlevel 1 goto failed
popd
exit /b 0
:selected
call :test %MODE%
if errorlevel 1 goto failed
popd
exit /b 0
:failed
popd
exit /b 1
:test
cl /nologo /std:c++20 /EHsc /W4 /WX /I OptiScaler /I OptiScaler\include tests\nr_alternate_frame_%1.cpp /Fe:"%OUT%\%1.exe" /Fo:"%OUT%\%1.obj" /link d3d12.lib dxgi.lib OptiScaler\library\detours\detours.lib >"%OUT%\%1-build.log" 2>&1
if errorlevel 1 (type "%OUT%\%1-build.log" & exit /b 1)
"%OUT%\%1.exe" >"%OUT%\%1-test.log" 2>&1
set "RESULT=%ERRORLEVEL%"
type "%OUT%\%1-test.log"
exit /b %RESULT%
