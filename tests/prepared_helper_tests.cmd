@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if not "%errorlevel%"=="0" exit /b 1
set CL=
set _CL_=
set "OUT=%~dp0..\builds\universal-guides\helper\tests"
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /std:c++20 /EHsc /W4 /WX /DNOMINMAX /I"%~dp0..\OptiScaler" "%~dp0prepared_helper_protocol.cpp" /Fe:"%OUT%\prepared_helper.exe" /Fo:"%OUT%\prepared_helper.obj" /link /INCREMENTAL:NO
if not "%errorlevel%"=="0" exit /b 1
"%OUT%\prepared_helper.exe"
if not "%errorlevel%"=="0" exit /b 1
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat" -vcvars_ver=14.44 >nul
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX /DNOMINMAX /I"%~dp0..\OptiScaler" "%~dp0prepared_helper_protocol.cpp" /Fe:"%OUT%\prepared_helper32.exe" /Fo:"%OUT%\prepared_helper32.obj" /link /INCREMENTAL:NO
if not "%errorlevel%"=="0" exit /b 1
"%OUT%\prepared_helper32.exe"
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX /DNOMINMAX /I"%~dp0..\OptiScaler" "%~dp0prepared_helper_client.cpp" /Fe:"%OUT%\prepared_helper_client32.exe" /Fo:"%OUT%\prepared_helper_client32.obj" /link /INCREMENTAL:NO
if not "%errorlevel%"=="0" exit /b 1
"%OUT%\prepared_helper_client32.exe" "%~dp0..\builds\universal-guides\helper\NeuRotic.PreparedHelper.exe"
if errorlevel 1 exit /b 1
call "%~dp0universal_connections\build_helper_gpu.cmd"
if errorlevel 1 exit /b 1
call "%~dp0universal_connections\run_helper_gpu_tests.cmd"
exit /b %errorlevel%
