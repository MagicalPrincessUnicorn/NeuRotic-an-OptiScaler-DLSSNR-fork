@echo off
set "NR_BUILD_PATH=%PATH%"
set PATH=
set "Path=%NR_BUILD_PATH%"
"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" "%~dp0NrPolicyTests.vcxproj" /p:Configuration=Release /p:Platform=x64 /p:VCToolsVersion=14.44.35207 /p:TrackFileAccess=false /v:minimal /nologo
if errorlevel 1 exit /b %ERRORLEVEL%
"%~dp0..\..\..\builds\window-worker\nr\NrPolicyTests.exe"
exit /b %ERRORLEVEL%
