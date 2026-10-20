@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if not "%ERRORLEVEL%"=="0" exit /b 1
pushd "%~dp0.."
set "OUT=builds\native-mfg\tests"
if not exist "%OUT%" mkdir "%OUT%"
ml64 /nologo /c /Fo"%OUT%\mfg_create_fixture_asm.obj" tests\mfg_create_fixture.asm
if not "%ERRORLEVEL%"=="0" (popd & exit /b 1)
cl /nologo /std:c++20 /EHsc /W4 /WX /LD tests\mfg_create_fixture.cpp "%OUT%\mfg_create_fixture_asm.obj" /Fe:"%OUT%\mfg_create_fixture.dll" /Fo:"%OUT%\mfg_create_fixture.obj" /link /EXPORT:CreateFixture /EXPORT:CreateRaceFixture /EXPORT:CreateLateRaceFixture /EXPORT:ForeignFixture
if not "%ERRORLEVEL%"=="0" (popd & exit /b 1)
cl /nologo /std:c++20 /EHsc /W4 /WX /DNR_MFG_ENTRY_TEST /I external\nvngx_dlss_sdk tests\nr_mfg_create_entry.cpp /Fe:"%OUT%\nr_mfg_create_entry.exe" /Fo:"%OUT%\nr_mfg_create_entry.obj"
if not "%ERRORLEVEL%"=="0" (popd & exit /b 1)
"%OUT%\nr_mfg_create_entry.exe" "%CD%\%OUT%\mfg_create_fixture.dll"
set "result=%ERRORLEVEL%"
popd
exit /b %result%
