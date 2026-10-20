param(
    [string]$MSBuild = 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe',
    [string]$Toolset = 'v145',
    [string]$ToolsVersion = '14.44.35207'
)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$output=Join-Path $taskRoot 'builds\character-inspector'
New-Item -ItemType Directory -Path (Join-Path $output 'product-objects') -Force | Out-Null
# The selected compile target does not execute the project's version prebuild.
$date=[DateTime]::UtcNow.ToString('yyyyMMdd_HHmmss')
$revision=(& git -C $taskRoot rev-parse --short HEAD).Trim()
if (& git -C $taskRoot status --porcelain --untracked-files=normal) { $revision+='-experimental-dirty' }
Set-Content -LiteralPath (Join-Path $taskRoot 'OptiScaler\resource_build_date.h') -Value ('#define VER_BUILD_DATE "'+$date+'"')
Set-Content -LiteralPath (Join-Path $taskRoot 'OptiScaler\resource_build_commit.h') -Value ('#define VER_BUILD_COMMIT "'+$revision+'"')
# This host can expose both Path and PATH. The child batch normalizes their casing,
# matching the established product build wrapper and avoiding MSBuild MSB6001.
$batch=Join-Path $output 'product-compile.cmd'
$lines=@('@echo off','set "NR_CI_BUILD_PATH=%PATH%"','set PATH=','set "Path=%NR_CI_BUILD_PATH%"',
    ('"'+$MSBuild+'" "'+(Join-Path $taskRoot 'OptiScaler\OptiScaler.vcxproj')+'" /t:ClCompile /m:2 /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset='+$Toolset+' /p:VCToolsVersion='+$ToolsVersion+' /p:SolutionDir="'+$taskRoot+'/" /p:OutDir="'+$output+'/bin/" /p:IntDir="'+$output+'/product-objects/" /p:TrackFileAccess=false /p:PostBuildEventUseInBuild=false /p:ForceImportAfterCppTargets="'+(Join-Path $PSScriptRoot 'character_product_compile.targets')+'" /v:minimal'),
    'exit /b %ERRORLEVEL%')
Set-Content -LiteralPath $batch -Value $lines -Encoding ascii
& cmd.exe /d /c $batch *> (Join-Path $output 'product-compile.log')
$result=$LASTEXITCODE
Get-Content -LiteralPath (Join-Path $output 'product-compile.log') -Tail 20
exit $result
