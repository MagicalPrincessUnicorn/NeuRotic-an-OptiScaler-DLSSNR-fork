@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if not "%ERRORLEVEL%"=="0" exit /b 1
set "prepTestOut=%~dp0..\builds\capability-facts\vulkan-device-preparation"
if not exist "%prepTestOut%" mkdir "%prepTestOut%"
pushd "%~dp0.."
cl /nologo /std:c++20 /EHsc /W4 /WX /I OptiScaler /I external\vulkan\include tests\vulkan_device_preparation.cpp /Fe:"%prepTestOut%\device_preparation.exe" /Fo:"%prepTestOut%\device_preparation.obj" >"%prepTestOut%\compile.txt" 2>&1
set "prepCompileExit=%ERRORLEVEL%"
type "%prepTestOut%\compile.txt"
if not "%prepCompileExit%"=="0" (popd & exit /b %prepCompileExit%)
"%prepTestOut%\device_preparation.exe" >"%prepTestOut%\test.txt" 2>&1
set "prepTestExit=%ERRORLEVEL%"
type "%prepTestOut%\test.txt"
popd
exit /b %prepTestExit%
