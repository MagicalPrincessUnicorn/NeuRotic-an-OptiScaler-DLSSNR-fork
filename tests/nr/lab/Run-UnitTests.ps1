[CmdletBinding()]
param(
    [string]$OutputRoot=('C:/NeuRotic/builds/lab-development/unit-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff')),
    [string]$PackageRoot,
    [string]$P1FixtureRoot
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
if ($PSVersionTable.PSVersion.Major -lt 7) { throw 'TEST_BUILD_REQUIRES_POWERSHELL_7' }
if (![IO.Path]::IsPathFullyQualified($OutputRoot) -or $OutputRoot -match '["%!\r\n&|<>^]') { throw 'ABSOLUTE_SAFE_OUTPUT_REQUIRED' }
$root=[IO.Path]::GetFullPath($OutputRoot)
if (Test-Path -LiteralPath $root) { throw 'FRESH_OUTPUT_REQUIRED' }
$repo=(Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../../..')).ProviderPath
if ($repo -match '["%!\r\n&|<>^]') { throw 'SAFE_SOURCE_PATH_REQUIRED' }
foreach ($optional in @($PackageRoot,$P1FixtureRoot)) {
    if ($optional -and (![IO.Path]::IsPathFullyQualified($optional) -or !(Test-Path -LiteralPath $optional -PathType Container))) { throw 'ABSOLUTE_EXISTING_OPTIONAL_ROOT_REQUIRED' }
}
if ($PackageRoot) {
    foreach ($name in @('PACKAGE_MANIFEST.json','LabSupervisor.exe','LabSupervisorControl.exe')) {
        if (!(Test-Path -LiteralPath (Join-Path $PackageRoot $name) -PathType Leaf)) { throw "PACKAGE_INPUT_MISSING:$name" }
    }
}
$vcvars='C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvarsall.bat'
if (!(Test-Path -LiteralPath $vcvars -PathType Leaf)) { throw 'MSVC_REQUIRED' }
function Hash([string]$Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
function JsonFile([string]$Path,$Value) { [IO.File]::WriteAllText($Path,($Value | ConvertTo-Json -Depth 30)+"`n",[Text.UTF8Encoding]::new($false)) }
function Quoted([string]$Path) { '"'+$Path.Replace('\','/')+'"' }
$jobs=@(
    @{name='unit_supervisor';sources=@('tools/nr/lab/LabSupervisor.cpp');libs='dbghelp.lib';test=$false},
    @{name='transport';sources=@('tests/nr/lab/transport.cpp');libs='bcrypt.lib ole32.lib';test=$true},
    @{name='bound_request';sources=@('tests/nr/lab/bound_request.cpp');libs='bcrypt.lib ole32.lib';test=$true},
    @{name='controls';sources=@('tests/nr/lab/controls.cpp');libs='bcrypt.lib ole32.lib';test=$true},
    @{name='owned_d3d12';sources=@('tests/nr/lab/owned_d3d12.cpp','tools/nr/lab/OwnedD3D12.cpp');libs='d3d12.lib dxgi.lib ole32.lib';test=$true},
    @{name='owned_validation';sources=@('tests/nr/lab/owned_validation.cpp','tools/nr/lab/OwnedD3D12.cpp');libs='bcrypt.lib d3d12.lib dxgi.lib ole32.lib';test=$true},
    @{name='p1_adapter';sources=@('tests/nr/lab/p1_adapter.cpp','tools/nr/lab/P1Adapter.cpp');libs='advapi32.lib';test=$true},
    @{name='controller_semantics';sources=@('tests/nr/lab/controller_semantics.cpp','tools/nr/lab/P1Adapter.cpp');libs='bcrypt.lib ole32.lib dxgi.lib d3d12.lib advapi32.lib';test=$true},
    @{name='export';sources=@('tests/nr/lab/export.cpp');libs='bcrypt.lib ole32.lib';test=$true}
)
$pending=[Collections.Generic.Queue[string]]::new()
$sources=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($job in $jobs) { foreach ($name in $job.sources) { $pending.Enqueue((Join-Path $repo $name)) } }
while ($pending.Count) {
    $path=[IO.Path]::GetFullPath($pending.Dequeue())
    if (!$sources.Add($path)) { continue }
    if (!(Test-Path -LiteralPath $path -PathType Leaf)) { throw "SOURCE_NOT_READY:$path" }
    foreach ($match in [regex]::Matches([IO.File]::ReadAllText($path),'(?m)^\s*#\s*include\s*([<"])([^>"\r\n]+)[>"]')) {
        $name=$match.Groups[2].Value
        $candidates=@((Join-Path ([IO.Path]::GetDirectoryName($path)) $name),(Join-Path (Join-Path $repo 'external') $name),(Join-Path $repo $name))
        $found=$false
        foreach ($candidate in $candidates) {
            if (Test-Path -LiteralPath $candidate -PathType Leaf) {
                $full=[IO.Path]::GetFullPath($candidate)
                if ([IO.Path]::GetRelativePath($repo,$full).StartsWith('..')) { throw 'INCLUDE_ESCAPES_SOURCE' }
                $pending.Enqueue($full);$found=$true;break
            }
        }
        if (!$found -and $match.Groups[1].Value -eq '"') { throw "LOCAL_INCLUDE_UNRESOLVED:${path}:$name" }
    }
}
foreach ($name in @('tests/nr/lab/Run-UnitTests.ps1','tests/nr/lab/Test-LabSupervisor.ps1','tools/nr/p1/Test-Supervisor.ps1')) { [void]$sources.Add((Join-Path $repo $name)) }
foreach ($file in Get-ChildItem -LiteralPath (Join-Path $repo 'tools/nr/lab/design') -File -Recurse) { [void]$sources.Add($file.FullName) }
New-Item -ItemType Directory -Path $root,(Join-Path $root 'source'),(Join-Path $root 'bin'),(Join-Path $root 'objects'),(Join-Path $root 'logs'),(Join-Path $root 'results'),(Join-Path $root 'processes') | Out-Null
$snapshot=Join-Path $root 'source'
$sourceRecords=@()
foreach ($path in ($sources | Sort-Object)) {
    $relative=[IO.Path]::GetRelativePath($repo,$path).Replace('\','/')
    $hash=Hash $path;$copy=Join-Path $snapshot $relative
    New-Item -ItemType Directory -Force -Path ([IO.Path]::GetDirectoryName($copy)) | Out-Null
    Copy-Item -LiteralPath $path -Destination $copy
    if ((Hash $copy) -cne $hash -or (Hash $path) -cne $hash) { throw "SOURCE_CHANGED_DURING_SNAPSHOT:$relative" }
    $sourceRecords += [ordered]@{path=$relative;sha256=$hash;bytes=(Get-Item -LiteralPath $copy).Length}
}
foreach ($record in $sourceRecords) { if ((Hash (Join-Path $repo $record.path)) -cne $record.sha256) { throw "SOURCE_CHANGED_DURING_SNAPSHOT:$($record.path)" } }
$manifest=[ordered]@{schema_version=1;role='UNIT_TEST_SOURCE_BUILD';build_id=[Guid]::NewGuid().ToString('D');snapshot_utc=[DateTime]::UtcNow.ToString('o');head=(& git -C $repo rev-parse HEAD).Trim();branch=(& git -C $repo branch --show-current).Trim();dirty_state=@(& git -C $repo status --porcelain);toolchain=@{version='MSVC 14.44.35207';architecture='x64';flags='/MT /W4 /EHsc /O2 /std:c++20';vcvars_sha256=(Hash $vcvars)};sources=$sourceRecords;builds=@();package_root=$PackageRoot;package_manifest_sha256=if($PackageRoot){Hash (Join-Path $PackageRoot 'PACKAGE_MANIFEST.json')}else{$null};p1_parity_fixtures=@();provider_execution='NOT_RUN';physical_execution='NOT_RUN';warp_execution='EXPLICIT_SOFTWARE_ONLY'}
if ($P1FixtureRoot) {
    foreach ($file in Get-ChildItem -LiteralPath $P1FixtureRoot -Recurse -File) {
        $manifest.p1_parity_fixtures += @{path=[IO.Path]::GetRelativePath($P1FixtureRoot,$file.FullName).Replace('\','/');sha256=(Hash $file.FullName)}
    }
}
JsonFile (Join-Path $root 'SOURCE_BUILD_MANIFEST.json') $manifest
$buildOk=@{}
foreach ($job in $jobs) {
    $objects=Join-Path (Join-Path $root 'objects') $job.name
    New-Item -ItemType Directory -Path $objects | Out-Null
    $binary=Join-Path (Join-Path $root 'bin') ($job.name+'.exe')
    $sourceArgs=($job.sources | ForEach-Object { Quoted (Join-Path $snapshot $_) }) -join ' '
    $command='cl /nologo /Bv /showIncludes /O2 /MT /W4 /EHsc /utf-8 /std:c++20 /I'+(Quoted (Join-Path $snapshot 'external'))+' '+$sourceArgs+' /Fo'+(Quoted ($objects.Replace('\','/')+'/'))+' /Fe'+(Quoted $binary)+' /link '+$job.libs
    $script=Join-Path $root ('logs/build-'+$job.name+'.cmd');$log=Join-Path $root ('logs/build-'+$job.name+'.txt')
    [IO.File]::WriteAllLines($script,@('@echo off','chcp 65001 >nul',('call '+(Quoted $vcvars)+' x64 -vcvars_ver=14.44.35207'),'if errorlevel 1 exit /b 1','set CL=','set _CL_=',('cd /d '+(Quoted $objects)),$command,'exit /b %errorlevel%'),[Text.UTF8Encoding]::new($false))
    & cmd /d /c $script *> $log
    $code=$LASTEXITCODE;$buildOk[$job.name]=($code -eq 0)
    $manifest.builds += [ordered]@{suite=$job.name;command=$command;log=('logs/'+[IO.Path]::GetFileName($log));exit_code=$code;binary_sha256=if($code -eq 0){Hash $binary}else{$null}}
    JsonFile (Join-Path $root 'SOURCE_BUILD_MANIFEST.json') $manifest
    Write-Output ("BUILD {0}: exit {1}" -f $job.name,$code)
}
foreach ($record in $sourceRecords) { if ((Hash (Join-Path $snapshot $record.path)) -cne $record.sha256) { throw "SOURCE_SNAPSHOT_CHANGED:$($record.path)" } }
$outcomes=[Collections.Generic.List[object]]::new()
$supervisor=Join-Path $root 'bin/unit_supervisor.exe'
foreach ($job in $jobs | Where-Object test) {
    $name=$job.name;$data=Join-Path $root ('evidence/'+$name)
    $outcome=[ordered]@{suite=$name;status='NOT_RUN';exit_code=$null;process_status=$null;elapsed_ms=$null;command=@();reported_counts=@{};log=('logs/test-'+$name+'.txt');reason=$null}
    if (!$buildOk[$name] -or !$buildOk.unit_supervisor) { $outcome.status='FAILED_BUILD';$outcome.reason='Test or supervision binary did not compile.' }
    elseif ($name -eq 'owned_validation' -and !(@($outcomes | Where-Object { $_.suite -eq 'owned_d3d12' -and $_.status -eq 'PASS' }).Count)) { $outcome.status='BLOCKED_PREREQUISITE';$outcome.reason='Fresh same/cross WARP fixtures were not produced successfully.' }
    else {
        $testArgs=switch ($name) {
            'transport' { @() }
            'bound_request' { @($snapshot,$data) }
            'controls' { if($PackageRoot){@($data,$PackageRoot)}else{@($data)} }
            'owned_d3d12' { @($data) }
            'owned_validation' { @((Join-Path $root 'evidence/owned_d3d12'),$data) }
            'p1_adapter' { if($P1FixtureRoot){@($data,$P1FixtureRoot)}else{@($data)} }
            'controller_semantics' { @($data) }
            'export' { @($data) }
        }
        $receipt=Join-Path $root ('processes/'+$name);New-Item -ItemType Directory -Path $receipt | Out-Null
        $arguments=@('--exe',(Join-Path $root ('bin/'+$name+'.exe')),'--receipt',$receipt,'--timeout-ms','30000','--')+@($testArgs)
        $outcome.command=@($supervisor)+$arguments;$watch=[Diagnostics.Stopwatch]::StartNew()
        & $supervisor @arguments *> (Join-Path $root ('logs/supervisor-'+$name+'.txt'))
        $supervisorExit=$LASTEXITCODE;$watch.Stop();$outcome.elapsed_ms=$watch.ElapsedMilliseconds
        $text=''
        foreach ($stream in @('runner-stdout.txt','runner-stderr.txt')) { $path=Join-Path $receipt $stream;if(Test-Path -LiteralPath $path){$text += [IO.File]::ReadAllText($path)} }
        [IO.File]::WriteAllText((Join-Path $root $outcome.log),$text,[Text.UTF8Encoding]::new($false))
        try {
            $process=Get-Content -LiteralPath (Join-Path $receipt 'supervisor.json') -Raw | ConvertFrom-Json
            $capture=Get-Content -LiteralPath (Join-Path $receipt 'lab-supervision.json') -Raw | ConvertFrom-Json
            $outcome.exit_code=$process.child_exit_code;$outcome.process_status=$process.process_status
            if ($supervisorExit -eq 0 -and $process.process_status -eq 'exited' -and $process.child_exit_code -eq 0 -and $process.child_drained -and $capture.capture_complete -and !$capture.stdout.truncated -and !$capture.stderr.truncated) { $outcome.status='PASS' }
            else { $outcome.status='FAIL';$outcome.reason='Nonzero test/supervisor exit, incomplete capture, or incomplete process drain.' }
        } catch { $outcome.status='FAIL';$outcome.reason=$_.Exception.Message }
        foreach ($pattern in @('checks=(\d+)','NATIVE_CASES=(\d+)','PARITY_CASES=(\d+)','(\d+) evidence mutants rejected','ENTRIES=(\d+)')) {
            if ($text -match $pattern) { $outcome.reported_counts[$pattern]=[int]$Matches[1] }
        }
        if ($name -eq 'controls' -and (Test-Path -LiteralPath $data)) {
            $count=0;foreach ($file in Get-ChildItem -LiteralPath $data -File -Filter 'NLAB-CTL-*.json'){$control=Get-Content -LiteralPath $file.FullName -Raw | ConvertFrom-Json;$count+=@($control.checks).Count};$outcome.reported_counts.control_checks=$count
            $outcome.reported_counts.unavailable_case_refusals=3
        }
        if ($outcome.status -ne 'PASS') { Write-Output $text }
    }
    JsonFile (Join-Path $root ('results/'+$name+'.json')) $outcome;$outcomes.Add($outcome)
    Write-Output ("TEST {0}: {1}" -f $name,$outcome.status)
}
$packageOutcome=[ordered]@{suite='packaged_supervisor';status='NOT_RUN';exit_code=$null;reported_counts=@{};log='logs/test-packaged-supervisor.txt';reason='PackageRoot not supplied; no packaged-binary claim.'}
if ($PackageRoot) {
    $watch=[Diagnostics.Stopwatch]::StartNew()
    & (Get-Process -Id $PID).Path -NoProfile -File (Join-Path $snapshot 'tests/nr/lab/Test-LabSupervisor.ps1') -Supervisor (Join-Path $PackageRoot 'LabSupervisor.exe') -Control (Join-Path $PackageRoot 'LabSupervisorControl.exe') -EvidenceRoot (Join-Path $root 'evidence/packaged-supervisor') *> (Join-Path $root $packageOutcome.log)
    $packageOutcome.exit_code=$LASTEXITCODE;$watch.Stop();$packageOutcome.elapsed_ms=$watch.ElapsedMilliseconds
    $packageOutcome.status=if($packageOutcome.exit_code -eq 0){'PASS'}else{'FAIL'};$packageOutcome.reason=$null
    $text=Get-Content -LiteralPath (Join-Path $root $packageOutcome.log) -Raw
    foreach($pattern in @('LAB_SUPERVISOR_CONTROLS_VERIFIED=(\d+)','P1_REGRESSIONS_VERIFIED=(\d+)')){if($text -match $pattern){$packageOutcome.reported_counts[$pattern]=[int]$Matches[1]}}
    if($packageOutcome.status -ne 'PASS'){Write-Output $text}
}
JsonFile (Join-Path $root 'results/packaged_supervisor.json') $packageOutcome;$outcomes.Add($packageOutcome)
if (!$PackageRoot) {
    $unrun=[ordered]@{suite='package_control_extensions';status='NOT_RUN';reason='PackageRoot not supplied; CTL-001/CTL-003 package-specific controls were not invoked.'}
    JsonFile (Join-Path $root 'results/package_control_extensions.json') $unrun;$outcomes.Add($unrun)
}
$summary=[ordered]@{schema_version=1;completed_utc=[DateTime]::UtcNow.ToString('o');source_build_manifest_sha256=(Hash (Join-Path $root 'SOURCE_BUILD_MANIFEST.json'));suite_count=$outcomes.Count;passed=@($outcomes | Where-Object status -eq 'PASS').Count;failed=@($outcomes | Where-Object { $_.status -in @('FAIL','FAILED_BUILD') }).Count;blocked=@($outcomes | Where-Object status -eq 'BLOCKED_PREREQUISITE').Count;not_run=@($outcomes | Where-Object status -eq 'NOT_RUN').Count;outcomes=$outcomes;provider_execution='NOT_RUN';physical_execution='NOT_RUN'}
JsonFile (Join-Path $root 'UNIT_TEST_RESULTS.json') $summary
Write-Output ("UNIT_TEST_RESULTS={0}; PASS={1}; FAIL={2}; BLOCKED={3}; NOT_RUN={4}" -f (Join-Path $root 'UNIT_TEST_RESULTS.json'),$summary.passed,$summary.failed,$summary.blocked,$summary.not_run)
if ($summary.failed -or $summary.blocked) { throw 'UNIT_SUITES_FAILED_SEE_RESULTS_AND_LOGS' }
