@echo off
setlocal
cd /d "%~dp0..\.."
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
if not exist builds\hub\simple-loader mkdir builds\hub\simple-loader
rc /nologo /fobuilds\hub\simple-loader\loader.res tests\hub\simple_fixture_loader.rc
if errorlevel 1 exit /b 1
cl /nologo /LD /W4 /WX tests\hub\simple_fixture_loader.cpp builds\hub\simple-loader\loader.res /Fobuilds\hub\simple-loader\loader.obj /Febuilds\hub\simple-loader\OptiScaler.dll /link /NOENTRY /NODEFAULTLIB
if errorlevel 1 exit /b 1
del /q builds\hub\simple-loader\loader.obj builds\hub\simple-loader\loader.res
exit /b 0
