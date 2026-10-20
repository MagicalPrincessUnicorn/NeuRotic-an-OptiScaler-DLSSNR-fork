#requires -Version 5.1
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][ValidateNotNullOrEmpty()][string]$OutputDirectory,
    [ValidateRange(10,3600)][int]$TimeoutSeconds=300
)
# NR-LIFE-001 candidate runner: NOT_RUN in Chat. Invoke only after authorized ARCH -> LIFE application.
# Uses unchanged ARCH-owned process containment/evidence helpers; no second implementation of them.
# From a reconciled VS x64 developer environment, choose an unused output directory outside the checkout.
# This records CPU commands and raw fixture assertions; it does not assign C15 verdicts or a gate decision.
# No dependency fetch, automatic vcvars discovery, repository mutation, GPU work, or mutation application.
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$shared=Join-Path $PSScriptRoot '..\contracts'
. (Join-Path $shared 'NativeProcessGroup.ps1')
. (Join-Path $shared 'CommandEvidence.ps1')
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$run=$null; $exitStatus=2
# Explicit reviewed inventory, never wildcard compile registration.
$contractHeaders=@('C01_Acquisition.h','C02_Context.h','C03_Consumption.h','C04_Capability.h','C05_Routing.h','C06_ProtocolInput.h','C07_ProtocolOutput.h','C08_Reset.h','C09_Multipass.h','C10_Gap.h','C11_Diagnostics.h','C12_Representation.h','C13_FinalFrame.h','C14_Identity.h','C15_Validation.h','C16_Execution.h','ContractCommon.h','ContractManifest.h','Evidence.h','FixtureCodec.h','Identity.h','Reasons.h','SemanticDescriptions.h')
$headers=@('IdentityAssociations.h','IdentityDependencies.h','IdentityIssuers.h','IdentityPrimitives.h','IdentityTransitions.h')
$tests=@('nr_identity_t07','nr_identity_t08','nr_identity_t09','nr_identity_final_seal')
$extraSources=@('IdentityTestSupport.h','Run-NrIdentity.ps1','nr_identity_mutations.json')
try {
    if (!(Test-Path -LiteralPath (Join-Path $repo 'OptiScaler.sln') -PathType Leaf)) { throw 'Repository root check failed.' }
    $run=New-NrOwnedRunDirectory $OutputDirectory $repo
    $state=@{run=$run;repository=$repo;commands=[Collections.Generic.List[object]]::new()
        infrastructure=[Collections.Generic.List[object]]::new()}
    $results=[Collections.Generic.List[object]]::new()
    $before=@();$sourceStable=$false;$failure=$null;$compiler=$null;$compilerIdentity=$null
    try {
        if ($env:CL -or $env:_CL_) { throw 'Implicit CL/_CL_ options must be reconciled and cleared explicitly.' }
        if ($env:VSCMD_ARG_TGT_ARCH -ne 'x64') { throw 'A reconciled Visual Studio x64 environment is required.' }
        $compiler=(Get-Command cl.exe -CommandType Application -ErrorAction Stop).Source
        $compilerIdentity=[ordered]@{path=$compiler;hash=(Get-NrHashReceipt $compiler)
            file_version=(Get-Item -LiteralPath $compiler).VersionInfo.FileVersion
            vcvars_declared_path=$env:NR_LIFE001_VCVARS64;visual_studio_version=$env:VisualStudioVersion
            windows_sdk_version=$env:WindowsSDKVersion;include_environment=$env:INCLUDE;lib_environment=$env:LIB}
        if (!(Save-NrStage $state 'toolchain.json' $compilerIdentity)) { throw 'Toolchain evidence unavailable.' }
        $sources=@($contractHeaders|ForEach-Object{Join-Path $repo ('OptiScaler\nr\contracts\'+$_)})
        $sources+=@($headers|ForEach-Object{Join-Path $repo ('OptiScaler\nr\lifecycle\'+$_)})
        $sources+=@($tests|ForEach-Object{Join-Path $PSScriptRoot ($_+'.cpp')})
        $sources+=@($extraSources|ForEach-Object{Join-Path $PSScriptRoot $_})
        $sources+=@(Join-Path $shared 'NativeProcessGroup.ps1')
        $sources+=@(Join-Path $shared 'CommandEvidence.ps1')
        $sources+=@(Join-Path $repo 'OptiScaler\OptiScaler.vcxproj')
        $sources+=@(Join-Path $repo 'OptiScaler\OptiScaler.vcxproj.filters')
        $before=@($sources|ForEach-Object{Get-NrHashReceipt $_})
        if (!(Save-NrStage $state 'sources-before.json' $before) -or @($before|Where-Object{$_.state -ne 'Recorded'}).Count) {
            throw 'Required source bytes could not be recorded before commands.'
        }
        $base=@('/nologo','/std:c++20','/EHsc','/W4','/WX','/Y-','/UNDEBUG','/Zc:__cplusplus',
            '/I',(Join-Path $repo 'OptiScaler'))
        foreach ($name in $tests) {
            $exe=Join-Path $run.path ($name+'.exe');$obj=Join-Path $run.path ($name+'.obj')
            if ((Test-Path -LiteralPath $exe) -or (Test-Path -LiteralPath $obj)) { throw 'Unexpected preexisting compiler output.' }
            # MSVC /Od fixture entry frames retain many independent records (measured up to 2.35 MiB).
            # This bounded standalone-test reserve does not qualify production-thread stack usage.
            $argv=$base+@((Join-Path $PSScriptRoot ($name+'.cpp')),('/Fe:'+ $exe),('/Fo:'+ $obj),'/link','/STACK:8388608')
            $built=Invoke-NrRecordedCommand $state ($name+'-compile') $compiler $argv $TimeoutSeconds
            $objectHash=Get-NrHashReceipt $obj;$binaryHash=Get-NrHashReceipt $exe
            $fresh=$built -and $objectHash.state -eq 'Recorded' -and $binaryHash.state -eq 'Recorded'
            $executed=$false;$succeeded=$false
            if ($fresh) { $executed=$true;$succeeded=Invoke-NrRecordedCommand $state ($name+'-execute') $exe @() $TimeoutSeconds }
            $row=[ordered]@{group=$name;compile_succeeded=$built;object=$objectHash;executable=$binaryHash
                fresh_executable_present=$fresh;execution_attempted=$executed;execution_succeeded=$succeeded}
            $results.Add($row)
            if (!(Save-NrStage $state ($name+'.group.json') $row)) { throw 'Group evidence could not be persisted.' }
            # An ordinary contained compile/assertion failure does not suppress eligible independent groups.
            # A timeout, hash/capture/storage error, or uncertain group cleanup throws and stops instead.
        }
        foreach ($header in ($headers+@('AllCanonicalAndLife'))) {
            $label='include-'+[IO.Path]::GetFileNameWithoutExtension($header)
            $source=Join-Path $run.path ($label+'.cpp');$obj=Join-Path $run.path ($label+'.obj')
            $stream=[IO.File]::Open($source,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
            try {
                $text=if ($header -eq 'AllCanonicalAndLife') {
                    '#include <nr/contracts/ContractManifest.h>'+"`r`n"+
                    (($headers|ForEach-Object{'#include <nr/lifecycle/'+$_+'>'}) -join "`r`n")+"`r`n"
                } else { '#include <nr/lifecycle/'+$header+'>'+"`r`n" }
                $bytes=[Text.UTF8Encoding]::new($false).GetBytes($text)
                $stream.Write($bytes,0,$bytes.Length);$stream.Flush($true)
            } finally { $stream.Dispose() }
            $sourceHash=Get-NrHashReceipt $source
            if (!(Save-NrStage $state ($label+'.source.json') $sourceHash) -or $sourceHash.state -ne 'Recorded') {
                throw 'Generated include translation unit provenance is unavailable.'
            }
            $ok=Invoke-NrRecordedCommand $state $label $compiler ($base+@('/c',$source,('/Fo:'+$obj))) $TimeoutSeconds
            $row=[ordered]@{header=$header;compile_succeeded=$ok;source=$sourceHash;object=(Get-NrHashReceipt $obj)}
            $results.Add($row)
            if (!(Save-NrStage $state ($label+'.group.json') $row)) { throw 'Header evidence could not be persisted.' }
        }
    } catch { $failure=$_.Exception.ToString() }
    # Terminal aggregation cannot erase per-attempt intent/results already durably written.
    $after=@();$sourceStable=$before.Count -gt 0
    foreach ($old in $before) {
        $new=Get-NrHashReceipt $old.path;$after+=,$new
        if ($new.state -ne 'Recorded' -or $old.state -ne 'Recorded' -or $old.sha256 -ne $new.sha256) { $sourceStable=$false }
    }
    $null=Save-NrStage $state 'sources-after.json' $after
    $all=$null -eq $failure -and $sourceStable -and $state.infrastructure.Count -eq 0 -and
        $state.commands.Count -eq (2*$tests.Count+$headers.Count+1)
    foreach ($command in $state.commands) { if (!$command.command_succeeded) { $all=$false } }
    foreach ($row in $results) {
        if (!$row.compile_succeeded -or $row.object.state -ne 'Recorded') { $all=$false }
        if ($row.Contains('execution_succeeded') -and !$row.execution_succeeded) { $all=$false }
    }
    $artifacts=@()
    try {
        foreach ($file in Get-ChildItem -LiteralPath $run.path -File -Force) {
            if ($file.Name -eq 'run-owner.json') { continue } # Held exclusive until this run ends.
            $hash=Get-NrHashReceipt $file.FullName;$artifacts+=,$hash
            if ($hash.state -ne 'Recorded') { $all=$false }
        }
    } catch { $state.infrastructure.Add(@{stage='terminal-inventory';error=$_.Exception.ToString()});$all=$false }
    $manifest=[ordered]@{artifact_kind='LIFE001_COMMAND_EVIDENCE_NOT_GATE_APPROVAL';schema_version=2
        run_id=$run.run_id;output_physical=$run.physical;repository=$repo;toolchain=$compilerIdentity
        source_files_before=$before;source_files_after=$after;source_files_unchanged=$sourceStable
        commands=$state.commands.ToArray();groups=$results.ToArray();artifacts=$artifacts
        infrastructure=$state.infrastructure.ToArray();terminal_exception=$failure
        all_scheduled_commands_succeeded=$all
        protocol_semantic_verdict=$null;gpu_resource_verdict=$null;temporal_iq_verdict=$null;performance_latency_verdict=$null
        completion_receipt_required='run-completion.json with matching run ID and terminal manifest hash'
        missing_coverage=@('Inherited Windows supervisor fault matrix not inferred from this run',
            'Full live-checkout and baseline provenance evidence','Production mutation sensitivity not executed by this runner',
            'Owner bootstrap/wiring and contention/lifetime integration proof','Actual target stack/copy cost acceptance',
            'GPU/resource/temporal-IQ/game/performance qualification','NR-TEST-002 remains a separate work order')
        gate_approval='NOT_PERFORMED';next_work_order_started=$false}
    $terminalSaved=Save-NrStage $state 'command-evidence.json' $manifest
    $terminalHash=Get-NrHashReceipt (Join-Path $run.path 'command-evidence.json')
    $closed=$false
    if ($terminalSaved -and $terminalHash.state -eq 'Recorded') {
        $closed=Save-NrStage $state 'run-completion.json' ([ordered]@{run_id=$run.run_id;terminal_manifest=$terminalHash
            command_summary_success=$all;closed_utc=[DateTime]::UtcNow.ToString('o');gate_approval='NOT_PERFORMED'})
    }
    Write-Host ('LIFE candidate evidence: '+$run.path)
    $exitStatus=if ($closed -and $all) {0} else {1}
} catch {
    Write-Error -ErrorAction Continue ('Infrastructure failure; no successful execution claim: '+$_.Exception.ToString())
    $exitStatus=2
} finally { if ($run -and $run.lock) { $run.lock.Dispose() } }
exit $exitStatus
