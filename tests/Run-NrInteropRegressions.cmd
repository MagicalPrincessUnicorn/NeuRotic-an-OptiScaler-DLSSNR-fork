@echo off
setlocal EnableDelayedExpansion
set "ROOT=%~dp0.."
set "OUT=%ROOT%\builds\interop-safety\tests"
set "TASK_PYTHON=python"
if not exist "%OUT%" mkdir "%OUT%"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if not "!ERRORLEVEL!"=="0" exit /b 1
pushd "%ROOT%"
call tests\Run-NrBridgeSafety.cmd
if not "!ERRORLEVEL!"=="0" goto failed
cl /nologo /std:c++20 /EHsc /W4 /I OptiScaler /I OptiScaler\include tests\nr_gpu_safety.cpp /Fe:"%OUT%\gpu-safety.exe" /Fo:"%OUT%\gpu-safety.obj" /link d3d12.lib dxgi.lib OptiScaler\library\detours\detours.lib >"%OUT%\gpu-safety-build.log" 2>&1
if not "!ERRORLEVEL!"=="0" goto failed
"%OUT%\gpu-safety.exe" >"%OUT%\gpu-safety-test.log" 2>&1
if not "!ERRORLEVEL!"=="0" goto failed
type "%OUT%\gpu-safety-test.log"
cl /nologo /std:c++20 /EHsc /W4 /I OptiScaler /I OptiScaler\include tests\nr_native_dx11.cpp /Fe:"%OUT%\native-dx11.exe" /Fo:"%OUT%\native-dx11.obj" /link d3d11.lib d3d12.lib dxgi.lib d3dcompiler.lib OptiScaler\library\detours\detours.lib >"%OUT%\native-dx11-build.log" 2>&1
if not "!ERRORLEVEL!"=="0" goto failed
"%OUT%\native-dx11.exe" >"%OUT%\native-dx11-test.log" 2>&1
if not "!ERRORLEVEL!"=="0" goto failed
type "%OUT%\native-dx11-test.log"
"%TASK_PYTHON%" -B tests\nr_bridge_contract.py >"%OUT%\bridge-contract.log" 2>&1
if not "!ERRORLEVEL!"=="0" goto failed
type "%OUT%\bridge-contract.log"
"%TASK_PYTHON%" -B tests\dx11_integration_contract.py >"%OUT%\dx11-contract.log" 2>&1
if not "!ERRORLEVEL!"=="0" goto failed
type "%OUT%\dx11-contract.log"
call tests\Run-NrBridgeMutations.cmd >"%OUT%\mutations.log" 2>&1
if not "!ERRORLEVEL!"=="0" goto failed
type "%OUT%\mutations.log"
popd
exit /b 0
:failed
set "RESULT=%ERRORLEVEL%"
for %%N in (gpu-safety native-dx11) do if exist "%OUT%\%%N-build.log" type "%OUT%\%%N-build.log"
for %%N in (gpu-safety native-dx11) do if exist "%OUT%\%%N-test.log" type "%OUT%\%%N-test.log"
if exist "%OUT%\bridge-contract.log" type "%OUT%\bridge-contract.log"
if exist "%OUT%\dx11-contract.log" type "%OUT%\dx11-contract.log"
if exist "%OUT%\mutations.log" type "%OUT%\mutations.log"
popd
exit /b %RESULT%
