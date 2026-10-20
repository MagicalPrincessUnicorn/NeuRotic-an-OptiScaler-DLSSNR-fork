@echo off
set "NR_BUILD_PATH=%PATH%"
set PATH=
set "Path=%NR_BUILD_PATH%"
set "NR_MSBUILD=C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe"
"%NR_MSBUILD%" "%~dp0Forwarder.vcxproj" /p:Configuration=Release /p:Platform=x64 /p:VCToolsVersion=14.44.35207 /p:TrackFileAccess=false /v:minimal /nologo
if errorlevel 1 exit /b %ERRORLEVEL%
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Pin-Forwarder.ps1"
if errorlevel 1 exit /b %ERRORLEVEL%
"%NR_MSBUILD%" "%~dp0NrHost.vcxproj" /p:Configuration=Release /p:Platform=x64 /p:VCToolsVersion=14.44.35207 /p:TrackFileAccess=false /v:minimal /nologo
exit /b %ERRORLEVEL%
