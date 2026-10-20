@echo off
setlocal
set "ROOT=%~dp0..\..\.."
set "OUT=%ROOT%\builds\streamline\common-lifecycle"
if not exist "%OUT%\NrCommonFixtureA.dll" exit /b 2
if not exist "%OUT%\NrCommonFixtureB.dll" exit /b 2
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
pushd "%ROOT%"
cl /nologo /std:c++20 /EHsc /W4 /WX /MT /DNOMINMAX /UNDEBUG /Y- tests\streamline\common_lifecycle\owned_callback_json.cpp /Fo"%OUT%\OwnedCallbackJson.obj" /Fe"%OUT%\OwnedCallbackJson.exe" /link /INCREMENTAL:NO
if errorlevel 1 (popd & exit /b 1)
"%OUT%\OwnedCallbackJson.exe" "%OUT%"
set "RESULT=%ERRORLEVEL%"
popd
exit /b %RESULT%
