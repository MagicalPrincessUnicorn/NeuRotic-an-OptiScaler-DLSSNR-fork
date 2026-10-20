@echo off
set "NRW_SCHED_PATH=%PATH%"
set PATH=
set "Path=%NRW_SCHED_PATH%"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b %ERRORLEVEL%
cd /d "%~dp0..\.."
if not exist builds\window-worker\scheduling\obj mkdir builds\window-worker\scheduling\obj
if not exist builds\window-worker\scheduling\bin mkdir builds\window-worker\scheduling\bin
cl /nologo /std:c++20 /EHsc /O2 /W4 /DUNICODE /D_UNICODE /DNOMINMAX /Fo"builds\window-worker\scheduling\obj\\" /Fe"builds\window-worker\scheduling\bin\SchedulingTests.exe" tests\window-worker\SchedulingTests.cpp /link user32.lib
exit /b %ERRORLEVEL%
