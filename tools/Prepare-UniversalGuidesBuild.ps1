param([string]$DependencyRoot='C:/NeuRotic/Optiscaler-Stuff/external',[string]$BuildLabel='')
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$output=Join-Path $repo 'builds/universal-guides'
$generated=Join-Path $output 'generated'
New-Item -ItemType Directory -Force -Path $generated | Out-Null
$includes=@($generated,(Join-Path $repo 'OptiScaler'),(Join-Path $repo 'OptiScaler/include'))
$includes+=@('vulkan/include','nvngx_dlss_sdk','xess/inc/xess','xess/inc/xell','xess/inc/xess_fg',
 'FidelityFX-SDK/ffx-api/include/ffx_api','simpleini','unordered_dense/include','spdlog/include','freetype',
 'streamline','streamline1','nvapi','nlohmann','fakenvapi','magic_enum/include/magic_enum','AntiLag2-SDK','latencyflex') |
 ForEach-Object { Join-Path $DependencyRoot $_ }
if(-not $BuildLabel){
 $BuildLabel=(& git -C $repo rev-parse --short=12 HEAD).Trim()
 if($LASTEXITCODE -ne 0){throw 'Unable to identify build source'}
 if(& git -C $repo status --porcelain){$BuildLabel+='-working'}
}
if($BuildLabel -notmatch '^[A-Za-z0-9._-]{1,80}$'){throw 'Invalid build label'}
[IO.File]::WriteAllLines((Join-Path $generated 'resource_build_date.h'),@('#define VER_BUILD_DATE "'+[DateTime]::UtcNow.ToString('yyyyMMdd')+'_UNIVERSAL_CONNECTIONS"'))
[IO.File]::WriteAllLines((Join-Path $generated 'resource_build_commit.h'),@('#define VER_BUILD_COMMIT "'+$BuildLabel+'"'))
$common=@('/nologo','/Zs','/std:c++latest','/EHsc','/DNDEBUG','/D_WINDOWS','/D_USRDLL','/DUNICODE','/D_UNICODE',
 '/D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR')+@($includes|ForEach-Object{'/I"'+$_+'"'})
$commands=@('@echo off','call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul',
 'if errorlevel 1 exit /b 1','set CL=','set _CL_=','set VSTEL_EWDK=1')
foreach($source in @('shaders/dlssnr/DlssNr_Dx12.cpp','inputs/NVNGX_DLSS_Dx12.cpp','dlssnr/DlssNr_Present.cpp')) {
 $name=[IO.Path]::GetFileNameWithoutExtension($source)
 $response=Join-Path $output ($name+'.rsp')
 [IO.File]::WriteAllLines($response,$common+@('"'+(Join-Path $repo ('OptiScaler/'+$source))+'"'))
 $commands+=@(('cl @"'+$response+'"'),'if errorlevel 1 exit /b 1')
}
$commands+='exit /b 0'
[IO.File]::WriteAllLines((Join-Path $output 'check-source.cmd'),$commands)
$libraries=@('fsr2','fsr2_212','fsr31','vulkan','d3dx','detours')|ForEach-Object{Join-Path (Split-Path $DependencyRoot -Parent) ('OptiScaler/library/'+$_)}
$libraries+=@((Join-Path $DependencyRoot 'xess/lib'),(Join-Path $DependencyRoot 'freetype'))
$ix=[Security.SecurityElement]::Escape(($includes -join ';')+';$(IncludePath)')
$lx=[Security.SecurityElement]::Escape(($libraries -join ';')+';$(LibraryPath)')
$targets='<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003"><PropertyGroup><IncludePath>'+$ix+'</IncludePath><LibraryPath>'+$lx+'</LibraryPath></PropertyGroup><ItemDefinitionGroup><ClCompile><AdditionalOptions>/MP2 %(AdditionalOptions)</AdditionalOptions></ClCompile><ResourceCompile><AdditionalIncludeDirectories>'+[Security.SecurityElement]::Escape($generated)+';%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories></ResourceCompile></ItemDefinitionGroup></Project>'
[IO.File]::WriteAllText((Join-Path $output 'dependencies.targets'),$targets)
$msbuild='C:/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/MSBuild.exe'
$build=@('@echo off','set "NR_GUIDE_BUILD_PATH=%PATH%"','set PATH=','set "Path=%NR_GUIDE_BUILD_PATH%"','set CL=','set _CL_=','set VSTEL_EWDK=1',
 ('"'+$msbuild+'" "'+$repo+'/OptiScaler/OptiScaler.vcxproj" /nologo /m:1 /v:minimal /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v145 /p:VCToolsVersion=14.44.35207 /p:TrackFileAccess=false /p:PreBuildEventUseInBuild=false /p:PostBuildEventUseInBuild=false /p:IntDir="'+$output+'/product-obj/" /p:OutDir="'+$output+'/bin/" /p:ForceImportBeforeCppTargets="'+$output+'/dependencies.targets"'),
 'exit /b %errorlevel%')
[IO.File]::WriteAllLines((Join-Path $output 'build-product.cmd'),$build)
Write-Output (Join-Path $output 'check-source.cmd')
