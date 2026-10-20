@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
cd /d "%~dp0.."
if not exist builds\character-inspector\audit mkdir builds\character-inspector\audit
cl /nologo /std:c++20 /EHsc /W4 /WX /I OptiScaler /I external\nlohmann tests\nr\semantic\character_cohort_tests.cpp /Fe:builds\character-inspector\audit\cohort.exe /Fo:builds\character-inspector\audit\cohort.obj
if errorlevel 1 exit /b 1
builds\character-inspector\audit\cohort.exe builds\character-inspector\audit\fixture
exit /b %ERRORLEVEL%
