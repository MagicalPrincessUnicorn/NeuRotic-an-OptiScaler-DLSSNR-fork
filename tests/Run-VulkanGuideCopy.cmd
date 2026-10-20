@echo off
setlocal
cd /d "%~dp0.."
if not defined NR_GUIDE_PYTHON set "NR_GUIDE_PYTHON=python"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
"%NR_GUIDE_PYTHON%" tools\Build-VulkanNrShaders.py --verify
if errorlevel 1 exit /b 1
"%NR_GUIDE_PYTHON%" tests\vulkan_guide_shader_contract.py
if errorlevel 1 exit /b 1
"%NR_GUIDE_PYTHON%" tests\vulkan_guide_recording.py
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX /I "." /I "OptiScaler" /I "external\vulkan\include" tests\vulkan_guide_sample_copy.cpp /Fe:"builds\vulkan-experiment\tests\sampled-guides\vulkan_guide_sample_copy.exe" /Fo:"builds/vulkan-experiment/tests/sampled-guides/"
if errorlevel 1 exit /b 1
builds\vulkan-experiment\tests\sampled-guides\vulkan_guide_sample_copy.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX /I "." /I "OptiScaler" /I "external\vulkan\include" builds\vulkan-experiment\tests\sampled-guides\vulkan_guide_recording.cpp /Fe:"builds\vulkan-experiment\tests\sampled-guides\vulkan_guide_recording.exe" /Fo:"builds/vulkan-experiment/tests/sampled-guides/"
if errorlevel 1 exit /b 1
builds\vulkan-experiment\tests\sampled-guides\vulkan_guide_recording.exe
exit /b %errorlevel%
