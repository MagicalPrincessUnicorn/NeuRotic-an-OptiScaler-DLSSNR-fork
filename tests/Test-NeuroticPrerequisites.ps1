$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$engine = Get-Content -Raw -LiteralPath (Join-Path $root 'tools/packaging/customer/support/NeuRotic-Setup-Engine.ps1')
$script:checks = 0
function Assert([bool]$condition,[string]$reason) {
    if (-not $condition) { throw $reason }
    ++$script:checks
}
Assert ($engine.Contains('Get-NeuroticVcRuntimeStatus')) 'Setup has no x64 runtime prerequisite gate.'
. (Join-Path $root 'tools/packaging/customer/support/NeuRotic-Prerequisites.ps1')
$minimum = [version]'14.44.35207.0'
function Evidence([version]$version) {
    @('msvcp140.dll','msvcp140_atomic_wait.dll','vcruntime140.dll','vcruntime140_1.dll') | ForEach-Object {
        [pscustomobject]@{Name=$_;Version=$version}
    }
}
Assert (Test-NeuroticVcRuntimeEvidence (Evidence $minimum) $minimum $true).Ready 'Exact minimum runtime refused.'
Assert (Test-NeuroticVcRuntimeEvidence (Evidence ([version]'14.51.36247.0')) $minimum $true).Ready 'Newer runtime refused.'
Assert (-not (Test-NeuroticVcRuntimeEvidence (Evidence ([version]'14.44.35206.0')) $minimum $true).Ready) 'Older runtime admitted.'
Assert (-not (Test-NeuroticVcRuntimeEvidence @() $minimum $true).Ready) 'Missing x64 runtime admitted.'
Assert (-not (Test-NeuroticVcRuntimeEvidence (Evidence $minimum) $minimum $false).Ready) '32-bit Windows admitted.'
foreach ($name in @('msvcp140.dll','msvcp140_atomic_wait.dll','vcruntime140.dll','vcruntime140_1.dll')) {
    $missing = @(Evidence $minimum | Where-Object Name -ne $name)
    Assert (-not (Test-NeuroticVcRuntimeEvidence $missing $minimum $true).Ready) "Missing component admitted: $name"
    $unknown = Evidence $minimum
    ($unknown | Where-Object Name -eq $name).Version = $null
    Assert (-not (Test-NeuroticVcRuntimeEvidence $unknown $minimum $true).Ready) "Unknown component version admitted: $name"
    $mixed = Evidence $minimum
    ($mixed | Where-Object Name -eq $name).Version = [version]'14.29.30139.0'
    Assert (-not (Test-NeuroticVcRuntimeEvidence $mixed $minimum $true).Ready) "Mixed old runtime admitted: $name"
}
Assert ((Get-NeuroticNativeSystemDirectory 'C:\Windows' $true $true) -eq 'C:\Windows\System32') 'Native x64 system directory differs.'
Assert ((Get-NeuroticNativeSystemDirectory 'C:\Windows' $true $false) -eq 'C:\Windows\Sysnative') '32-bit PowerShell did not select native x64 files.'
$failed = Test-NeuroticVcRuntimeEvidence @() $minimum $true
$message = (Write-NeuroticVcRuntimeRequirement $failed | Out-String)
Assert ($message.Contains('Microsoft Visual C++') -and $message.Contains('x64')) 'Missing-runtime message is not explicit.'
Assert ($message.Contains('https://aka.ms/vc14/vc_redist.x64.exe')) 'Message omits official x64 download.'
Assert ($message.Contains('run NeuRotic Setup again')) 'Message omits retry instructions.'
Assert ($message.Contains('may fail to load or display errors')) 'Runtime message does not explain the optional risk.'
Assert ($engine.Contains('[switch]$ContinueWithoutRuntime')) 'Installer has no explicit continue-anyway option.'
Assert ($engine.Contains('Continue installation anyway')) 'Installer omits the interactive continue choice.'
$gate = $engine.IndexOf('Get-NeuroticVcRuntimeStatus')
Assert ($gate -gt $engine.IndexOf('if ($Uninstall)')) 'Runtime check blocks uninstall.'
Assert ($gate -gt $engine.IndexOf("if (`$ExistingInstallAction -eq 'Uninstall')")) 'Runtime check blocks Setup existing-install uninstall.'
Assert ($gate -lt $engine.IndexOf('$action = ''None''')) 'Runtime check occurs after installation planning.'
Assert ($engine.Contains("'support\NeuRotic-Prerequisites.ps1'")) 'Prerequisite helper is not admitted by member hashes.'
$live = Get-NeuroticVcRuntimeStatus -MinimumVersion $minimum
Assert $live.Ready 'Installed native x64 runtime could not be verified on the test host.'
foreach ($builder in @('Build-Public-InstallerPackage.ps1','Build-W03GameTestPackage.py','Build-PreparedDepthGamePackage.py')) {
    $text = Get-Content -Raw -LiteralPath (Join-Path $root ('tools/packaging/customer/' + $builder))
    Assert ($text.Contains('NeuRotic-Prerequisites.ps1')) "Current-template packager omits the runtime checker: $builder"
}
Write-Output "PASS: $script:checks runtime policy, message, native-path and installer-order checks"
