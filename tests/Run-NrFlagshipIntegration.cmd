@echo off
setlocal EnableDelayedExpansion
set "FI_ROOT=%~dp0.."
set "FI_OUT=%FI_ROOT%\builds\ui-release\merge-tests"
if not exist "%FI_OUT%" mkdir "%FI_OUT%"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if not "!ERRORLEVEL!"=="0" exit /b 1
pushd "%FI_ROOT%"
if not "!ERRORLEVEL!"=="0" exit /b 1
for %%T in (nr_config nr_dispatch_resources nr_placement_lifecycle nr_advisor_input nr_present_resolution nr_alternate_frame_policy) do (
    call :cpu %%T
    if not "!ERRORLEVEL!"=="0" goto failed
)
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests\Test-NrConfigCoverage.ps1
if not "!ERRORLEVEL!"=="0" goto failed
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests\Test-NeuroticUi.ps1
if not "!ERRORLEVEL!"=="0" goto failed
if defined NR_TEST_PYTHON (
    set "FI_PYTHON=%NR_TEST_PYTHON%"
) else (
    set "FI_PYTHON=python"
)
for %%T in (integration_contract advisor_stage_contract dx11_integration_contract) do (
    "%FI_PYTHON%" tests\%%T.py
    if not "!ERRORLEVEL!"=="0" goto failed
)
popd
exit /b 0

:cpu
if not exist "%FI_OUT%\%~1" mkdir "%FI_OUT%\%~1"
cl /nologo /std:c++20 /EHsc /W4 /utf-8 /I OptiScaler /I OptiScaler\include /I external\simpleini tests\%~1.cpp /Fe:"%FI_OUT%\%~1\test.exe" /Fo:"%FI_OUT%\%~1\test.obj" /link d3d12.lib dxgi.lib user32.lib >"%FI_OUT%\%~1\build.log" 2>&1
if not "!ERRORLEVEL!"=="0" (type "%FI_OUT%\%~1\build.log" & exit /b 1)
"%FI_OUT%\%~1\test.exe" >"%FI_OUT%\%~1\test.log" 2>&1
if not "!ERRORLEVEL!"=="0" (type "%FI_OUT%\%~1\test.log" & exit /b 1)
type "%FI_OUT%\%~1\test.log"
exit /b 0

:failed
popd
exit /b 1
