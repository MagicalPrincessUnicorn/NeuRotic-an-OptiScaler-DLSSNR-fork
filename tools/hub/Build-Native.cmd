@echo off
set "NH_BUILD_PATH=%PATH%"
set PATH=
set "Path=%NH_BUILD_PATH%"
"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" "%~dp0..\..\apps\NeuRoticHub\NeuRoticHub.sln" /m:4 /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v145 /p:VCToolsVersion=14.44.35207 /p:TrackFileAccess=false /v:minimal /nologo
exit /b %ERRORLEVEL%
