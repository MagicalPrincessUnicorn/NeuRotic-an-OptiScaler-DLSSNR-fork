@echo off
setlocal EnableDelayedExpansion
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
pushd "%~dp0.."
if errorlevel 1 exit /b 1
set "OUT=builds\native-mfg\compat"
if not exist "%OUT%" mkdir "%OUT%"
rem Exercise the existing production color and input owners in this checkout.
for %%t in (nr_hdr_observation nr_present_color nr_present_compatibility native_temporal_inputs) do (
    cl /nologo /std:c++20 /EHsc /W4 /WX /I OptiScaler tests\%%t.cpp /Fe:"%OUT%\%%t.exe" /Fo:"%OUT%\%%t.obj" /link dxgi.lib
    if errorlevel 1 (popd & exit /b 1)
    "%OUT%\%%t.exe"
    if not "!ERRORLEVEL!"=="0" (popd & exit /b 1)
)
cl /nologo /std:c++20 /EHsc /W4 /I OptiScaler /I OptiScaler\include tests\nr_present_guides.cpp /Fe:"%OUT%\nr_present_guides.exe" /Fo:"%OUT%\nr_present_guides.obj" /link d3d12.lib dxgi.lib OptiScaler\library\detours\detours.lib
if errorlevel 1 (popd & exit /b 1)
"%OUT%\nr_present_guides.exe"
if not "!ERRORLEVEL!"=="0" (popd & exit /b 1)
for %%t in (nr_present_color_warp nr_present_composition_warp nr_present_hdr_pipeline_warp) do (
    cl /nologo /std:c++20 /EHsc /I tests\present_conversion_stubs /I OptiScaler /I OptiScaler\include /I external\magic_enum\include\magic_enum tests\%%t.cpp OptiScaler\shaders\Shader_Dx12.cpp OptiScaler\shaders\format_transfer\FT_Dx12.cpp /Fe:"%OUT%\%%t.exe" /Fo:"%OUT%\\" /link d3d12.lib dxgi.lib d3dcompiler.lib
    if errorlevel 1 (popd & exit /b 1)
    "%OUT%\%%t.exe"
    if not "!ERRORLEVEL!"=="0" (popd & exit /b 1)
)
popd
exit /b 0
