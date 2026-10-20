@echo off
setlocal
if "%~1"=="" exit /b 2
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if not "%ERRORLEVEL%"=="0" exit /b 1
pushd "%~dp0.."
if not "%ERRORLEVEL%"=="0" exit /b 1
set "OUT=builds\native-mfg\tests"
if not "%~2"=="" set "OUT=%~2"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests\Prepare-NrMfgRuntimeHooks.ps1 -OutputDirectory "%OUT%"
if not "%ERRORLEVEL%"=="0" (popd & exit /b 1)
set "NR_MFG_TEST_FLAGS=/nologo /std:c++20 /EHsc /W4 /WX /MT /UNDEBUG /Zi /utf-8"
cl %NR_MFG_TEST_FLAGS% /DNR_MFG_RUNTIME_TEST /I tests\mfg-runtime-stubs /c OptiScaler\mfg\MfgAdaUnlock.cpp /Fo:"%OUT%\MfgAdaUnlock.obj" /Fd:"%OUT%\compiler.pdb"
if not "%ERRORLEVEL%"=="0" (popd & exit /b 1)
cl %NR_MFG_TEST_FLAGS% /DNR_MFG_RUNTIME_TEST /I external\nvngx_dlss_sdk /c tests\nr_mfg_runtime.cpp /Fo:"%OUT%\nr_mfg_runtime.obj" /Fd:"%OUT%\compiler.pdb"
if not "%ERRORLEVEL%"=="0" (popd & exit /b 1)
ml64 /nologo /c /Fo"%OUT%\MfgCreateForward.obj" OptiScaler\mfg\MfgCreateForward.asm
if not "%ERRORLEVEL%"=="0" (popd & exit /b 1)
link /nologo /DEBUG /INCREMENTAL:NO /PDB:"%OUT%\nr_mfg_runtime.pdb" /OUT:"%OUT%\nr_mfg_runtime.exe" "%OUT%\nr_mfg_runtime.obj" "%OUT%\MfgAdaUnlock.obj" "%OUT%\MfgCreateForward.obj" psapi.lib
if not "%ERRORLEVEL%"=="0" (popd & exit /b 1)
cl %NR_MFG_TEST_FLAGS% /DNR_MFG_RUNTIME_TEST /c tests\nr_mfg_generation.cpp /Fo:"%OUT%\nr_mfg_generation.obj" /Fd:"%OUT%\compiler.pdb"
if not "%ERRORLEVEL%"=="0" (popd & exit /b 1)
link /nologo /DEBUG /INCREMENTAL:NO /PDB:"%OUT%\nr_mfg_generation.pdb" /OUT:"%OUT%\nr_mfg_generation.exe" "%OUT%\nr_mfg_generation.obj" "%OUT%\MfgAdaUnlock.obj" psapi.lib
if not "%ERRORLEVEL%"=="0" (popd & exit /b 1)
cl %NR_MFG_TEST_FLAGS% /DNR_MFG_RUNTIME_TEST /I external\streamline /I "%OUT%" /c tests\nr_mfg_owned_adapter.cpp /Fo:"%OUT%\nr_mfg_owned_adapter.obj" /Fd:"%OUT%\compiler.pdb"
if not "%ERRORLEVEL%"=="0" (popd & exit /b 1)
link /nologo /DEBUG /INCREMENTAL:NO /PDB:"%OUT%\nr_mfg_owned_adapter.pdb" /OUT:"%OUT%\nr_mfg_owned_adapter.exe" "%OUT%\nr_mfg_owned_adapter.obj" "%OUT%\MfgAdaUnlock.obj" psapi.lib d3d12.lib dxgi.lib
if not "%ERRORLEVEL%"=="0" (popd & exit /b 1)
if not exist "%OUT%\public-wrapper" mkdir "%OUT%\public-wrapper"
cl %NR_MFG_TEST_FLAGS% /LD tests\mfg_plugin_fixture.cpp /Fo:"%OUT%\mfg_plugin_fixture.obj" /Fd:"%OUT%\compiler.pdb" /link /DEBUG /INCREMENTAL:NO /PDB:"%OUT%\public-wrapper\sl.dlss_g.pdb" /OUT:"%OUT%\public-wrapper\sl.dlss_g.dll"
if not "%ERRORLEVEL%"=="0" (popd & exit /b 1)
cl %NR_MFG_TEST_FLAGS% /I external\streamline /I external\nlohmann /I "%OUT%" tests\nr_mfg_plugin_startup.cpp /Fo:"%OUT%\nr_mfg_plugin_startup.obj" /Fd:"%OUT%\compiler.pdb" /Fe:"%OUT%\nr_mfg_plugin_startup.exe" /link /DEBUG /INCREMENTAL:NO /PDB:"%OUT%\nr_mfg_plugin_startup.pdb" d3d12.lib dxgi.lib
if not "%ERRORLEVEL%"=="0" (popd & exit /b 1)
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests\Test-NativeMfgRuntime.ps1 -ProviderDirectory "%~1" -TestDirectory "%OUT%"
set "testResult=%ERRORLEVEL%"
popd
exit /b %testResult%
