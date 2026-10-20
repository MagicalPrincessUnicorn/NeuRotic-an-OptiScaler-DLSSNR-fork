@echo off
call "C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX /O2 /DNRPG_CPU_MAINLINE_TESTING /DNOMINMAX /I OptiScaler /I"C:/NeuRotic/Optiscaler-Stuff/external/FidelityFX-SDK/ffx-api/include/ffx_api" tests/native_fsr_real.cpp OptiScaler/dlssnr/NativeFsr3D3D12.cpp /Fo:builds/universal-guides/tests/ /Fe:builds/universal-guides/tests/native_fsr_real.exe /link d3d12.lib dxgi.lib
if errorlevel 1 exit /b 1
"builds/universal-guides/tests/native_fsr_real.exe" "C:/NeuRotic/universal-guides-experimental/builds/native-framegen/provider/amd_fidelityfx_dx12.dll"
exit /b %errorlevel%
