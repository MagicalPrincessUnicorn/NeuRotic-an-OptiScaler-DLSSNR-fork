@echo off
setlocal
set "NR_FG_ROOT=%~dp0..\.."
set "NR_FG_DEPS=C:\NeuRotic\Optiscaler-Stuff"
set "NR_FG_SDK=%NR_FG_DEPS%\external\FidelityFX-SDK\sdk"
set "NR_FG_OUT=%NR_FG_ROOT%\builds\native-framegen\bridge"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
if not exist "%NR_FG_OUT%" mkdir "%NR_FG_OUT%"
cl /nologo /std:c++20 /EHsc /permissive /Zc:strictStrings- /MD /O2 /DNDEBUG /D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR /DNOMINMAX /DWIN32_LEAN_AND_MEAN /DVK_USE_PLATFORM_WIN32_KHR /I"%NR_FG_ROOT%\OptiScaler" /I"%NR_FG_DEPS%\external\vulkan\include" /I"%NR_FG_SDK%\include" /FI"%~dp0SdkDispatch.h" "%~dp0Bridge.cpp" "%~dp0PhysicalSwapchain.cpp" "%~dp0vendor\FrameInterpolationSwapchainVK.cpp" "%~dp0vendor\FrameInterpolationSwapchainVK_Helpers.cpp" "%~dp0vendor\FrameInterpolationSwapchainVK_UiComposition.cpp" "%~dp0vendor\FrameInterpolationSwapchainVK_DebugPacing.cpp" "%NR_FG_SDK%\src\shared\ffx_assert.cpp" /Fo"%NR_FG_OUT%\\" /LD /Fe"%NR_FG_OUT%\NeuRotic.Fsr3.Vulkan.dll" /link "%NR_FG_DEPS%\OptiScaler\library\vulkan\vulkan-1.lib" winmm.lib /DEBUG /PDB:"%NR_FG_OUT%\NeuRotic.Fsr3.Vulkan.pdb" /PDBALTPATH:NeuRotic.Fsr3.Vulkan.pdb
exit /b %errorlevel%
