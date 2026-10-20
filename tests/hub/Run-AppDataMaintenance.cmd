@echo off
set "NR_MAINT_PATH=%PATH%"
set PATH=
set "Path=%NR_MAINT_PATH%"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b %ERRORLEVEL%
cd /d "%~dp0..\.."
if not exist builds\hub\maintenance-tests\obj mkdir builds\hub\maintenance-tests\obj
cl /nologo /std:c++20 /EHsc /O2 /W4 /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /Iapps/NeuRoticHub /Fobuilds/hub/maintenance-tests/obj/ /Febuilds/hub/maintenance-tests/AppDataMaintenanceTests.exe tests/hub/AppDataMaintenanceTests.cpp
if errorlevel 1 exit /b %ERRORLEVEL%
builds\hub\maintenance-tests\AppDataMaintenanceTests.exe
exit /b %ERRORLEVEL%
