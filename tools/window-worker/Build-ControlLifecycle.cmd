@echo off
set "NRW_BUILD_PATH=%PATH%"
set PATH=
set "Path=%NRW_BUILD_PATH%"
"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" "%~dp0ControlLifecycle.vcxproj" /m:2 /p:Configuration=Release /p:Platform=x64 /p:VCToolsVersion=14.44.35207 /p:TrackFileAccess=false /v:minimal /nologo
exit /b %ERRORLEVEL%
