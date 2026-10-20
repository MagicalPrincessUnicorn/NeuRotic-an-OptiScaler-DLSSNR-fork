@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if not "%ERRORLEVEL%"=="0" exit /b 1
pushd "%~dp0.."
set "capOut=builds\capability-facts\tests"
if not exist "%capOut%" mkdir "%capOut%"
cl /nologo /std:c++20 /EHsc /O2 /W4 /WX tests\nr_capability_refresh.cpp /Fe:"%capOut%\nr_capability_refresh.exe" /Fo:"%capOut%\nr_capability_refresh.obj" >"%capOut%\refresh-compile.log" 2>&1
if not "%ERRORLEVEL%"=="0" (type "%capOut%\refresh-compile.log" & popd & exit /b 1)
"%capOut%\nr_capability_refresh.exe"
set "capExit=%ERRORLEVEL%"
popd
exit /b %capExit%
