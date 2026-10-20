@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
set "NRPG_OUT=%~dp0..\..\builds\prepared-guides"
cl /nologo /std:c++20 /EHsc /W4 /WX /MD /O2 /I"%~dp0external\reshade\include" "%~dp0src\capture_input_test.cpp" /Fo"%NRPG_OUT%\capture_input_test.obj" /Fe"%NRPG_OUT%\capture_input_test.exe"
if errorlevel 1 exit /b 1
"%NRPG_OUT%\capture_input_test.exe"
exit /b %errorlevel%
