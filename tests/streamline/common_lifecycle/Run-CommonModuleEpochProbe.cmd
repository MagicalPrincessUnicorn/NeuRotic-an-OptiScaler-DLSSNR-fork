@echo off
setlocal
set "ROOT=%~dp0..\..\.."
set "OUT=%ROOT%\builds\streamline\common-lifecycle"
if not exist "%OUT%" mkdir "%OUT%"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
pushd "%ROOT%"
cl /nologo /std:c++20 /EHsc /W4 /WX /MT /DNOMINMAX /UNDEBUG /Y- /LD /DCOMMON_FIXTURE_TAG=2 tests\streamline\common_lifecycle\FixtureProvider.cpp /Fo"%OUT%\FixtureB.obj" /Fe"%OUT%\NrCommonFixtureB.dll" /link /INCREMENTAL:NO
if errorlevel 1 (popd & exit /b 1)
cl /nologo /std:c++20 /EHsc /W4 /WX /MT /DNOMINMAX /UNDEBUG /Y- /LD /DCOMMON_FIXTURE_TAG=1 tests\streamline\common_lifecycle\FixtureProvider.cpp /Fo"%OUT%\FixtureA.obj" /Fe"%OUT%\NrCommonFixtureA.dll" /link /DEF:tests\streamline\common_lifecycle\FixtureA.def /INCREMENTAL:NO
if errorlevel 1 (popd & exit /b 1)
cl /nologo /std:c++20 /EHsc /W4 /WX /MT /DNOMINMAX /UNDEBUG /Y- tests\streamline\common_lifecycle\module_epoch_probe.cpp /Fo"%OUT%\ModuleEpoch.obj" /Fe"%OUT%\ModuleEpoch.exe" /link /INCREMENTAL:NO
if errorlevel 1 (popd & exit /b 1)
"%OUT%\ModuleEpoch.exe" "%OUT%"
set "RESULT=%ERRORLEVEL%"
popd
exit /b %RESULT%
