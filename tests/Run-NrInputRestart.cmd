@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
set "nrRestartOut=%~dp0..\builds\native-guides\restart"
if not exist "%nrRestartOut%" mkdir "%nrRestartOut%"
pushd "%~dp0.."
cl /nologo /std:c++20 /EHsc /O2 /W4 /WX tests\nr_input_restart.cpp /Fe:"%nrRestartOut%\tests.exe" /Fo:"%nrRestartOut%\tests.obj"
if errorlevel 1 goto failed
"%nrRestartOut%\tests.exe"
if errorlevel 1 goto failed
echo PASS source/transport require restart; fallback permission and return to process selection do not
popd
exit /b 0
:failed
popd
exit /b 1
