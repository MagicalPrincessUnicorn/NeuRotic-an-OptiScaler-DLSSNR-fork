@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
cd /d "%~dp0.."
if not exist builds\inspector-motion\tests mkdir builds\inspector-motion\tests
cl /nologo /std:c++20 /EHsc /O2 /W4 /WX /permissive- /I OptiScaler /I external\nlohmann tests\nr\semantic\character_tracking_wire_tests.cpp /Fe:builds\inspector-motion\tests\tracking-wire.exe /Fo:builds\inspector-motion\tests\tracking-wire.obj >builds\inspector-motion\tests\tracking-wire-build.txt 2>&1
if errorlevel 1 (type builds\inspector-motion\tests\tracking-wire-build.txt & exit /b 1)
builds\inspector-motion\tests\tracking-wire.exe
exit /b %errorlevel%
