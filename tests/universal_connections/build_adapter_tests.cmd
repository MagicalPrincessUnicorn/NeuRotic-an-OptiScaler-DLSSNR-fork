@echo off
setlocal EnableDelayedExpansion
set "ROOT=%~dp0..\.."
set "OUT=%ROOT%\builds\universal-guides\adapter-tests"
set "FLOW=%ROOT%\builds\universal-guides\flow"
if not exist "%OUT%" mkdir "%OUT%"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
set CL=
set _CL_=
cl /nologo /MD /std:c++20 /EHsc /DNOMINMAX /DWIN32_LEAN_AND_MEAN /DNRPG_CPU_MAINLINE_TESTING /DSPDLOG_USE_STD_FORMAT /I"C:\NeuRotic\Optiscaler-Stuff\external\spdlog\include" /I"%ROOT%\OptiScaler" /I"C:\NeuRotic\Optiscaler-Stuff\external\FidelityFX-SDK\ffx-api\include\ffx_api" /I"%ROOT%\addons\prepared-guides\src" "%~dp0adapter_capture_tests.cpp" "%ROOT%\OptiScaler\dlssnr\connections\D3D12Connection.cpp" "%ROOT%\OptiScaler\dlssnr\connections\D3D11Connection.cpp" "%ROOT%\OptiScaler\dlssnr\connections\D3D10Connection.cpp" "%ROOT%\OptiScaler\dlssnr\connections\GlConnection.cpp" "%ROOT%\OptiScaler\dlssnr\connections\GlContextIdentity.cpp" "%ROOT%\OptiScaler\dlssnr\NativeGuideProcessor.cpp" "%ROOT%\OptiScaler\dlssnr\NativeFsr3D3D12.cpp" "%ROOT%\addons\prepared-guides\src\CpuMainlineClient.cpp" /Fo"%OUT%\\" /Fe"%OUT%\adapter_capture_tests.exe" /link /INCREMENTAL:NO d3d12.lib d3d11.lib d3d10.lib d3d10_1.lib dxgi.lib d3dcompiler.lib opengl32.lib gdi32.lib user32.lib "%FLOW%\neurotic_prepared_flow.lib" "%FLOW%\lib\ffx_opticalflow_x64.lib" "%FLOW%\lib\ffx_backend_dx12_x64.lib" C:\NeuRotic\external\FidelityFX-SDK\sdk\libs\pix\bin\x64\WinPixEventRuntime.lib
if errorlevel 1 exit /b 1
copy /y C:\NeuRotic\external\FidelityFX-SDK\sdk\libs\pix\bin\x64\WinPixEventRuntime.dll "%OUT%\WinPixEventRuntime.dll" >nul
if not "%~1"=="" (
  "%OUT%\adapter_capture_tests.exe" %*
  exit /b !errorlevel!
)
for %%T in (d3d12 d3d12-d24 d3d12-color-formats d3d11 bgra d16 d24 d32 d3d10 d3d10-d24 d3d10-base opengl) do (
  "%OUT%\adapter_capture_tests.exe" %%T
  if errorlevel 1 exit /b 1
)
exit /b 0
