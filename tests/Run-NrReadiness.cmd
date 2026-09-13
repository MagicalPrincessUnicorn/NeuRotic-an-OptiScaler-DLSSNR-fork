@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
pushd "%~dp0.."
cl /nologo /std:c++20 /EHsc /W4 tests\nr_readiness.cpp /Fe:C:\OptiScaler-NR-Dev\logs\nr_readiness_test.exe /Fo:C:\OptiScaler-NR-Dev\logs\nr_readiness_test.obj
if errorlevel 1 (popd & exit /b 1)
C:\OptiScaler-NR-Dev\logs\nr_readiness_test.exe
set "testResult=%ERRORLEVEL%"
if not "%testResult%"=="0" (popd & exit /b %testResult%)
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests\Test-NrPreSrSoftReset.ps1
set "testResult=%ERRORLEVEL%"
popd
exit /b %testResult%
