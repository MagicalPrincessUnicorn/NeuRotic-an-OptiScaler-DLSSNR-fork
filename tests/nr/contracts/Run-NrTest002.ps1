#requires -Version 5.1
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][ValidateNotNullOrEmpty()][string]$OutputDirectory,
    [ValidateRange(10,3600)][int]$TimeoutSeconds=300,
    [ValidateNotNullOrEmpty()][string]$PythonExecutable='python.exe',
    [switch]$MutationRawOnly
)
# NEW candidate, not executed in Chat. Apply ARCH -> LIFE -> TEST only after independent review.
# MutationRawOnly changes TEST reporting/compile flags only; it never applies a mutation.
# Source/recipe mutation execution requires separate authorization and an isolated machine artifact.
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'NativeProcessGroup.ps1')
. (Join-Path $PSScriptRoot 'CommandEvidence.ps1')
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$run=$null; $exitStatus=2
$groups=@(
    @{name='nr_test002_contracts';source='tests\nr\contracts\nr_test002_contracts.cpp'},
    @{name='nr_test002_schema';source='tests\nr\contracts\nr_test002_schema.cpp'},
    @{name='nr_test002_identity';source='tests\nr\contracts\nr_test002_identity.cpp'},
    @{name='nr_test002_analytics';source='tests\nr\oracles\nr_test002_analytics.cpp'}
)
$contractHeaders=@('C01_Acquisition.h','C02_Context.h','C03_Consumption.h','C04_Capability.h','C05_Routing.h',
    'C06_ProtocolInput.h','C07_ProtocolOutput.h','C08_Reset.h','C09_Multipass.h','C10_Gap.h','C11_Diagnostics.h',
    'C12_Representation.h','C13_FinalFrame.h','C14_Identity.h','C15_Validation.h','C16_Execution.h','ContractCommon.h',
    'ContractManifest.h','Evidence.h','FixtureCodec.h','Identity.h','Reasons.h','SemanticDescriptions.h')
