@echo off
setlocal
set "NR_FLOW_ROOT=%~dp0.."
set "NR_FLOW_OUT=%NR_FLOW_ROOT%\builds\native-guides\runtime"
set "NR_FLOW_LIB=%NR_FLOW_ROOT%\builds\universal-guides\flow"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44
if errorlevel 1 exit /b 1
if not exist "%NR_FLOW_OUT%" mkdir "%NR_FLOW_OUT%"
cl /nologo /std:c++20 /EHsc /W4 /WX /MD /O2 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR "%~dp0prepared_flow_ownership.cpp" "%~dp0prepared_flow_runtime_probe.cpp" /Fo"%NR_FLOW_OUT%\\" /Fe"%NR_FLOW_OUT%\prepared_flow_runtime.exe" /link "%NR_FLOW_LIB%\neurotic_prepared_flow.lib" "%NR_FLOW_LIB%\lib\ffx_opticalflow_x64.lib" "%NR_FLOW_LIB%\lib\ffx_backend_dx12_x64.lib" d3d12.lib dxgi.lib d3dcompiler.lib
if errorlevel 1 exit /b 1
"%NR_FLOW_OUT%\prepared_flow_runtime.exe"
exit /b %errorlevel%
