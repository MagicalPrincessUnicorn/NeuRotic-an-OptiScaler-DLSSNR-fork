#requires -Version 5.1
[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$OutputDirectory,
    [Parameter(Mandatory=$true)][string]$MaintenanceLockPath,
    [Parameter(Mandatory=$true)][string]$PythonExecutable,
    [ValidateRange(10,3600)][int]$TimeoutSeconds=300)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '..\contracts\NativeProcessGroup.ps1')
. (Join-Path $PSScriptRoot '..\contracts\CommandEvidence.ps1')
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$run=$null;$maintenance=$null;$state=$null;$status=2
try {
    $maintenance=[IO.File]::Open($MaintenanceLockPath,[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
    if ($env:CL -or $env:_CL_ -or $env:VSCMD_ARG_TGT_ARCH -ne 'x64') {throw 'Explicit clean x64 compiler environment required.'}
    $compiler=(Get-Command cl.exe -CommandType Application).Source
    $librarian=(Get-Command lib.exe -CommandType Application).Source
    $python=(Get-Command $PythonExecutable -CommandType Application).Source
    $run=New-NrOwnedRunDirectory $OutputDirectory $repo
    $state=@{run=$run;repository=$repo;commands=[Collections.Generic.List[object]]::new();infrastructure=[Collections.Generic.List[object]]::new()}
    $files=@()
    foreach($folder in @('OptiScaler\nr','tests\nr')) {
        $files+=@(Get-ChildItem -LiteralPath (Join-Path $repo $folder) -File -Recurse | Where-Object {$_.Extension -in @('.h','.cpp','.ps1','.py','.json')})
    }
    foreach($file in @('OptiScaler\shaders\dlssnr\DlssNr_Dx12.cpp','OptiScaler\dlssnr\DlssNrFeature_Dx12.h','OptiScaler\OptiScaler.vcxproj','OptiScaler\OptiScaler.vcxproj.filters','OptiScaler.ini')) {
        $files+=Get-Item -LiteralPath (Join-Path $repo $file)
    }
    $before=@($files | ForEach-Object {Get-NrHashReceipt $_.FullName})
    if(@($before | Where-Object {$_.state -ne 'Recorded'}).Count -or !(Save-NrStage $state 'sources-before.json' $before)){throw 'Source identity unavailable.'}
    $common=@('/nologo','/std:c++20','/EHsc','/W4','/WX','/Y-','/UNDEBUG','/I',(Join-Path $repo 'OptiScaler'))
    $core=@('OptiScaler\nr\orchestration\StreamCoordinatorKernel.cpp','OptiScaler\nr\lifecycle\NativeSessionLifetime.cpp','OptiScaler\nr\protocol\NativeRouteAdmission.cpp')
    $arguments=$common+@('/DNR_ORCH_INIT_TESTING','/c')+@($core|ForEach-Object {Join-Path $repo $_})+@('/Fo'+$run.path+'\')
    if(!(Invoke-NrRecordedCommand $state 'kernel-compile' $compiler $arguments $TimeoutSeconds)){throw 'Kernel compilation failed.'}
    $library=Join-Path $run.path 'initial-kernel.lib'
    $objects=@('StreamCoordinatorKernel.obj','NativeSessionLifetime.obj','NativeRouteAdmission.obj')|ForEach-Object {Join-Path $run.path $_}
    if(!(Invoke-NrRecordedCommand $state 'kernel-library' $librarian (@('/nologo',('/OUT:'+$library))+$objects) $TimeoutSeconds)){throw 'Kernel library failed.'}
    $all=$true
    foreach($pair in @(@('initial-profile-preparation','initial_profile_preparation.cpp'),@('initial-fixed-native-kernel','initial_fixed_native_kernel.cpp'),
        @('initial-native-admission','initial_native_admission.cpp'),@('initial-native-races','initial_native_races.cpp'),@('initial-native-protocol','initial_native_protocol.cpp'))) {
        $suite=$pair[0];$exe=Join-Path $run.path ($suite+'.exe');$obj=Join-Path $run.path ($suite+'.obj')
        $arguments=$common+@('/DNR_ORCH_INIT_TESTING',(Join-Path $PSScriptRoot $pair[1]),$library,('/Fe'+$exe),('/Fo'+$obj),'/link','/STACK:8388608')
        if(!(Invoke-NrRecordedCommand $state ($suite+'-compile') $compiler $arguments $TimeoutSeconds)){$all=$false;continue}
        $passed=Invoke-NrRecordedCommand $state ($suite+'-execute') $exe @($run.run_id) $TimeoutSeconds
        $stdout=$state.commands[$state.commands.Count-1].intent.stdout
        $verified=Invoke-NrRecordedCommand $state ($suite+'-verify') $python @('-B',(Join-Path $PSScriptRoot 'verify_initial.py'),'--log',$stdout,'--suite',$suite,'--run-id',$run.run_id) $TimeoutSeconds
        if(!$passed -or !$verified){$all=$false}
    }
    $authority=@()
    $markers=@('CurrentRouteView','Route','Recommit','NativeInvocationAdmission','ExecuteNativeProtocol','MintFinalSeal','CloseDrained','InitialNativeTestAccess')
    for($case=1;$case -le 8;$case++) {
        $source=Join-Path $PSScriptRoot 'initial_native_authority.cpp'
        $base=$common+@('/c',('/DNR_AUTH_CASE='+$case),$source,('/Fo'+(Join-Path $run.path ('authority-'+$case+'.obj'))))
        $control=Invoke-NrRecordedCommand $state ('auth-'+$case+'-control') $compiler ($base+@('/DNR_AUTH_CONTROL')) $TimeoutSeconds
        $accepted=Invoke-NrRecordedCommand $state ('auth-'+$case+'-negative') $compiler $base $TimeoutSeconds
        $record=$state.commands[$state.commands.Count-1]
        $diagnostic=Get-Content -LiteralPath $record.intent.stdout -Raw
        $intended=$control -and !$accepted -and $diagnostic.Contains($markers[$case-1]) -and $diagnostic.Contains('error C')
        $authority+=@{id=('INIT-AUTH-'+('{0:D2}' -f $case));positive_control=$control;negative_compiled=$accepted;intended_diagnostic=$intended}
        if(!$intended){$all=$false}
    }
    $after=@($files|ForEach-Object {Get-NrHashReceipt $_.FullName});$stable=$before.Count -eq $after.Count
    for($i=0;$i -lt $before.Count;$i++){if($before[$i].sha256 -ne $after[$i].sha256 -or $after[$i].state -ne 'Recorded'){$stable=$false}}
    $saved=Save-NrStage $state 'command-evidence.json' ([ordered]@{artifact_kind='ORCH002_INIT_CPU_EVIDENCE';run_id=$run.run_id;repository=$repo;
        sources_before=$before;sources_after=$after;source_files_unchanged=$stable;commands=$state.commands.ToArray();authority=$authority;
        compiler=(Get-NrHashReceipt $compiler);python=(Get-NrHashReceipt $python);kernel_library=(Get-NrHashReceipt $library);
        owned_checks_passed=$all;production_attachment='UNCONNECTED';modelRuntime='NOT_RUN';gpuResource='NOT_RUN';temporalImageQuality='NOT_RUN';performanceLatency='NOT_RUN';gate_approval='NOT_PERFORMED'})
    if($saved -and $stable -and $all){$status=0}else{$status=1}
    Write-Host ('INIT CPU evidence: '+$run.path)
} catch {
    if($state){$null=Save-NrStage $state 'failure.json' @{error=$_.Exception.ToString();gate_approval='NOT_PERFORMED'}}
    Write-Error -ErrorAction Continue $_;$status=2
} finally {if($run -and $run.lock){$run.lock.Dispose()};if($maintenance){$maintenance.Dispose()}}
exit $status