$lifeHeaders=@('IdentityAssociations.h','IdentityDependencies.h','IdentityIssuers.h','IdentityPrimitives.h','IdentityTransitions.h')
try {
    if (!(Test-Path -LiteralPath (Join-Path $repo 'OptiScaler.sln') -PathType Leaf)) { throw 'Source root check failed.' }
    $run=New-NrOwnedRunDirectory $OutputDirectory $repo
    $state=@{run=$run;repository=$repo;commands=[Collections.Generic.List[object]]::new();infrastructure=[Collections.Generic.List[object]]::new()}
    $before=@();$rows=[Collections.Generic.List[object]]::new();$failure=$null;$toolchain=$null;$sourceStable=$false
    try {
        if ($env:CL -or $env:_CL_) { throw 'Implicit CL/_CL_ options must be reconciled and cleared.' }
        if ($env:VSCMD_ARG_TGT_ARCH -ne 'x64') { throw 'Reconciled VS x64 environment required.' }
        $compiler=(Get-Command cl.exe -CommandType Application -ErrorAction Stop).Source
        $python=(Get-Command $PythonExecutable -CommandType Application -ErrorAction Stop).Source
        $toolchain=[ordered]@{compiler=$compiler;compiler_hash=(Get-NrHashReceipt $compiler)
            compiler_file_version=(Get-Item -LiteralPath $compiler).VersionInfo.FileVersion
            python=$python;python_hash=(Get-NrHashReceipt $python);vs_version=$env:VisualStudioVersion
            windows_sdk=$env:WindowsSDKVersion;include_environment=$env:INCLUDE;lib_environment=$env:LIB
            mutation_raw_only=[bool]$MutationRawOnly;live_git_identity='NOT_VERIFIED_BY_THIS_RUNNER'}
        if (!(Save-NrStage $state 'toolchain.json' $toolchain)) { throw 'Toolchain receipt unavailable.' }
        $sources=@($contractHeaders|ForEach-Object{Join-Path $repo ('OptiScaler\nr\contracts\'+$_)})
        $sources+=@($lifeHeaders|ForEach-Object{Join-Path $repo ('OptiScaler\nr\lifecycle\'+$_)})
        $sources+=@($groups|ForEach-Object{Join-Path $repo $_.source})
        foreach ($file in @('tests\nr\contracts\Test002Support.h','tests\nr\contracts\Test002WireFixtures.h',
            'tests\nr\oracles\Test002AnalyticCases.h','tests\nr\contracts\nr_test002_inventory.json',
            'tests\nr\contracts\nr_test002_mutations.json','tests\nr\contracts\test002_source_and_evidence.py',
            'tests\nr\contracts\Run-NrTest002.ps1','tests\Run-NrTest002.cmd',
            'tests\nr\contracts\CommandEvidence.ps1','tests\nr\contracts\NativeProcessGroup.ps1',
            'tests\nr\identity\IdentityTestSupport.h','OptiScaler\dlssnr\NativeTemporalInputs.h',
            'OptiScaler\shaders\dlssnr\DlssNr_Common.h','OptiScaler\OptiScaler.vcxproj',
            'OptiScaler\OptiScaler.vcxproj.filters','OptiScaler.sln')) { $sources+=Join-Path $repo $file }
        $before=@($sources|ForEach-Object{Get-NrHashReceipt $_})
        if (!(Save-NrStage $state 'sources-before.json' $before) -or @($before|Where-Object{$_.state -ne 'Recorded'}).Count) {
            throw 'Required source hash unavailable.'
        }
        $script=Join-Path $PSScriptRoot 'test002_source_and_evidence.py'
        $sourceAudit=Invoke-NrRecordedCommand $state 'source-audit' $python @($script,'--source-check') $TimeoutSeconds
        if (!$sourceAudit) { throw 'Static source checks failed; candidate cannot proceed silently.' }
        $base=@('/nologo','/std:c++20','/EHsc','/W4','/WX','/Y-','/UNDEBUG','/Zc:__cplusplus','/I',(Join-Path $repo 'OptiScaler'))
        if ($MutationRawOnly) { $base+= '/DNR_TEST002_MUTATION_RAW_ONLY' }
        foreach ($group in $groups) {
            $name=$group.name;$exe=Join-Path $run.path ($name+'.exe');$obj=Join-Path $run.path ($name+'.obj')
            if ((Test-Path -LiteralPath $exe) -or (Test-Path -LiteralPath $obj)) { throw 'Unexpected compiler output already exists.' }
            $compileArguments=$base+@((Join-Path $repo $group.source),('/Fe:'+$exe),('/Fo:'+$obj))
            # The identity fixture retains many independent value records in one /Od entry frame.
            # Keep assertions and warnings enabled; reserve bounded test-host stack only.
            if ($name -eq 'nr_test002_identity') { $compileArguments+=@('/link','/STACK:8388608') }
            $built=Invoke-NrRecordedCommand $state ($name+'-compile') $compiler $compileArguments $TimeoutSeconds
            $binary=Get-NrHashReceipt $exe;$object=Get-NrHashReceipt $obj
            $fresh=$built -and $binary.state -eq 'Recorded' -and $object.state -eq 'Recorded'
            $executed=$false;$succeeded=$false;$framed=$false;$stdout=$null
            if ($fresh) {
                $executed=$true
                $succeeded=Invoke-NrRecordedCommand $state ($name+'-execute') $exe @($run.run_id) $TimeoutSeconds
                $stdout=$state.commands[$state.commands.Count-1].intent.stdout
                $arguments=@($script,'--verify-log',$stdout,'--run-dir',$run.path,'--suite',$name,'--run-id',$run.run_id)
                if ($MutationRawOnly) { $arguments+='--mutation-raw-only' }
                $framed=Invoke-NrRecordedCommand $state ($name+'-evidence') $python $arguments $TimeoutSeconds
            }
            $row=[ordered]@{group=$name;compile_succeeded=$built;object=$object;executable=$binary
                execution_attempted=$executed;execution_succeeded=$succeeded;evidence_framing_succeeded=$framed;stdout=$stdout}
            $rows.Add($row)
            if (!(Save-NrStage $state ($name+'.group.json') $row)) { throw 'Group receipt unavailable.' }
            # Ordinary contained compile/assertion failures do not suppress independent groups.
            # Shared infrastructure exceptions stop all subsequent execution instead.
        }
    } catch { $failure=$_.Exception.ToString() }
    $after=@();$sourceStable=$before.Count -gt 0
    foreach ($old in $before) {
        $new=Get-NrHashReceipt $old.path;$after+=,$new
        if ($new.state -ne 'Recorded' -or $old.state -ne 'Recorded' -or $new.sha256 -ne $old.sha256) { $sourceStable=$false }
    }
    $null=Save-NrStage $state 'sources-after.json' $after
    $all=$null -eq $failure -and $sourceStable -and $state.infrastructure.Count -eq 0 -and $state.commands.Count -eq 13
    foreach ($cmd in $state.commands) { if (!$cmd.command_succeeded) { $all=$false } }
    foreach ($row in $rows) { if (!$row.compile_succeeded -or !$row.execution_succeeded -or !$row.evidence_framing_succeeded) { $all=$false } }
    $artifacts=@()
    try {
        foreach ($file in Get-ChildItem -LiteralPath $run.path -File -Force) {
            if ($file.Name -eq 'run-owner.json') { continue }
            $hash=Get-NrHashReceipt $file.FullName;$artifacts+=,$hash
            if ($hash.state -ne 'Recorded') { $all=$false }
        }
    } catch { $state.infrastructure.Add(@{stage='terminal-inventory';error=$_.Exception.ToString()});$all=$false }
    $manifest=[ordered]@{artifact_kind='TEST002_COMMAND_EVIDENCE_NOT_GATE_APPROVAL';schema_version=1
        run_id=$run.run_id;repository=$repo;output_physical=$run.physical;toolchain=$toolchain
        sources_before=$before;sources_after=$after;source_files_unchanged=$sourceStable;groups=$rows.ToArray()
        commands=$state.commands.ToArray();infrastructure=$state.infrastructure.ToArray();terminal_exception=$failure;artifacts=$artifacts
        all_scheduled_commands_succeeded=$all;mutation_raw_only=[bool]$MutationRawOnly
        production_mutations_applied=$false;complete_test002_semantic_qualification=$false
        missing_coverage=@('CTX T05 qualification and 18 analytic bindings remain unbound',
            'DIAG operational non-interference and transport not implemented by TEST',
            'Mutation survival/kills require baseline and mutant evidence for the intended assertion',
            'Inherited ARCH/LIFE suites and Windows infrastructure matrix are separate runs',
            'W00 live commit/toolchain/source closure reconciliation',
            'Windows full regression, GPU/resources, temporal IQ, game compatibility, performance')
        gate_approval='NOT_PERFORMED';next_work_order_started=$false}
    $saved=Save-NrStage $state 'command-evidence.json' $manifest
    $terminal=Get-NrHashReceipt (Join-Path $run.path 'command-evidence.json')
    $closed=$false
    if ($saved -and $terminal.state -eq 'Recorded') {
        $closed=Save-NrStage $state 'run-completion.json' ([ordered]@{run_id=$run.run_id;terminal_manifest=$terminal
            command_summary_success=$all;coverage_complete=$false;closed_utc=[DateTime]::UtcNow.ToString('o');gate_approval='NOT_PERFORMED'})
    }
    Write-Host ('TEST-002 command evidence: '+$run.path+'; no gate or whole-work-order PASS is implied.')
    $exitStatus=if ($closed -and $all) {0} else {1}
} catch {
    Write-Error -ErrorAction Continue ('Infrastructure failure: '+$_.Exception.ToString());$exitStatus=2
} finally {
    if ($run -and $run.lock) { $run.lock.Dispose() }
}
exit $exitStatus
