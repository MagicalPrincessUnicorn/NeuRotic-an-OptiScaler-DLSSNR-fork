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
cl /nologo /std:c++20 /EHsc /W4 /WX tests\nr_exposure_guard.cpp /Fe:"%testOut%\nr_exposure_guard.exe" /Fo:"%testOut%\nr_exposure_guard.obj" >"%testOut%\exposure-guard-build.txt" 2>&1
if errorlevel 1 goto failed
"%testOut%\nr_exposure_guard.exe" >"%testOut%\exposure-guard-test.txt" 2>&1
if errorlevel 1 goto failed
type "%testOut%\gpu-test.txt"
type "%testOut%\scan-test.txt"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests\Test-NrSecondLayer.ps1 >"%testOut%\second-layer-static.txt" 2>&1
if errorlevel 1 goto failed
type "%testOut%\second-layer-static.txt"
type "%testOut%\exposure-guard-test.txt"
cl /nologo /std:c++20 /EHsc /W4 /WX tests\nr_present_pacing.cpp /Fe:"%testOut%\nr_present_pacing.exe" /Fo:"%testOut%\nr_present_pacing.obj" >"%testOut%\pacing-build.txt" 2>&1
if errorlevel 1 goto failed
"%testOut%\nr_present_pacing.exe" >"%testOut%\pacing-test.txt" 2>&1
if errorlevel 1 goto failed
type "%testOut%\pacing-test.txt"
cl /nologo /std:c++20 /EHsc /W4 /WX /I OptiScaler tests\nr_toggle_burst.cpp /Fe:"%testOut%\nr_toggle_burst.exe" /Fo:"%testOut%\nr_toggle_burst.obj" >"%testOut%\toggle-burst-build.txt" 2>&1
if errorlevel 1 goto failed
"%testOut%\nr_toggle_burst.exe" >"%testOut%\toggle-burst-test.txt" 2>&1
if errorlevel 1 goto failed
type "%testOut%\toggle-burst-test.txt"
cl /nologo /std:c++20 /EHsc /W4 /WX /I OptiScaler tests\nr_present_compatibility.cpp /Fe:"%testOut%\nr_present_compatibility.exe" /Fo:"%testOut%\nr_present_compatibility.obj" >"%testOut%\present-compatibility-build.txt" 2>&1
if errorlevel 1 goto failed
"%testOut%\nr_present_compatibility.exe" >"%testOut%\present-compatibility-test.txt" 2>&1
if errorlevel 1 goto failed
type "%testOut%\present-compatibility-test.txt"
cl /nologo /std:c++20 /EHsc /W4 /WX /I OptiScaler tests\nr_bridge_telemetry.cpp /Fe:"%testOut%\nr_bridge_telemetry.exe" /Fo:"%testOut%\nr_bridge_telemetry.obj" >"%testOut%\bridge-build.txt" 2>&1
if errorlevel 1 goto failed
"%testOut%\nr_bridge_telemetry.exe" >"%testOut%\bridge-test.txt" 2>&1
if errorlevel 1 goto failed
type "%testOut%\bridge-test.txt"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests\Test-NrCrossGameDiagnostics.ps1 >"%testOut%\cross-game-static.txt" 2>&1
if errorlevel 1 goto failed
type "%testOut%\cross-game-static.txt"
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
