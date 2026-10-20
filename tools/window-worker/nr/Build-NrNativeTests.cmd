@echo off
set "NR_BUILD_PATH=%PATH%"
set PATH=
set "Path=%NR_BUILD_PATH%"
"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" "%~dp0NrNativeTests.vcxproj" /p:Configuration=Release /p:Platform=x64 /p:VCToolsVersion=14.44.35207 /p:TrackFileAccess=false /v:minimal /nologo
exit /b %ERRORLEVEL%
