@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
set "NR_CAPTURE_TEST=%~dp0..\builds\universal-guides\tests\captured-guides"
if not exist "%NR_CAPTURE_TEST%" mkdir "%NR_CAPTURE_TEST%"
cl /nologo /std:c++20 /EHsc /W4 /WX /I"%~dp0..\OptiScaler" "%~dp0captured_guide_contract.cpp" /Fo"%NR_CAPTURE_TEST%\contract.obj" /Fe"%NR_CAPTURE_TEST%\contract.exe"
if errorlevel 1 exit /b 1
"%NR_CAPTURE_TEST%\contract.exe"
exit /b %errorlevel%
