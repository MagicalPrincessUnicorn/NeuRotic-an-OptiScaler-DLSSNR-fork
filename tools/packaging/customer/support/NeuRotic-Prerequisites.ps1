# Read-only prerequisite evidence. Never load runtime DLLs or install software.
function Get-NeuroticNativeSystemDirectory([string]$WindowsDirectory,[bool]$Is64BitOs,[bool]$Is64BitProcess) {
    if ($Is64BitOs -and -not $Is64BitProcess) { return Join-Path $WindowsDirectory 'Sysnative' }
    return Join-Path $WindowsDirectory 'System32'
}

function Test-NeuroticVcRuntimeEvidence([object[]]$Evidence,[version]$MinimumVersion,[bool]$Is64BitOs) {
    $problems = @()
    if (-not $Is64BitOs) { $problems += 'NeuRotic requires 64-bit Windows.' }
    foreach ($name in @('msvcp140.dll','msvcp140_atomic_wait.dll','vcruntime140.dll','vcruntime140_1.dll')) {
        $component = @($Evidence | Where-Object { $_.Name -ieq $name })
        if ($component.Count -ne 1 -or $null -eq $component[0].Version) {
            $problems += "$name is missing or could not be checked."
        } elseif ([version]$component[0].Version -lt $MinimumVersion) {
            $problems += "$name is older than $MinimumVersion."
        }
    }
    return [pscustomobject]@{Ready=($problems.Count -eq 0);MinimumVersion=$MinimumVersion;
        Is64BitOs=$Is64BitOs;Problems=$problems}
}

function Get-NeuroticVcRuntimeStatus([version]$MinimumVersion = [version]'14.44.35207.0') {
    # System32 contains native x64 runtime files on supported x64 Windows. A
    # 32-bit PowerShell host must use Sysnative to avoid inspecting x86 files.
    $directory = Get-NeuroticNativeSystemDirectory ([Environment]::GetFolderPath('Windows')) `
        ([Environment]::Is64BitOperatingSystem) ([Environment]::Is64BitProcess)
    $evidence = foreach ($name in @('msvcp140.dll','msvcp140_atomic_wait.dll','vcruntime140.dll','vcruntime140_1.dll')) {
        $version = $null
        try {
            $path = Join-Path $directory $name
            if ([IO.File]::Exists($path)) {
                $info = [Diagnostics.FileVersionInfo]::GetVersionInfo($path)
                $version = New-Object Version($info.FileMajorPart,$info.FileMinorPart,$info.FileBuildPart,$info.FilePrivatePart)
            }
        } catch { $version = $null }
        [pscustomobject]@{Name=$name;Version=$version}
    }
    return Test-NeuroticVcRuntimeEvidence @($evidence) $MinimumVersion ([Environment]::Is64BitOperatingSystem)
}

function Write-NeuroticVcRuntimeRequirement($Status) {
    if (-not $Status.Is64BitOs) {
        Write-Output 'NeuRotic requires 64-bit Windows. No game files have been changed.'
        return
    }
    Write-Output ''
    Write-Output 'NeuRotic Setup - Microsoft runtime warning'
    Write-Output "Microsoft Visual C++ Redistributable (x64), version $($Status.MinimumVersion) or newer, could not be verified."
    Write-Output 'Some runtime components are missing, too old, or could not be checked.'
    Write-Output 'Without this runtime, NeuRotic may fail to load or display errors.'
    Write-Output 'You can continue installation anyway, or cancel and install (or Repair) the latest x64 runtime from Microsoft:'
    Write-Output 'https://aka.ms/vc14/vc_redist.x64.exe'
    Write-Output 'After installing it, run NeuRotic Setup again.'
    Write-Output 'No game files have been changed.'
}
