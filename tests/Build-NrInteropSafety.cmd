@echo off
setlocal
set "ROOT=%~dp0.."
set "OUT=%ROOT%\builds\interop-safety\release"
if not exist "%OUT%" mkdir "%OUT%"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
pushd "%ROOT%"
msbuild OptiScaler\OptiScaler.vcxproj /m:2 /t:Rebuild /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v145 /p:VCToolsVersion=14.44.35207 /p:SolutionDir="%CD%/" /p:OutDir="%CD%/builds/interop-safety/release/out/" /p:IntDir="%CD%/builds/interop-safety/release/obj/" /p:PostBuildEventUseInBuild=false /p:TrackFileAccess=false /p:MinimalRebuildFromTracking=false /v:minimal /fl /flp:logfile="%OUT%\build.log";verbosity=normal >"%OUT%\console.log" 2>&1
set "RESULT=%ERRORLEVEL%"
type "%OUT%\console.log"
popd
exit /b %RESULT%
