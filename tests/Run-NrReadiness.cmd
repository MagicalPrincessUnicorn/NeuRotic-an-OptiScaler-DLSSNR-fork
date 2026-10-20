@echo off
setlocal
set "ROOT=%~dp0.."
set "OUT=%ROOT%\builds\capability-facts\readiness"
if not exist "%OUT%" mkdir "%OUT%"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if not "%ERRORLEVEL%"=="0" exit /b 1
pushd "%ROOT%"
cl /nologo /std:c++20 /EHsc /W4 tests\nr_readiness.cpp /Fe:"%OUT%\test.exe" /Fo:"%OUT%\test.obj"
if not "%ERRORLEVEL%"=="0" (popd & exit /b 1)
"%OUT%\test.exe"
set "testResult=%ERRORLEVEL%"
if not "%testResult%"=="0" (popd & exit /b %testResult%)
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests\Test-NrPreSrSoftReset.ps1
set "testResult=%ERRORLEVEL%"
popd
exit /b %testResult%

