@echo off
setlocal
set "ROOT=%~dp0..\..\.."
set "OUT=%ROOT%\builds\streamline\common-lifecycle"
if not exist "%OUT%\NrCommonFixtureA.dll" exit /b 2
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
pushd "%ROOT%"
cl /nologo /std:c++20 /EHsc /W4 /WX /MT /DNOMINMAX /UNDEBUG /Y- /I OptiScaler\include tests\streamline\common_lifecycle\cached_getter_control.cpp /Fo"%OUT%\CachedGetterControl.obj" /Fe"%OUT%\CachedGetterControl.exe" /link /LIBPATH:OptiScaler\library\detours detours.lib /INCREMENTAL:NO
if errorlevel 1 (popd & exit /b 1)
"%OUT%\CachedGetterControl.exe" "%OUT%"
set "RESULT=%ERRORLEVEL%"
popd
exit /b %RESULT%
