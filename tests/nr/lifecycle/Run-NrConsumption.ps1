#requires -Version 5.1
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [Parameter(Mandatory=$true)][string]$MaintenanceLockPath,
    [Parameter(Mandatory=$true)][string]$PythonExecutable,
    [ValidateRange(10,3600)][int]$TimeoutSeconds=300
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '..\contracts\NativeProcessGroup.ps1')
. (Join-Path $PSScriptRoot '..\contracts\CommandEvidence.ps1')
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$run=$null;$maintenance=$null;$status=2;$state=$null
try {
    $maintenance=[IO.File]::Open($MaintenanceLockPath,[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
    if ($env:CL -or $env:_CL_ -or $env:VSCMD_ARG_TGT_ARCH -ne 'x64') {throw 'Explicit x64 compiler environment required.'}
    $compiler=(Get-Command cl.exe -CommandType Application).Source
    $python=(Get-Command $PythonExecutable -CommandType Application).Source
    $run=New-NrOwnedRunDirectory $OutputDirectory $repo
    $state=@{run=$run;repository=$repo;commands=[Collections.Generic.List[object]]::new();infrastructure=[Collections.Generic.List[object]]::new()}
    $files=@()
    foreach ($folder in @('OptiScaler\nr','tests\nr','OptiScaler\dlssnr')) {
        $files+=@(Get-ChildItem -LiteralPath (Join-Path $repo $folder) -File -Recurse | Where-Object {$_.Extension -in @('.h','.cpp','.ps1','.py','.json')})
    }
    $before=@($files | ForEach-Object {Get-NrHashReceipt $_.FullName})
    if (@($before | Where-Object {$_.state -ne 'Recorded'}).Count -or !(Save-NrStage $state 'sources-before.json' $before)) {throw 'Source identity unavailable.'}
    $all=$true
    $suites=@(@('consumption','consumption-leases'),@('gpu_lease','gpu-consumption-leases'),@('guide_lease','guide-consumption-leases'),@('provider_lease','provider-consumption-leases'))
    foreach ($entry in $suites) {
        $suite=$entry[0];$exe=Join-Path $run.path ($suite+'.exe');$obj=Join-Path $run.path ($suite+'.obj')
        $arguments=@('/nologo','/std:c++20','/EHsc','/W4','/WX','/wd4324','/Y-','/UNDEBUG','/I',(Join-Path $repo 'OptiScaler'),
            '/I',(Join-Path $repo 'OptiScaler\include'),(Join-Path $PSScriptRoot ($suite+'_tests.cpp')),
            ('/Fe'+$exe),('/Fo'+$obj),'/link','/STACK:8388608','d3d12.lib','dxgi.lib',(Join-Path $repo 'OptiScaler\library\detours\detours.lib'))
        $built=Invoke-NrRecordedCommand $state ($suite+'-compile') $compiler $arguments $TimeoutSeconds
        if (!$built) {$all=$false;continue}
        $passed=Invoke-NrRecordedCommand $state ($suite+'-execute') $exe @($run.run_id) $TimeoutSeconds
        $stdout=$state.commands[$state.commands.Count-1].intent.stdout
        $verified=Invoke-NrRecordedCommand $state ($suite+'-verify') $python @('-B',(Join-Path $PSScriptRoot 'verify_consumption.py'),
            '--log',$stdout,'--suite',$entry[1],'--run-id',$run.run_id) $TimeoutSeconds
        if (!$passed -or !$verified) {$all=$false}
    }
    $after=@($files | ForEach-Object {Get-NrHashReceipt $_.FullName})
    $stable=$before.Count -eq $after.Count
    for ($i=0;$i -lt $before.Count;$i++) {if ($before[$i].sha256 -ne $after[$i].sha256 -or $after[$i].state -ne 'Recorded') {$stable=$false}}
    $saved=Save-NrStage $state 'command-evidence.json' ([ordered]@{
        artifact_kind='LIFE002_CPU_WARP_COMMAND_EVIDENCE';run_id=$run.run_id;repository=$repo
        sources_before=$before;sources_after=$after;source_files_unchanged=$stable
        commands=$state.commands.ToArray();compiler=(Get-NrHashReceipt $compiler);python=(Get-NrHashReceipt $python)
        vs_version=$env:VisualStudioVersion;sdk=$env:WindowsSDKVersion;owned_commands_passed=$all
        execution_scope='CPU and software D3D12 fixtures; no proprietary model or game'
        hardwareProvider='NOT_RUN';temporalImageQuality='NOT_RUN';performanceLatency='NOT_RUN';gate_approval='NOT_PERFORMED'
    })
    if ($saved -and $stable -and $all -and $state.commands.Count -eq 12) {$status=0} else {$status=1}
    Write-Host ('Consumption lease CPU/WARP evidence: '+$run.path)
} catch {
    if ($state) {$null=Save-NrStage $state 'failure.json' @{error=$_.Exception.ToString();gate_approval='NOT_PERFORMED'}}
    Write-Error -ErrorAction Continue $_;$status=2
} finally {
    if ($run -and $run.lock) {$run.lock.Dispose()}
    if ($maintenance) {$maintenance.Dispose()}
}
exit $status
