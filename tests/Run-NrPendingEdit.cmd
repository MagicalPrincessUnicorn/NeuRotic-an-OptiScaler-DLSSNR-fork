@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
set "nrEditOut=%~dp0..\builds\ui-release\pending-edit"
if not exist "%nrEditOut%" mkdir "%nrEditOut%"
pushd "%~dp0.."
cl /nologo /std:c++20 /EHsc /O2 /W4 /WX tests\nr_pending_edit.cpp /Fe:"%nrEditOut%\nr_pending_edit.exe" /Fo:"%nrEditOut%\nr_pending_edit.obj"
set "result=%ERRORLEVEL%"
if not "%result%"=="0" (popd & exit /b %result%)
"%nrEditOut%\nr_pending_edit.exe"
set "result=%ERRORLEVEL%"
popd
exit /b %result%
