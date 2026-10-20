@echo off
setlocal
cd /d "%~dp0..\.."
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
set CL=
set _CL_=
powershell -NoProfile -ExecutionPolicy Bypass -File tools\experimental\Prepare-InputDependencies.ps1
if errorlevel 1 exit /b 1
powershell -NoProfile -ExecutionPolicy Bypass -File tools\Prepare-UniversalGuidesBuild.ps1 -BuildLabel UniversalInput-Experimental
if errorlevel 1 exit /b 1
powershell -NoProfile -Command "$p='builds/universal-guides/dependencies.targets'; $d=[xml](Get-Content -LiteralPath $p); $d.Project.PropertyGroup.IncludePath=$d.Project.PropertyGroup.IncludePath.Replace('C:/NeuRotic/Optiscaler-Stuff/external','C:/NeuRotic/experimental-lab/external'); $d.Save((Join-Path (Get-Location) $p))"
if errorlevel 1 exit /b 1
msbuild OptiScaler\OptiScaler.vcxproj /m:2 /nologo /v:minimal /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v145 /p:VCToolsVersion=14.44.35207 /p:SolutionDir=C:/NeuRotic/experimental-lab/ /p:IntDir=C:/NeuRotic/experimental-lab/builds/universal-input/runtime/obj/ /p:OutDir=C:/NeuRotic/experimental-lab/builds/universal-input/runtime/bin/ /p:ForceImportBeforeCppTargets=C:/NeuRotic/experimental-lab/builds/universal-guides/dependencies.targets /p:PostBuildEventUseInBuild=false /p:PreBuildEventUseInBuild=false /p:BuildProjectReferences=false
exit /b %errorlevel%
