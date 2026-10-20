@echo off
setlocal
set "NRPG_ROOT=%~dp0..\.."
set "NRPG_OUT=%NRPG_ROOT%\builds\prepared-guides"
set "NRPG_ARCH=%~1"
if "%NRPG_ARCH%"=="" set "NRPG_ARCH=x64"
if "%NRPG_ARCH%"=="x86" (call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat" -vcvars_ver=14.44) else (call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44)
if not "%errorlevel%"=="0" exit /b %errorlevel%
set CL=
set _CL_=
set "NRPG_EXT=addon64"
set "NRPG_FLOW=%NRPG_ROOT%\builds\universal-guides\flow"
set "NRPG_SOURCES="%~dp0src\CpuMainlineClient.cpp" "%~dp0src\SharedGpuClient.cpp" "%NRPG_ROOT%\OptiScaler\dlssnr\PreparedVkDx12Transport.cpp" "%NRPG_ROOT%\OptiScaler\dlssnr\NativeGuideProcessor.cpp" "%NRPG_ROOT%\OptiScaler\dlssnr\NativeFsr3D3D12.cpp" "%~dp0helper\NativeFsrUnavailable.cpp""
set "NRPG_LIBS=%NRPG_FLOW%\neurotic_prepared_flow.lib %NRPG_FLOW%\lib\ffx_opticalflow_x64.lib %NRPG_FLOW%\lib\ffx_backend_dx12_x64.lib C:\NeuRotic\external\FidelityFX-SDK\sdk\libs\pix\bin\x64\WinPixEventRuntime.lib d3dcompiler.lib"
if "%NRPG_ARCH%"=="x86" (set "NRPG_OUT=%NRPG_OUT%\x86"&set "NRPG_EXT=addon32"&set "NRPG_LIBS="&set "NRPG_SOURCES="%~dp0src\CpuMainlineClient.cpp"")
set "NRPG_SOURCES=%NRPG_SOURCES% "%NRPG_ROOT%\OptiScaler\dlssnr\connections\D3D12Connection.cpp" "%NRPG_ROOT%\OptiScaler\dlssnr\connections\D3D11Connection.cpp" "%NRPG_ROOT%\OptiScaler\dlssnr\connections\D3D10Connection.cpp" "%NRPG_ROOT%\OptiScaler\dlssnr\connections\GlConnection.cpp" "%NRPG_ROOT%\OptiScaler\dlssnr\connections\GlContextIdentity.cpp" "%~dp0helper\PreparedGpuClient.cpp" "%~dp0helper\GpuRemoteConsumer.cpp""
if not exist "%NRPG_OUT%" mkdir "%NRPG_OUT%"
rem Games can load app-local VC runtimes older than the installed redistributable.
rem Match the core's mutex compatibility policy; initialize through that runtime.
cl /nologo /std:c++20 /EHsc /W4 /MD /O2 /D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR /DNOMINMAX /DWIN32_LEAN_AND_MEAN /DSPDLOG_USE_STD_FORMAT /I"C:\NeuRotic\Optiscaler-Stuff\external\spdlog\include" /I"C:\NeuRotic\external\vulkan\include" /I"%NRPG_ROOT%\OptiScaler" /I"C:\NeuRotic\Optiscaler-Stuff\external\FidelityFX-SDK\ffx-api\include\ffx_api" /I"%~dp0external\reshade\include" /LD "%~dp0src\prepared_guides_addon.cpp" %NRPG_SOURCES% /Fo"%NRPG_OUT%\\" /Fe"%NRPG_OUT%\NeuRotic-PreparedGuides.%NRPG_EXT%" /link /MAP:"%NRPG_OUT%\NeuRotic-PreparedGuides.map" d3d12.lib d3d11.lib d3d10.lib d3d10_1.lib dxgi.lib d3dcompiler.lib opengl32.lib user32.lib kernel32.lib advapi32.lib bcrypt.lib %NRPG_LIBS% /INCREMENTAL:NO /IMPLIB:"%NRPG_OUT%\NeuRotic-PreparedGuides.lib"
if not "%errorlevel%"=="0" exit /b %errorlevel%
if "%NRPG_ARCH%"=="x64" copy /y "C:\NeuRotic\external\FidelityFX-SDK\sdk\libs\pix\bin\x64\WinPixEventRuntime.dll" "%NRPG_OUT%\WinPixEventRuntime.dll" >nul
cl /nologo /std:c++20 /EHsc /W4 /MD /O2 "%~dp0src\session_policy_test.cpp" /Fo"%NRPG_OUT%\session_policy_test.obj" /Fe"%NRPG_OUT%\session_policy_test.exe"
if not "%errorlevel%"=="0" exit /b %errorlevel%
"%NRPG_OUT%\session_policy_test.exe"
if not "%errorlevel%"=="0" exit /b %errorlevel%
cl /nologo /std:c++20 /EHsc /W4 /MD /O2 "%~dp0src\capture_codec_test.cpp" /Fo"%NRPG_OUT%\capture_codec_test.obj" /Fe"%NRPG_OUT%\capture_codec_test.exe"
if not "%errorlevel%"=="0" exit /b %errorlevel%
"%NRPG_OUT%\capture_codec_test.exe"
if not "%errorlevel%"=="0" exit /b %errorlevel%
cl /nologo /std:c++20 /EHsc /W4 /WX /MD /O2 /I"%~dp0external\reshade\include" "%~dp0src\depth_binding_test.cpp" /Fo"%NRPG_OUT%\depth_binding_test.obj" /Fe"%NRPG_OUT%\depth_binding_test.exe"
if not "%errorlevel%"=="0" exit /b %errorlevel%
"%NRPG_OUT%\depth_binding_test.exe"
if not "%errorlevel%"=="0" exit /b %errorlevel%
cl /nologo /std:c++20 /EHsc /W4 /WX /MD /O2 /I"%~dp0external\reshade\include" "%~dp0src\capture_input_test.cpp" /Fo"%NRPG_OUT%\capture_input_test.obj" /Fe"%NRPG_OUT%\capture_input_test.exe"
if not "%errorlevel%"=="0" exit /b %errorlevel%
"%NRPG_OUT%\capture_input_test.exe"
if not "%errorlevel%"=="0" exit /b %errorlevel%
set "NRPG_ABI_OBJECTS="%NRPG_OUT%\prepared_guides_addon.obj" "%NRPG_OUT%\CpuMainlineClient.obj" "%NRPG_OUT%\D3D12Connection.obj" "%NRPG_OUT%\D3D11Connection.obj" "%NRPG_OUT%\D3D10Connection.obj" "%NRPG_OUT%\GlConnection.obj" "%NRPG_OUT%\GlContextIdentity.obj" "%NRPG_OUT%\PreparedGpuClient.obj" "%NRPG_OUT%\GpuRemoteConsumer.obj""
if "%NRPG_ARCH%"=="x64" set "NRPG_ABI_OBJECTS=%NRPG_ABI_OBJECTS% "%NRPG_OUT%\SharedGpuClient.obj" "%NRPG_OUT%\PreparedVkDx12Transport.obj" "%NRPG_OUT%\NativeGuideProcessor.obj" "%NRPG_OUT%\NativeFsr3D3D12.obj" "%NRPG_OUT%\NativeFsrUnavailable.obj""
cl /nologo /std:c++20 /EHsc /MD /O2 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /DSPDLOG_USE_STD_FORMAT /I"C:\NeuRotic\Optiscaler-Stuff\external\spdlog\include" /I"%NRPG_ROOT%\OptiScaler" /I"C:\NeuRotic\Optiscaler-Stuff\external\FidelityFX-SDK\ffx-api\include\ffx_api" "%NRPG_ROOT%\tests\universal_connections\addon_abi_tests.cpp" %NRPG_ABI_OBJECTS% /Fo"%NRPG_OUT%\addon_abi_tests.obj" /Fe"%NRPG_OUT%\addon_abi_tests.exe" /link d3d12.lib d3d11.lib d3d10.lib d3d10_1.lib dxgi.lib d3dcompiler.lib opengl32.lib user32.lib kernel32.lib advapi32.lib bcrypt.lib %NRPG_LIBS% /INCREMENTAL:NO
if not "%errorlevel%"=="0" exit /b %errorlevel%
"%NRPG_OUT%\addon_abi_tests.exe"
exit /b %errorlevel%
