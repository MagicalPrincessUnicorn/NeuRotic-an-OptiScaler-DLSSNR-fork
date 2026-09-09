@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set "testOut=C:\OptiScaler-NR-Dev\logs\nr-robustness"
if not exist "%testOut%" mkdir "%testOut%"
if not exist "%testOut%" exit /b 1
pushd "%~dp0.."
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /I OptiScaler /I OptiScaler\include tests\nr_gpu_safety.cpp /Fe:"%testOut%\nr_gpu_safety.exe" /Fo:"%testOut%\nr_gpu_safety.obj" /link d3d12.lib dxgi.lib OptiScaler\library\detours\detours.lib >"%testOut%\gpu-build.txt" 2>&1
if errorlevel 1 goto failed
"%testOut%\nr_gpu_safety.exe" >"%testOut%\gpu-test.txt" 2>&1
if errorlevel 1 goto failed
cl /nologo /std:c++20 /EHsc /W4 /I tests\exposure_scan_stubs tests\nr_exposure_scan.cpp /Fe:"%testOut%\nr_exposure_scan.exe" /Fo:"%testOut%\nr_exposure_scan.obj" >"%testOut%\scan-build.txt" 2>&1
if errorlevel 1 goto failed
"%testOut%\nr_exposure_scan.exe" >"%testOut%\scan-test.txt" 2>&1
if errorlevel 1 goto failed
type "%testOut%\gpu-test.txt"
type "%testOut%\scan-test.txt"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests\Test-NrSecondLayer.ps1 >"%testOut%\second-layer-static.txt" 2>&1
if errorlevel 1 goto failed
type "%testOut%\second-layer-static.txt"
call tests\Run-NrConfig.cmd >"%testOut%\config-suite.txt" 2>&1
if errorlevel 1 goto failed
call tests\Run-NrDispatchResources.cmd >"%testOut%\dispatch-suite.txt" 2>&1
if errorlevel 1 goto failed
call tests\Run-NrReadiness.cmd >"%testOut%\readiness-suite.txt" 2>&1
if errorlevel 1 goto failed
type "%testOut%\config-suite.txt"
type "%testOut%\dispatch-suite.txt"
type "%testOut%\readiness-suite.txt"
popd
exit /b 0
:failed
set "testResult=%ERRORLEVEL%"
echo Robustness tests failed. See logs in "%testOut%".
popd
exit /b %testResult%
