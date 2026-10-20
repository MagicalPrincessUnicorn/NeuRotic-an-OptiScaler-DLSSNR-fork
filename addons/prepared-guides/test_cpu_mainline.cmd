@echo off
setlocal
set "NRPG_TEST_ROOT=%~dp0..\.."
set "NRPG_TEST_OUT=%NRPG_TEST_ROOT%\builds\prepared-guides\cpu-mainline-test"
set "NRPG_TEST_ARCH=%~1"
if "%NRPG_TEST_ARCH%"=="x86" set "NRPG_TEST_OUT=%NRPG_TEST_OUT%\x86"
if "%NRPG_TEST_ARCH%"=="x86" (call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat") else (call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat")
if errorlevel 1 exit /b 1
if not exist "%NRPG_TEST_OUT%" mkdir "%NRPG_TEST_OUT%"
cl /nologo /std:c++20 /EHsc /W4 /WX /MD /O2 /DNRPG_CPU_MAINLINE_TESTING /DNOMINMAX /DWIN32_LEAN_AND_MEAN /I"%NRPG_TEST_ROOT%\OptiScaler" "%~dp0src\cpu_mainline_test.cpp" "%~dp0src\CpuMainlineClient.cpp" /Fo"%NRPG_TEST_OUT%\\" /Fe"%NRPG_TEST_OUT%\cpu_mainline_test.exe" /link d3d12.lib dxgi.lib kernel32.lib
if errorlevel 1 exit /b 1
"%NRPG_TEST_OUT%\cpu_mainline_test.exe"
exit /b %errorlevel%
