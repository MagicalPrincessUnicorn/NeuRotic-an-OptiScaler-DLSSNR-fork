@echo off
setlocal
set "DAV2_POLICY_PATH=%PATH%"
set PATH=
set "Path=%DAV2_POLICY_PATH%"
cd /d "%~dp0..\.."
"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" tools\window-worker\nr\NrPolicyTests.vcxproj /p:Configuration=Release /p:Platform=x64 /p:VCToolsVersion=14.44.35207 /p:TrackFileAccess=false /v:minimal /nologo
if errorlevel 1 exit /b %ERRORLEVEL%
builds\window-worker\nr\NrPolicyTests.exe
exit /b %ERRORLEVEL%
