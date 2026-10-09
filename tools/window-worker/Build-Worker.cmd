@echo off
where msbuild >nul 2>nul
if errorlevel 1 (echo Run this from a Visual Studio Developer Command Prompt. & exit /b 1)
msbuild "%~dp0..\..\apps\NeuRoticWindowWorker\NeuRoticWindowWorker.vcxproj" /p:Configuration=Release /p:Platform=x64 /p:VCToolsVersion=14.44.35207 /p:TrackFileAccess=false /v:minimal /nologo
exit /b %ERRORLEVEL%
