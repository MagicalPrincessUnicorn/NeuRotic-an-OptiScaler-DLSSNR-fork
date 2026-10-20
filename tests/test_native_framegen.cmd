@echo off
call "C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat" >nul
if %errorlevel% neq 0 exit /b 1
set "NR_FG_OUT=builds/universal-guides/tests"
if not exist "%NR_FG_OUT%" mkdir "%NR_FG_OUT%"
cl /nologo /std:c++20 /EHsc /W4 /WX /DVK_NO_PROTOTYPES /DNOMINMAX /I"C:/NeuRotic/Optiscaler-Stuff/external/vulkan/include" tests/native_fg_selection.cpp OptiScaler/dlssnr/NativeFgVulkan.cpp /Fo:"%NR_FG_OUT%/" /Fe:"%NR_FG_OUT%/native_fg_selection.exe"
if errorlevel 1 exit /b 1
for %%S in (0 1 3 4) do (
 "%NR_FG_OUT%/native_fg_selection.exe" %%S 1 0
 if errorlevel 1 exit /b 1
)
"%NR_FG_OUT%/native_fg_selection.exe" 2 1 1
if errorlevel 1 exit /b 1
"%NR_FG_OUT%/native_fg_selection.exe" -1 1 1
if errorlevel 1 exit /b 1
"%NR_FG_OUT%/native_fg_selection.exe" 2 0 0
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX /DNOMINMAX tests/native_fg_history.cpp /Fo:"%NR_FG_OUT%/" /Fe:"%NR_FG_OUT%/native_fg_history.exe"
if %errorlevel% neq 0 exit /b 1
"%NR_FG_OUT%/native_fg_history.exe"
if %errorlevel% neq 0 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX /DNRPG_CPU_MAINLINE_TESTING /DNOMINMAX /I"C:/NeuRotic/Optiscaler-Stuff/external/FidelityFX-SDK/ffx-api/include/ffx_api" tests/native_fsr3.cpp OptiScaler/dlssnr/NativeFsr3D3D12.cpp /Fo:"%NR_FG_OUT%/" /Fe:"%NR_FG_OUT%/native_fsr3.exe" /link d3d12.lib dxgi.lib
if %errorlevel% neq 0 exit /b 1
"%NR_FG_OUT%/native_fsr3.exe"
if %errorlevel% neq 0 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX /DNRPG_CPU_MAINLINE_TESTING /DVK_NO_PROTOTYPES /DNOMINMAX /I"C:/NeuRotic/Optiscaler-Stuff/external/vulkan/include" tests/native_fg_wsi.cpp OptiScaler/dlssnr/NativeFgVulkan.cpp /Fo:"%NR_FG_OUT%/" /Fe:"%NR_FG_OUT%/native_fg_wsi.exe"
if %errorlevel% neq 0 exit /b 1
"%NR_FG_OUT%/native_fg_wsi.exe"
if %errorlevel% neq 0 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX /DNOMINMAX /I"C:/NeuRotic/Optiscaler-Stuff/external/vulkan/include" tests/native_fg_layouts.cpp /Fo:"%NR_FG_OUT%/" /Fe:"%NR_FG_OUT%/native_fg_layouts.exe"
if %errorlevel% neq 0 exit /b 1
"%NR_FG_OUT%/native_fg_layouts.exe"
if %errorlevel% neq 0 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX /DNOMINMAX /I"C:/NeuRotic/Optiscaler-Stuff/external/vulkan/include" tests/native_fg_failure.cpp /Fo:"%NR_FG_OUT%/" /Fe:"%NR_FG_OUT%/native_fg_failure.exe"
if %errorlevel% neq 0 exit /b 1
"%NR_FG_OUT%/native_fg_failure.exe"
exit /b %errorlevel%
