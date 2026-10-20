@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
pushd "%~dp0.."
set "out=builds\prepared-depth\package\helper"
if not exist "%out%" mkdir "%out%"
cl /nologo /std:c++20 /EHsc /W4 /WX tests\prepared_depth_launcher_fixture.cpp /Fe:"%out%\FixtureGame.exe" /Fo:"%out%\FixtureGame.obj"
set "result=%errorlevel%"
popd
exit /b %result%
