@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
set CL=
set _CL_=
set "ROOT=%~dp0.."
set "OUT=%ROOT%\builds\universal-guides\regression"
for /f %%R in ('powershell -NoProfile -Command "[Guid]::NewGuid().ToString('N')"') do set "NR_RUN_ID=%%R"
if not exist "%OUT%" mkdir "%OUT%"
pushd "%OUT%"
set "NRFLAGS=/nologo /std:c++20 /EHsc /W4 /bigobj /Gy /O1 /DNOMINMAX /DNR_GPU_SAFETY_TEST /UNDEBUG /DNR_ORCH_INIT_TESTING /DNR_LAB_WARP_ONLY /I%ROOT%\OptiScaler /I%ROOT%\OptiScaler\include"
cl %NRFLAGS% /c "%ROOT%\OptiScaler\nr\orchestration\StreamCoordinatorKernel.cpp" "%ROOT%\OptiScaler\nr\lifecycle\NativeSessionLifetime.cpp" "%ROOT%\OptiScaler\nr\protocol\NativeRouteAdmission.cpp" "%ROOT%\OptiScaler\dlssnr\NrGpuSafety.cpp"
if errorlevel 1 (popd & exit /b 1)
for %%S in (vertical-slice/native_ngx_call_capture vertical-slice/native_context_chain spectre/native_source_semantics feed/nr_feed_observation_tests) do (
  cl %NRFLAGS% "%ROOT%\tests\nr\%%S.cpp" StreamCoordinatorKernel.obj NativeSessionLifetime.obj NativeRouteAdmission.obj NrGpuSafety.obj /Fe:test.exe /Fo:test.obj /link /OPT:REF /INCREMENTAL:NO /LIBPATH:"C:\NeuRotic\Optiscaler-Stuff\OptiScaler\library\detours" d3d12.lib dxgi.lib d3dcompiler.lib ole32.lib detours.lib /STACK:8388608
  if errorlevel 1 (popd & exit /b 1)
  test.exe %NR_RUN_ID%
  if errorlevel 1 (popd & exit /b 1)
)
cl /nologo /std:c++20 /EHsc /W4 /WX /I"%ROOT%\OptiScaler" /I"C:\NeuRotic\external\vulkan\include" "%ROOT%\tests\vulkan_device_preparation.cpp" /Fe:vulkan_device_preparation.exe /Fo:vulkan_device_preparation.obj
if errorlevel 1 (popd & exit /b 1)
vulkan_device_preparation.exe
set "NR_RESULT=%errorlevel%"
popd
exit /b %NR_RESULT%
