@echo off
setlocal
cd /d "%~dp0..\.."
if "%NR_DEPTH_TEST_PYTHON%"=="" set "NR_DEPTH_TEST_PYTHON=python"
for %%T in (
 builds\depth-anything-v2\contracts\DepthContractTests.exe
 builds\depth-anything-v2\module\ModuleTests.exe
 builds\depth-anything-v2\gpu\GpuTests.exe
 builds\depth-anything-v2\preferences\PreferenceTests.exe
 builds\depth-anything-v2\app\AppDepthTests.exe
 builds\window-worker\control\bin\ControlTests.exe
 builds\window-worker\guidance\GuidanceTests.exe
 builds\window-worker\nr\NrPolicyTests.exe
 builds\window-worker\scheduling\bin\SchedulingTests.exe
 builds\hub-anything\ui\AnythingUiTests.exe
) do (
 echo TEST %%T
 %%T
 if errorlevel 1 exit /b 1
)
builds\hub-anything\AnythingTests.exe builds\hub-anything\AnythingProtocolFixture.exe builds\hub-anything\fixtures
if errorlevel 1 exit /b 1
builds\window-worker\nr\NrNativeTests.exe --warp-gpu-only
if errorlevel 1 exit /b 1
for %%T in (tests\window-worker\depth-anything-v2\CacheToolTests.py tests\window-worker\ProtocolTests.py tests\window-worker\depth-anything-v2\PreviewStatusTests.py) do (
 echo TEST %%T
 "%NR_DEPTH_TEST_PYTHON%" -B %%T
 if errorlevel 1 exit /b 1
)
if not "%~1"=="" (
 "%NR_DEPTH_TEST_PYTHON%" -B tests\window-worker\ControlLifecycleTests.py "%~1"
 if errorlevel 1 exit /b 1
) else echo SKIP legacy control lifecycle: supply the canonical model file as argument; no real NR execution.
echo PASS available source regression suite; NVIDIA runtime qualification remains separate.
exit /b 0
