@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
cd /d "%~dp0.."
if not exist builds\inspector-motion\object-rules mkdir builds\inspector-motion\object-rules
cl /nologo /std:c++20 /EHsc /W4 /WX /utf-8 /I OptiScaler /I external\nlohmann tests\nr\semantic\object_rules_tests.cpp /Fe:builds\inspector-motion\object-rules\core.exe /Fo:builds\inspector-motion\object-rules\core.obj
if errorlevel 1 exit /b 1
builds\inspector-motion\object-rules\core.exe
exit /b %errorlevel%
