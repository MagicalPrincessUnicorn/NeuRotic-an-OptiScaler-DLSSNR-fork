@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if not "%ERRORLEVEL%"=="0" exit /b %ERRORLEVEL%
pushd "%~dp0.."
set "markerOut=builds\ui-release\audit-config"
powershell -NoProfile -ExecutionPolicy Bypass -File tests\Prepare-NrExperimentalMarkerFixture.ps1 -OutputDirectory "%markerOut%"
if not "%ERRORLEVEL%"=="0" (popd & exit /b 1)
cl /nologo /std:c++20 /EHsc /O2 /W4 /WX /I "%markerOut%" tests\nr_experimental_marker.cpp /Fe:"%markerOut%\marker.exe" /Fo:"%markerOut%\marker.obj" >"%markerOut%\marker-build.txt" 2>&1
if not "%ERRORLEVEL%"=="0" (type "%markerOut%\marker-build.txt" & popd & exit /b 1)
"%markerOut%\marker.exe" >"%markerOut%\marker-dedicated.txt" 2>&1
set "markerDedicated=%ERRORLEVEL%"
"%markerOut%\marker.exe" global >"%markerOut%\marker-global.txt" 2>&1
set "markerGlobal=%ERRORLEVEL%"
type "%markerOut%\marker-dedicated.txt"
type "%markerOut%\marker-global.txt"
popd
if not "%markerDedicated%"=="0" exit /b %markerDedicated%
exit /b %markerGlobal%
