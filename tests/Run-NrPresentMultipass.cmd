@echo off
setlocal EnableDelayedExpansion
set "MP_ROOT=%~dp0.."
set "MP_OUT=%MP_ROOT%\builds\present-multipass-release\tests\focused"
if not exist "%MP_OUT%" mkdir "%MP_OUT%"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if not "!ERRORLEVEL!"=="0" exit /b 1
pushd "%MP_ROOT%"
if not "!ERRORLEVEL!"=="0" exit /b 1
for %%T in (nr_present_multipass nr_present_live_settings nr_experimental_policy nr_pre_fg nr_readiness) do (
    call :cpu %%T
    if not "!ERRORLEVEL!"=="0" goto failed
)
call :warp nr_present_multipass_warp
if not "!ERRORLEVEL!"=="0" goto failed
if defined NR_TEST_PYTHON (
    "%NR_TEST_PYTHON%" tests\run_nr_multipass_creation.py --out-dir "%MP_OUT%\creation" >"%MP_OUT%\creation.log" 2>&1
) else (
    py -3 tests\run_nr_multipass_creation.py --out-dir "%MP_OUT%\creation" >"%MP_OUT%\creation.log" 2>&1
)
if not "!ERRORLEVEL!"=="0" (
    type "%MP_OUT%\creation.log"
    goto failed
)
type "%MP_OUT%\creation.log"
popd
exit /b 0

:cpu
if not exist "%MP_OUT%\%~1" mkdir "%MP_OUT%\%~1"
cl /nologo /std:c++20 /EHsc /W4 /utf-8 /I OptiScaler /I OptiScaler\include tests\%~1.cpp /Fe:"%MP_OUT%\%~1\test.exe" /Fo:"%MP_OUT%\%~1\test.obj" /link d3d12.lib dxgi.lib user32.lib >"%MP_OUT%\%~1\build.log" 2>&1
if not "!ERRORLEVEL!"=="0" (
    type "%MP_OUT%\%~1\build.log"
    exit /b 1
)
"%MP_OUT%\%~1\test.exe" >"%MP_OUT%\%~1\test.log" 2>&1
if not "!ERRORLEVEL!"=="0" (
    type "%MP_OUT%\%~1\test.log"
    exit /b 1
)
type "%MP_OUT%\%~1\test.log"
exit /b 0

:warp
if not exist "%MP_OUT%\%~1" mkdir "%MP_OUT%\%~1"
cl /nologo /std:c++20 /EHsc /W4 /utf-8 /I tests\present_conversion_stubs /I OptiScaler /I OptiScaler\include /I external\magic_enum\include\magic_enum tests\%~1.cpp OptiScaler\shaders\Shader_Dx12.cpp OptiScaler\shaders\format_transfer\FT_Dx12.cpp /Fe:"%MP_OUT%\%~1\test.exe" /Fo:"%MP_OUT%\%~1\\" /link d3d12.lib dxgi.lib d3dcompiler.lib >"%MP_OUT%\%~1\build.log" 2>&1
if not "!ERRORLEVEL!"=="0" (
    type "%MP_OUT%\%~1\build.log"
    exit /b 1
)
"%MP_OUT%\%~1\test.exe" >"%MP_OUT%\%~1\test.log" 2>&1
if not "!ERRORLEVEL!"=="0" (
    type "%MP_OUT%\%~1\test.log"
    exit /b 1
)
type "%MP_OUT%\%~1\test.log"
exit /b 0

:failed
popd
exit /b 1
