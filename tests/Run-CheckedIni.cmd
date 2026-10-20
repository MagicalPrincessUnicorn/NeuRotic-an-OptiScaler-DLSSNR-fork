@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if not "%ERRORLEVEL%"=="0" exit /b 1
pushd "%~dp0.."
set "iniOut=builds\ui-release\ini-tests"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests\Prepare-CheckedIniFixture.ps1
if not "%ERRORLEVEL%"=="0" (popd & exit /b 1)
cl /nologo /std:c++20 /EHsc /O2 /W4 /WX /D_CRT_SECURE_NO_WARNINGS /I "%iniOut%" /external:I external\simpleini /external:W0 tests\checked_ini.cpp advapi32.lib /Fe:"%iniOut%\checked_ini.exe" /Fo:"%iniOut%\checked_ini.obj" >"%iniOut%\compile.log" 2>&1
if not "%ERRORLEVEL%"=="0" (type "%iniOut%\compile.log" & popd & exit /b 1)
"%iniOut%\checked_ini.exe" %* >"%iniOut%\test.log" 2>&1
set "iniExit=%ERRORLEVEL%"
type "%iniOut%\test.log"
popd
exit /b %iniExit%
