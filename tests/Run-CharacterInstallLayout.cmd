@echo off
setlocal
cd /d "%~dp0.."
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
if not exist builds\character-inspector\layout mkdir builds\character-inspector\layout
cl /nologo /std:c++20 /EHsc /W4 /WX /I OptiScaler tests\CharacterInstallLayoutTests.cpp /Fobuilds\character-inspector\layout\test.obj /Febuilds\character-inspector\layout\test.exe
if errorlevel 1 exit /b 1
builds\character-inspector\layout\test.exe
if errorlevel 1 exit /b 1
del /q builds\character-inspector\layout\test.obj
exit /b 0
