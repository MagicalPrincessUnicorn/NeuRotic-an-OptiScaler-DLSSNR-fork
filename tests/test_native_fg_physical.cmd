@echo off
call "C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set "NR_FG_OUT=builds/universal-guides/tests"
if not exist "%NR_FG_OUT%" mkdir "%NR_FG_OUT%"
cl /nologo /std:c++20 /EHsc /W4 /WX /DNOMINMAX /I"C:/NeuRotic/Optiscaler-Stuff/external/vulkan/include" tests/native_fg_physical.cpp addons/native-framegen/PhysicalSwapchain.cpp /Fo:"%NR_FG_OUT%/" /Fe:"%NR_FG_OUT%/native_fg_physical.exe"
if errorlevel 1 exit /b 1
"%NR_FG_OUT%/native_fg_physical.exe"
exit /b %errorlevel%
