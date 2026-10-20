param(
    [ValidateSet('nr_mfg_control', 'nr_mfg_provider', 'nr_mfg_publication', 'nr_mfg_temporal', 'vulkan_nr_integration', 'vulkan_nr_provider_caps', 'vulkan_nr_ffx_fg', 'vulkan_nr_streamline', 'vulkan_nr_pre_fg', 'vulkan_nr_capture', 'vulkan_nr_diagnostics', 'vulkan_nr_gpu', 'all', 'vulkan_rr_routing', 'vulkan_nr_tuning', 'vulkan_nr_frame_params', 'vulkan_nr_frame_contract', 'vulkan_nr_recording', 'vulkan_nr_completion', 'vulkan_nr_reservations', 'vulkan_nr_generation', 'vulkan_nr_resolution', 'vulkan_nr_pre_sr', 'vulkan_nr_pass_plan', 'vulkan_nr_pass_recording', 'vulkan_present_guides', 'vulkan_present_color', 'vulkan_nr_session', 'vulkan_present_admission', 'vulkan_present_recording', 'vulkan_present_recreation', 'vulkan_present_submission', 'vulkan_present_status', 'nr_present_history', 'nr_present_pacing', 'nr_present_resolution', 'nr_present_color')]
    [string] $Test = 'all',
    [switch] $BuildProduct,
    [switch] $RebuildProduct
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$output = Join-Path $repo 'builds\vulkan-experiment\tests'
$vcvars = 'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path -LiteralPath $vcvars)) { throw "VS18 v145 setup missing: $vcvars" }
New-Item -ItemType Directory -Path $output -Force | Out-Null

if ($BuildProduct -or $RebuildProduct) {
    $sourceCommit = (& git -c "safe.directory=$repo" -C $repo rev-parse HEAD).Trim()
    $sourceShort = (& git -c "safe.directory=$repo" -C $repo rev-parse --short HEAD).Trim()
    if($RebuildProduct){
        & git -c "safe.directory=$repo" -C $repo diff HEAD --quiet -- OptiScaler
        if($LASTEXITCODE -ne 0){throw 'Commit product source before an exact all-source rebuild.'}
    }
    $branch = (& git -C $repo branch --show-current).Trim()
    $buildRoot = Join-Path $repo $(if($branch -eq 'codex/flagship'){'builds\present-multipass-release'}else{'builds\vulkan-experiment\release'})
    $bin = Join-Path $buildRoot 'bin'
    $msbuild = 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe'
    if (-not (Test-Path -LiteralPath $msbuild)) { throw "MSBuild missing: $msbuild" }
    New-Item -ItemType Directory -Path $buildRoot, $bin -Force | Out-Null
    $script = Join-Path $buildRoot 'run-product-build.cmd'
    $target = if ($RebuildProduct) { "Rebuild" } else { "Build" }
    $common = "/t:$target /m:4 /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v145 /p:VCToolsVersion=14.44.35207 /p:SolutionDir=$repo\ /p:OutDir=$bin\ /p:TrackFileAccess=false /p:PostBuildEventUseInBuild=false /v:minimal"
    @(
        '@echo off'
        'set "NR_VK_BUILD_PATH=%PATH%"'
        'set PATH='
        'set "Path=%NR_VK_BUILD_PATH%"'
        "`"$msbuild`" `"$repo\OptiScaler\OptiScaler.vcxproj`" $common /p:IntDir=$buildRoot\obj\OptiScaler\"
        'if errorlevel 1 exit /b %ERRORLEVEL%'
        "`"$msbuild`" `"$repo\OptiScaler\dlssnr\forwarder\dlssnr_forwarder.vcxproj`" $common /p:IntDir=$buildRoot\obj\dlssnr_forwarder\"
        'exit /b %ERRORLEVEL%'
    ) | Set-Content -LiteralPath $script -Encoding ASCII
    & cmd.exe /d /c "`"$script`""
    if ($LASTEXITCODE -ne 0) { throw "Release product/forwarder build failed with exit $LASTEXITCODE" }
    foreach ($name in @('OptiScaler.dll','nvngx.dll_dlssnr.dll')) {
        if (-not (Test-Path -LiteralPath (Join-Path $bin $name) -PathType Leaf)) {
            throw "Release output missing: $name"
        }
    }
    if($RebuildProduct){
        if((& git -c "safe.directory=$repo" -C $repo rev-parse HEAD).Trim() -ne $sourceCommit){throw 'HEAD changed during exact rebuild.'}
        & git -c "safe.directory=$repo" -C $repo diff HEAD --quiet -- OptiScaler
        if($LASTEXITCODE -ne 0){throw 'Product source changed during exact rebuild.'}
        $version=[Diagnostics.FileVersionInfo]::GetVersionInfo((Join-Path $bin 'OptiScaler.dll')).ProductVersion
        if(-not $version.Contains($sourceShort)){throw "Product resource does not identify $sourceShort : $version"}
        @{
            source_commit=$sourceCommit; target='Rebuild'; configuration='Release/x64';
            toolchain='VS18 v145 14.44.35207'; source_unchanged=$true; exit_code=0; product_version=$version;
            product_sha256=(Get-FileHash -LiteralPath (Join-Path $bin 'OptiScaler.dll')).Hash.ToLowerInvariant();
            forwarder_sha256=(Get-FileHash -LiteralPath (Join-Path $bin 'nvngx.dll_dlssnr.dll')).Hash.ToLowerInvariant();
            config_sha256=(Get-FileHash -LiteralPath (Join-Path $repo 'integration/OptiScaler.ini')).Hash.ToLowerInvariant();
            shaders_sha256=(Get-FileHash -LiteralPath (Join-Path $repo 'docs/implementation-evidence/vulkan-experiment/VULKAN-SHADERS.json')).Hash.ToLowerInvariant()
        } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $buildRoot 'exact-build-receipt.json') -Encoding utf8
        Write-Output "PASS exact all-source identity: $sourceCommit ($version)"
    }
    Write-Output "PASS Release/x64 product and Vulkan NR forwarder: $bin"
    return
}

$tests = if ($Test -eq 'all') {
    @('nr_mfg_control', 'nr_mfg_provider', 'nr_mfg_publication', 'nr_mfg_temporal', 'vulkan_nr_integration', 'vulkan_nr_provider_caps', 'vulkan_nr_ffx_fg', 'vulkan_nr_streamline', 'vulkan_nr_pre_fg', 'vulkan_nr_capture', 'vulkan_nr_diagnostics', 'vulkan_rr_routing', 'vulkan_nr_tuning', 'vulkan_nr_frame_params', 'vulkan_nr_frame_contract', 'vulkan_nr_recording', 'vulkan_nr_completion', 'vulkan_nr_reservations', 'vulkan_nr_generation', 'vulkan_nr_resolution', 'vulkan_nr_pre_sr', 'vulkan_nr_pass_plan', 'vulkan_nr_pass_recording', 'vulkan_present_guides', 'vulkan_present_color', 'vulkan_nr_session', 'vulkan_present_admission', 'vulkan_present_recording', 'vulkan_present_recreation', 'vulkan_present_submission', 'vulkan_present_status', 'nr_present_history', 'nr_present_pacing', 'nr_present_resolution', 'nr_present_color')
} else { @($Test) }

foreach ($name in $tests) {
    $source = Join-Path $repo "tests\$name.cpp"
    if (-not (Test-Path -LiteralPath $source)) { throw "Test source missing: $source" }
    $exe = Join-Path $output "$name.exe"
    $obj = Join-Path $output "$name.obj"
    $cmdFile = Join-Path $output "run-$name.cmd"
    $extraSources = if ($name -eq 'vulkan_nr_integration') {
        '"OptiScaler\dlssnr\VulkanPresentRegistry.cpp" "OptiScaler\dlssnr\VulkanPresentSubmission.cpp" "OptiScaler\dlssnr\VulkanNrRecording.cpp"'
    } elseif ($name -eq 'vulkan_present_admission') {
        '"OptiScaler\dlssnr\VulkanPresentRegistry.cpp" "OptiScaler\dlssnr\VulkanNrRecording.cpp"'
    } elseif ($name -eq 'vulkan_nr_ffx_fg') {
        '"OptiScaler\dlssnr\VulkanNrFfxFg.cpp" "OptiScaler\dlssnr\VulkanNrPreFg.cpp" "OptiScaler\dlssnr\VulkanNrRecording.cpp"'
    } elseif ($name -eq 'vulkan_nr_streamline') {
        '"OptiScaler\dlssnr\VulkanNrStreamline.cpp" "OptiScaler\dlssnr\VulkanNrPreFg.cpp" "OptiScaler\dlssnr\VulkanNrRecording.cpp"'
    } elseif ($name -eq 'vulkan_nr_pre_fg') {
        '"OptiScaler\dlssnr\VulkanNrPreFg.cpp" "OptiScaler\dlssnr\VulkanNrRecording.cpp"'
    } elseif ($name -eq 'vulkan_nr_capture') {
        '"OptiScaler\dlssnr\VulkanNrCapture.cpp" "OptiScaler\dlssnr\VulkanNrRecording.cpp"'
    } elseif ($name -eq 'vulkan_nr_diagnostics') {
        '"OptiScaler\dlssnr\VulkanNrExposureScan.cpp" "OptiScaler\dlssnr\VulkanNrRecording.cpp"'
    } elseif ($name -eq 'vulkan_nr_session') {
        '"OptiScaler\dlssnr\VulkanNrSession.cpp" "OptiScaler\dlssnr\VulkanNrRuntime.cpp" "OptiScaler\dlssnr\VulkanNrRecording.cpp" "OptiScaler\dlssnr\VulkanNrCompletion.cpp" "OptiScaler\dlssnr\VulkanPresentRegistry.cpp"'
    } elseif ($name -eq 'vulkan_nr_gpu') {
        '"OptiScaler\shaders\Shader_Vk.cpp" "OptiScaler\shaders\dlssnr\DlssNr_Vk.cpp" "OptiScaler\dlssnr\VulkanNrGeneration.cpp" "OptiScaler\dlssnr\VulkanNrCapture.cpp" "OptiScaler\dlssnr\VulkanNrPassRecorder.cpp" "OptiScaler\shaders\format_transfer\PresentColor_Vk.cpp" "OptiScaler\dlssnr\VulkanNrRecording.cpp" "OptiScaler\dlssnr\VulkanNrCompletion.cpp" "OptiScaler\dlssnr\VulkanPresentRegistry.cpp" "OptiScaler\dlssnr\VulkanNrSession.cpp" "OptiScaler\dlssnr\VulkanNrRuntime.cpp"'
    } elseif ($name -eq 'vulkan_nr_completion') {
        '"OptiScaler\dlssnr\VulkanNrRecording.cpp" "OptiScaler\dlssnr\VulkanNrCompletion.cpp" "OptiScaler\dlssnr\VulkanPresentRegistry.cpp" "OptiScaler\dlssnr\VulkanNrSession.cpp" "OptiScaler\dlssnr\VulkanNrRuntime.cpp"'
    } elseif ($name -eq 'vulkan_present_guides') {
        '"OptiScaler\dlssnr\VulkanPresentGuides.cpp" "OptiScaler\dlssnr\VulkanNrImageFacts.cpp" "OptiScaler\dlssnr\VulkanNrRecording.cpp"'
    } elseif ($name -eq 'vulkan_nr_pass_recording') {
        '"OptiScaler\dlssnr\VulkanNrPassRecorder.cpp" "OptiScaler\dlssnr\VulkanNrRecording.cpp"'
    } elseif ($name -eq 'vulkan_nr_pre_sr') {
        '"OptiScaler\dlssnr\VulkanNrPreSr.cpp" "OptiScaler\dlssnr\VulkanNrRecording.cpp"'
    } elseif ($name -eq 'vulkan_nr_generation') {
        '"OptiScaler\dlssnr\VulkanNrGeneration.cpp" "OptiScaler\dlssnr\VulkanNrRecording.cpp"'
    } elseif ($name -eq 'vulkan_nr_reservations') {
        '"OptiScaler\dlssnr\VulkanNrRecording.cpp"'
    } elseif ($name -eq 'vulkan_nr_recording') {
        '"OptiScaler\dlssnr\VulkanNrRecording.cpp"'
    } elseif ($name -eq 'vulkan_present_status') {
        '"OptiScaler\dlssnr\VulkanPresentStatus.cpp" "OptiScaler\dlssnr\VulkanNrRecording.cpp"'
    } elseif ($name -eq 'vulkan_present_recreation') {
        '"OptiScaler\dlssnr\VulkanPresentExecutor.cpp" "OptiScaler\dlssnr\VulkanPresentStatus.cpp" "OptiScaler\dlssnr\VulkanPresentSubmission.cpp" "OptiScaler\dlssnr\VulkanPresentRegistry.cpp" "OptiScaler\dlssnr\VulkanNrRecording.cpp" "OptiScaler\dlssnr\connections\ConnectionPolicy.cpp" "OptiScaler\dlssnr\connections\ConnectionSelection.cpp"'
    } elseif ($name -eq 'vulkan_present_submission') {
        '"OptiScaler\dlssnr\VulkanPresentSubmission.cpp"'
    } elseif ($name -eq 'vulkan_present_recording') {
        '"OptiScaler\dlssnr\VulkanPresent.cpp"'
    } elseif ($name -eq 'vulkan_present_admission') {
        '"OptiScaler\dlssnr\VulkanPresentRegistry.cpp"'
    } else { '' }
    $productIncludes = ''
    if($name -eq 'vulkan_present_recreation') {
        [xml]$project = Get-Content (Join-Path $repo 'OptiScaler/OptiScaler.vcxproj')
        $productIncludes = '/FI pch.h /wd4324 /wd4100 /wd4244 /wd4245 ' + ((($project.Project.PropertyGroup.IncludePath | Select-Object -Last 1).Split(';') | Where-Object { $_ -ne '$(IncludePath)' } | ForEach-Object {
            '/I "' + $_.Replace('$(ProjectDir)', 'OptiScaler/').Replace('$(SolutionDir)', './').Replace('\','/').TrimEnd('/') + '"'
        }) -join ' ')
    }
    $objectTarget = if ($extraSources) { ($output -replace '\\', '/') + '/' } else { $obj }
    $linkLibraries = if ($name -eq 'vulkan_nr_gpu') { '/link "OptiScaler\library\vulkan\vulkan-1.lib"' } elseif ($name -eq 'nr_present_color') { '/link dxgi.lib' } else { '' }
    # The existing resolution control uses an intentionally 256-byte-aligned shader constant struct.
    $fixtureOptions = if ($name -eq "vulkan_nr_gpu") { "/DNR_VK_GPU_FIXTURE /wd4324 /I tests/fixtures/vulkan_shader" } else { "" }
    $warningOptions = if ($name -in @('nr_present_resolution','vulkan_nr_capture','vulkan_nr_diagnostics')) { '/wd4324' } else { '' }
    @(
        '@echo off'
        "call `"$vcvars`" >nul"
        'if errorlevel 1 exit /b 1'
        "cd /d `"$repo`""
        "cl /nologo /std:c++20 /EHsc /W4 /WX $warningOptions $fixtureOptions $productIncludes /I `"OptiScaler`" /I `"OptiScaler\include`" /I `"external\streamline`" /I `"external\simpleini`" /I `"external\nvngx_dlss_sdk`" /I `"external\vulkan\include`" `"$source`" $extraSources /Fe:`"$exe`" /Fo:`"$objectTarget`" $linkLibraries"
        'if errorlevel 1 exit /b 1'
        "`"$exe`""
    ) | Set-Content -LiteralPath $cmdFile -Encoding ASCII
    & cmd.exe /d /c "`"$cmdFile`""
    if ($name -eq 'vulkan_nr_gpu' -and $LASTEXITCODE -eq 2) { Write-Output "UNAVAILABLE vulkan_nr_gpu"; return }
    if ($LASTEXITCODE -ne 0) { throw "$name failed with exit $LASTEXITCODE" }
    Write-Output "PASS $name"
}

if($Test -eq "all"){
    $python="python"
    & $python (Join-Path $repo "tests/vulkan_nr_ui_contract.py")
    if($LASTEXITCODE -ne 0){throw "Vulkan UI capability contract failed"}
}
