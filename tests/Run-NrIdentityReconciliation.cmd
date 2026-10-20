@echo off
setlocal
set "ROOT=%~dp0.."
set "OUT=%ROOT%\builds\moonshot-w03-reconciliation\identity"
if not exist "%OUT%" mkdir "%OUT%"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
pushd "%ROOT%"
cl /nologo /std:c++20 /EHsc /W4 /WX /I"%ROOT%\OptiScaler" tests\nr_identity_reconciliation.cpp /Fe:"%OUT%\test.exe" /Fo:"%OUT%\test.obj" /link d3d12.lib dxgi.lib >"%OUT%\build.txt" 2>&1
set "RC=%ERRORLEVEL%"
if not "%RC%"=="0" goto done
"%OUT%\test.exe" >"%OUT%\test.txt" 2>&1
set "RC=%ERRORLEVEL%"
:done
type "%OUT%\build.txt"
if exist "%OUT%\test.txt" type "%OUT%\test.txt"
popd
exit /b %RC%
