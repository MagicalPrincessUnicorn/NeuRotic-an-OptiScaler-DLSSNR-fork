@echo off
setlocal
set "NR_NATIVE_ROOT=%~dp0.."
set "NR_NATIVE_OUT=%NR_NATIVE_ROOT%\builds\native-guides\processor"
set "NR_NATIVE_FLOW=%NR_NATIVE_ROOT%\builds\universal-guides\flow"
if "%~1"=="x86" goto x86
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
if not exist "%NR_NATIVE_OUT%" mkdir "%NR_NATIVE_OUT%"
cl /nologo /std:c++20 /EHsc /W4 /WX /MD /O2 /DNRPG_CPU_MAINLINE_TESTING /DNOMINMAX /DWIN32_LEAN_AND_MEAN /I"%NR_NATIVE_ROOT%\OptiScaler" /I"C:/NeuRotic/Optiscaler-Stuff/external/FidelityFX-SDK/ffx-api/include/ffx_api" "%~dp0native_guide_processor.cpp" "%NR_NATIVE_ROOT%\OptiScaler\dlssnr\NativeGuideProcessor.cpp" "%NR_NATIVE_ROOT%\OptiScaler\dlssnr\NativeFsr3D3D12.cpp" "%NR_NATIVE_ROOT%\addons\prepared-guides\src\CpuMainlineClient.cpp" /Fo"%NR_NATIVE_OUT%\\" /Fe"%NR_NATIVE_OUT%\native_guide_processor.exe" /link "%NR_NATIVE_FLOW%\neurotic_prepared_flow.lib" "%NR_NATIVE_FLOW%\lib\ffx_opticalflow_x64.lib" "%NR_NATIVE_FLOW%\lib\ffx_backend_dx12_x64.lib" "C:\NeuRotic\Optiscaler-Stuff\OptiScaler\library\fsr31\ffx_opticalflow_x64.lib" "C:\NeuRotic\Optiscaler-Stuff\OptiScaler\library\fsr31\ffx_backend_dx11_x64.lib" "C:\NeuRotic\Optiscaler-Stuff\OptiScaler\library\fsr2\ffx_fsr2_api_dx12_x64.lib" d3d12.lib d3d11.lib dxgi.lib d3dcompiler.lib
if errorlevel 1 exit /b 1
"%NR_NATIVE_OUT%\native_guide_processor.exe"
exit /b %errorlevel%
:x86
set "NR_NATIVE_OUT=%NR_NATIVE_OUT%\x86"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat"
if errorlevel 1 exit /b 1
if not exist "%NR_NATIVE_OUT%" mkdir "%NR_NATIVE_OUT%"
cl /nologo /std:c++20 /EHsc /W4 /WX /MD /O2 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /I"%NR_NATIVE_ROOT%\OptiScaler" "%~dp0native_guide_processor.cpp" "%NR_NATIVE_ROOT%\OptiScaler\dlssnr\NativeGuideProcessor.cpp" /Fo"%NR_NATIVE_OUT%\\" /Fe"%NR_NATIVE_OUT%\native_guide_processor.exe"
if errorlevel 1 exit /b 1
"%NR_NATIVE_OUT%\native_guide_processor.exe"
exit /b %errorlevel%
