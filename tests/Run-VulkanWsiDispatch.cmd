@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if not "%ERRORLEVEL%"=="0" exit /b 1
set "wsiTestOut=%~dp0..\builds\capability-facts\vulkan-wsi-dispatch"
if not exist "%wsiTestOut%" mkdir "%wsiTestOut%"
pushd "%~dp0.."
cl /nologo /std:c++20 /EHsc /W4 /WX /I OptiScaler /I external\vulkan\include tests\vulkan_wsi_dispatch.cpp /Fe:"%wsiTestOut%\wsi_dispatch.exe" /Fo:"%wsiTestOut%\wsi_dispatch.obj" >"%wsiTestOut%\compile.txt" 2>&1
set "wsiCompileExit=%ERRORLEVEL%"
type "%wsiTestOut%\compile.txt"
if not "%wsiCompileExit%"=="0" (popd & exit /b %wsiCompileExit%)
"%wsiTestOut%\wsi_dispatch.exe" >"%wsiTestOut%\test.txt" 2>&1
set "wsiTestExit=%ERRORLEVEL%"
type "%wsiTestOut%\test.txt"
popd
exit /b %wsiTestExit%
