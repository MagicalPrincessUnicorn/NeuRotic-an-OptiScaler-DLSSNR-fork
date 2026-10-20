@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
cd /d "%~dp0.."
if not exist builds\inspector-motion\tests mkdir builds\inspector-motion\tests
cl /nologo /std:c++20 /EHsc /O2 /W4 /WX /permissive- /I OptiScaler tests\nr\semantic\character_capture_source_policy_tests.cpp /Fe:builds\inspector-motion\tests\source-policy.exe /Fo:builds\inspector-motion\tests\source-policy.obj >builds\inspector-motion\tests\source-policy-build.txt 2>&1
if errorlevel 1 (type builds\inspector-motion\tests\source-policy-build.txt & exit /b 1)
builds\inspector-motion\tests\source-policy.exe
exit /b %errorlevel%
