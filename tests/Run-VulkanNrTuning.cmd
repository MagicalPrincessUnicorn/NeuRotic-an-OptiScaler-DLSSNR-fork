@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
set "testOut=%~dp0..\builds\ui-release\vulkan"
if not exist "%testOut%" mkdir "%testOut%"
pushd "%~dp0.."
cl /nologo /std:c++20 /EHsc /W4 /I OptiScaler tests\vulkan_nr_tuning.cpp /Fe:%testOut%\vulkan_nr_tuning_test.exe /Fo:%testOut%\vulkan_nr_tuning_test.obj
if errorlevel 1 (popd & exit /b 1)
%testOut%\vulkan_nr_tuning_test.exe
set "testResult=%ERRORLEVEL%"
popd
exit /b %testResult%
