#requires -Version 5.1
# NEW candidate test infrastructure. Not executed during Chat preparation.
# Native declarations are emitted in-process; no Add-Type/csc bootstrap process is launched.
# Windows x64 only. This supervises trusted compiler/test CreateProcess descendants, not services,
# WMI/broker-launched work, hostile same-user processes, or an arbitrary executable sandbox.
Set-StrictMode -Version Latest

function Initialize-NrNative {
    if (Get-Variable -Name NrNative -Scope Script -ErrorAction SilentlyContinue) { return }
    if ($PSVersionTable.PSEdition -ne 'Desktop' -or $PSVersionTable.PSVersion.Major -ne 5) { throw 'Windows PowerShell 5.1 Desktop is required; pwsh/Core is not qualified.' }
    if ([IntPtr]::Size -ne 8) { throw 'The candidate supervisor requires a 64-bit PowerShell host.' }
    $assemblyName = [Reflection.AssemblyName]::new('NrArchCandidateNative-' + [Guid]::NewGuid().ToString('N'))
    $assembly = [AppDomain]::CurrentDomain.DefineDynamicAssembly($assemblyName, [Reflection.Emit.AssemblyBuilderAccess]::Run)
    $module = $assembly.DefineDynamicModule($assemblyName.Name)
    function Define-NrStruct([string]$Name, [System.Collections.IDictionary]$Fields) {
        $type = $module.DefineType($Name, [Reflection.TypeAttributes]'Public,Sealed,SequentialLayout', [ValueType])
        foreach ($field in $Fields.GetEnumerator()) {
            $null = $type.DefineField($field.Key, $field.Value, [Reflection.FieldAttributes]::Public)
        }
        return $type.CreateType()
    }
    $script:NrSA = Define-NrStruct 'NR_SECURITY_ATTRIBUTES' ([ordered]@{
        nLength=[uint32]; lpSecurityDescriptor=[IntPtr]; bInheritHandle=[int32]
    })
    $script:NrSI = Define-NrStruct 'NR_STARTUPINFOW' ([ordered]@{
        cb=[uint32]; lpReserved=[IntPtr]; lpDesktop=[IntPtr]; lpTitle=[IntPtr]
        dwX=[uint32]; dwY=[uint32]; dwXSize=[uint32]; dwYSize=[uint32]
        dwXCountChars=[uint32]; dwYCountChars=[uint32]; dwFillAttribute=[uint32]; dwFlags=[uint32]
        wShowWindow=[uint16]; cbReserved2=[uint16]; lpReserved2=[IntPtr]
        hStdInput=[IntPtr]; hStdOutput=[IntPtr]; hStdError=[IntPtr]
    })
    $script:NrSIEX = Define-NrStruct 'NR_STARTUPINFOEXW' ([ordered]@{ StartupInfo=$script:NrSI; lpAttributeList=[IntPtr] })
    $script:NrPI = Define-NrStruct 'NR_PROCESS_INFORMATION' ([ordered]@{
        hProcess=[IntPtr]; hThread=[IntPtr]; dwProcessId=[uint32]; dwThreadId=[uint32]
    })
    $script:NrBasicLimits = Define-NrStruct 'NR_JOB_BASIC_LIMITS' ([ordered]@{
        PerProcessUserTimeLimit=[int64]; PerJobUserTimeLimit=[int64]; LimitFlags=[uint32]
        MinimumWorkingSetSize=[UIntPtr]; MaximumWorkingSetSize=[UIntPtr]; ActiveProcessLimit=[uint32]
        Affinity=[UIntPtr]; PriorityClass=[uint32]; SchedulingClass=[uint32]
    })
    $script:NrIoCounters = Define-NrStruct 'NR_IO_COUNTERS' ([ordered]@{
        ReadOperationCount=[uint64]; WriteOperationCount=[uint64]; OtherOperationCount=[uint64]
        ReadTransferCount=[uint64]; WriteTransferCount=[uint64]; OtherTransferCount=[uint64]
    })
    $script:NrJobLimits = Define-NrStruct 'NR_JOB_EXTENDED_LIMITS' ([ordered]@{
        BasicLimitInformation=$script:NrBasicLimits; IoInfo=$script:NrIoCounters
        ProcessMemoryLimit=[UIntPtr]; JobMemoryLimit=[UIntPtr]; PeakProcessMemoryUsed=[UIntPtr]; PeakJobMemoryUsed=[UIntPtr]
    })
    $script:NrJobAccounting = Define-NrStruct 'NR_JOB_ACCOUNTING' ([ordered]@{
        TotalUserTime=[int64]; TotalKernelTime=[int64]; ThisPeriodTotalUserTime=[int64]; ThisPeriodTotalKernelTime=[int64]
        TotalPageFaultCount=[uint32]; TotalProcesses=[uint32]; ActiveProcesses=[uint32]; TotalTerminatedProcesses=[uint32]
    })
    $native = $module.DefineType('NR_NATIVE', [Reflection.TypeAttributes]'Public,Abstract,Sealed')
    function Define-NrImport([string]$Name, [Type]$Result, [Type[]]$Parameters) {
        $method = $native.DefinePInvokeMethod($Name, 'kernel32.dll',
            [Reflection.MethodAttributes]'Public,Static,PinvokeImpl', [Reflection.CallingConventions]::Standard,
            $Result, $Parameters, [Runtime.InteropServices.CallingConvention]::Winapi,
            [Runtime.InteropServices.CharSet]::Unicode)
        $attributeType = [Runtime.InteropServices.DllImportAttribute]
        $attribute = [Reflection.Emit.CustomAttributeBuilder]::new(
            $attributeType.GetConstructor([Type[]]@([string])), [object[]]@('kernel32.dll'),
            [Reflection.FieldInfo[]]@($attributeType.GetField('SetLastError'), $attributeType.GetField('ExactSpelling'), $attributeType.GetField('CharSet')),
            [object[]]@($true, $true, [Runtime.InteropServices.CharSet]::Unicode))
        $method.SetCustomAttribute($attribute)
        $method.SetImplementationFlags([Reflection.MethodImplAttributes]::PreserveSig)
    }
    Define-NrImport 'CreateJobObjectW' ([IntPtr]) ([Type[]]@([IntPtr],[string]))
    Define-NrImport 'SetInformationJobObject' ([int32]) ([Type[]]@([IntPtr],[int32],[IntPtr],[uint32]))
    Define-NrImport 'QueryInformationJobObject' ([int32]) ([Type[]]@([IntPtr],[int32],[IntPtr],[uint32],[IntPtr]))
    Define-NrImport 'AssignProcessToJobObject' ([int32]) ([Type[]]@([IntPtr],[IntPtr]))
    Define-NrImport 'TerminateJobObject' ([int32]) ([Type[]]@([IntPtr],[uint32]))
    Define-NrImport 'CreateProcessW' ([int32]) ([Type[]]@([string],[Text.StringBuilder],[IntPtr],[IntPtr],
        [int32],[uint32],[IntPtr],[string],[IntPtr],[IntPtr]))
    Define-NrImport 'ResumeThread' ([uint32]) ([Type[]]@([IntPtr]))
    Define-NrImport 'WaitForSingleObject' ([uint32]) ([Type[]]@([IntPtr],[uint32]))
    Define-NrImport 'GetExitCodeProcess' ([int32]) ([Type[]]@([IntPtr],[IntPtr]))
    Define-NrImport 'TerminateProcess' ([int32]) ([Type[]]@([IntPtr],[uint32]))
    Define-NrImport 'CloseHandle' ([int32]) ([Type[]]@([IntPtr]))
    Define-NrImport 'CreateFileW' ([IntPtr]) ([Type[]]@([string],[uint32],[uint32],[IntPtr],[uint32],[uint32],[IntPtr]))
    Define-NrImport 'FlushFileBuffers' ([int32]) ([Type[]]@([IntPtr]))
    Define-NrImport 'CreateDirectoryW' ([int32]) ([Type[]]@([string],[IntPtr]))
    Define-NrImport 'GetFinalPathNameByHandleW' ([uint32]) ([Type[]]@([IntPtr],[Text.StringBuilder],[uint32],[uint32]))
    Define-NrImport 'InitializeProcThreadAttributeList' ([int32]) ([Type[]]@([IntPtr],[uint32],[uint32],[IntPtr]))
    Define-NrImport 'UpdateProcThreadAttribute' ([int32]) ([Type[]]@([IntPtr],[uint32],[UIntPtr],[IntPtr],[UIntPtr],[IntPtr],[IntPtr]))
    Define-NrImport 'DeleteProcThreadAttributeList' ([void]) ([Type[]]@([IntPtr]))
    $script:NrNative = $native.CreateType()
}
function Invoke-NrNative([string]$Name, [object[]]$Arguments) {
    $result = $script:NrNative.GetMethod($Name).Invoke($null, $Arguments)
    $script:NrLastError = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
    return $result
}
function Assert-NrNative([bool]$Condition, [string]$Operation) {
    if (!$Condition) {
        $errorCode = $script:NrLastError
        throw ($Operation + ' failed; Win32=' + $errorCode)
    }
}
function Close-NrHandle([IntPtr]$Handle) {
    if ($Handle -ne [IntPtr]::Zero -and $Handle -ne [IntPtr](-1)) {
        Assert-NrNative ((Invoke-NrNative 'CloseHandle' @($Handle)) -ne 0) 'CloseHandle'
    }
}
function Quote-NrArgument([string]$Value) {
    # Windows CRT argv quoting, not CMD syntax. Empty strings and trailing slashes are preserved.
    if ($Value.Length -gt 0 -and $Value -notmatch '[\s"]') { return $Value }
    $escaped = [regex]::Replace($Value, '(\\*)"', '${1}${1}\"')
    $escaped = [regex]::Replace($escaped, '(\\+)$', '${1}${1}')
    return '"' + $escaped + '"'
}
function Get-NrPhysicalDirectory([string]$Path) {
    $handle = [IntPtr]::Zero
    try {
        $handle = Invoke-NrNative 'CreateFileW' @($Path,[uint32]0,[uint32]7,[IntPtr]::Zero,
            [uint32]3,[uint32]0x02000000,[IntPtr]::Zero)
        Assert-NrNative ($handle -ne [IntPtr](-1)) 'Open directory for physical identity'
        $buffer = [Text.StringBuilder]::new(32768)
        $count = Invoke-NrNative 'GetFinalPathNameByHandleW' @($handle,$buffer,[uint32]32768,[uint32]1)
        Assert-NrNative ($count -gt 0 -and $count -lt 32768) 'Get physical volume path'
        return $buffer.ToString().TrimEnd('\')
    } finally { Close-NrHandle $handle }
}
function Assert-NrNoReparseAncestors([string]$ExistingDirectory) {
    $path = [IO.Path]::GetFullPath($ExistingDirectory)
    while ($path) {
        $item = Get-Item -LiteralPath $path -Force -ErrorAction Stop
        if (!$item.PSIsContainer -or (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)) {
            throw 'Unsupported evidence/source ancestry: only existing non-reparse local directories are admitted.'
        }
        $parent = [IO.Directory]::GetParent($path)
        $path = if ($parent) { $parent.FullName } else { $null }
    }
}
function New-NrOwnedRunDirectory([string]$Requested, [string]$Repository) {
    Initialize-NrNative
    # Explicitly exclude UNC/device/drive-relative paths and alternate data streams.
    if ($Requested -notmatch '^[A-Za-z]:[\\/]' -or $Requested.Substring(2).Contains(':')) {
        throw 'Use a fully qualified local drive path; UNC/device/ADS/drive-relative output is unsupported.'
    }
    $out = [IO.Path]::GetFullPath($Requested).TrimEnd('\')
    $parent = [IO.Path]::GetDirectoryName($out)
    if (!$parent -or !(Test-Path -LiteralPath $parent -PathType Container)) { throw 'Output parent must already exist.' }
    Assert-NrNoReparseAncestors $Repository
    Assert-NrNoReparseAncestors $parent
    $repoPhysical = Get-NrPhysicalDirectory $Repository
    $proposedPhysical = (Get-NrPhysicalDirectory $parent) + '\' + [IO.Path]::GetFileName($out)
    if ($proposedPhysical.Equals($repoPhysical,[StringComparison]::OrdinalIgnoreCase) -or
        $proposedPhysical.StartsWith($repoPhysical + '\',[StringComparison]::OrdinalIgnoreCase)) {
        throw 'Evidence output resolves inside the source checkout.'
    }
    # Unlike Directory.CreateDirectory/Test-Path, this is one atomic exclusive reservation.
    Assert-NrNative ((Invoke-NrNative 'CreateDirectoryW' @($out,[IntPtr]::Zero)) -ne 0) 'Reserve NEW evidence directory'
    Assert-NrNoReparseAncestors $out
    $physical = Get-NrPhysicalDirectory $out
    if (!$physical.Equals($proposedPhysical,[StringComparison]::OrdinalIgnoreCase)) {
        throw 'Directory identity changed during reservation; do not reuse or delete the uncertain directory.'
    }
    $runId = [Guid]::NewGuid().ToString('N') # Test-run identity only, never a C14 identity.
    $lock = [IO.File]::Open((Join-Path $out 'run-owner.json'),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    try {
        $bytes = [Text.UTF8Encoding]::new($false).GetBytes((@{run_id=$runId;physical_directory=$physical;
            repository_physical=$repoPhysical;created_utc=[DateTime]::UtcNow.ToString('o');process_id=$PID} | ConvertTo-Json))
        $lock.Write($bytes,0,$bytes.Length); $lock.Flush($true)
        return @{ path=$out; physical=$physical; run_id=$runId; lock=$lock }
    } catch { $lock.Dispose(); throw }
}
function Invoke-NrProcessGroup([string]$Executable,[string[]]$Arguments,[string]$WorkingDirectory,
                               [string]$Stdout,[string]$Stderr,[int]$TimeoutSeconds,
                               [hashtable]$EnvironmentOverrides=@{}) {
    Initialize-NrNative
    $allocations = [Collections.Generic.List[IntPtr]]::new()
    $handles = [Collections.Generic.List[IntPtr]]::new()
    $job = [IntPtr]::Zero; $process = [IntPtr]::Zero; $thread = [IntPtr]::Zero
    $attributes = [IntPtr]::Zero; $attributeReady = $false; $assigned = $false; $created = $false
    $stdoutHandle = [IntPtr]::Zero; $stderrHandle = [IntPtr]::Zero
    $r = [ordered]@{ process_created=$false; user_code_started=$false; process_id=$null
        group_assigned_before_resume=$false; parent_exit_observed=$false; group_empty_observed=$false
        last_active_processes=$null; numeric_exit_code=$null; timed_out=$false
        termination_requested=$false; capture_complete=$false; timeout_observation=$null; errors=@(); cleanup_errors=@()
        started_utc=[DateTime]::UtcNow.ToString('o'); ended_utc=$null }
    function Allocate-Nr([int]$Bytes) {
        $ptr = [Runtime.InteropServices.Marshal]::AllocHGlobal($Bytes)
        $allocations.Add($ptr)
        [Runtime.InteropServices.Marshal]::Copy([byte[]]::new($Bytes),0,$ptr,$Bytes)
        return $ptr
    }
    function Put-NrStruct($Value) {
        $ptr = Allocate-Nr ([Runtime.InteropServices.Marshal]::SizeOf($Value))
        [Runtime.InteropServices.Marshal]::StructureToPtr($Value,$ptr,$false)
        return $ptr
    }
    $accounting = $null; $accountingPtr = [IntPtr]::Zero; $exitPtr = [IntPtr]::Zero
    try {
        $job = Invoke-NrNative 'CreateJobObjectW' @([IntPtr]::Zero,$null)
        Assert-NrNative ($job -ne [IntPtr]::Zero) 'CreateJobObject'
        $limits = [Activator]::CreateInstance($script:NrJobLimits)
        $basic = [Activator]::CreateInstance($script:NrBasicLimits)
        $basic.LimitFlags = [uint32]0x2000 # KILL_ON_JOB_CLOSE; neither breakaway permission is set.
        $limits.BasicLimitInformation = $basic
        $limitPtr = Put-NrStruct $limits
        Assert-NrNative ((Invoke-NrNative 'SetInformationJobObject' @($job,[int32]9,$limitPtr,
            [uint32][Runtime.InteropServices.Marshal]::SizeOf($limits))) -ne 0) 'Set job limits'
        $sa = [Activator]::CreateInstance($script:NrSA); $sa.nLength = [uint32][Runtime.InteropServices.Marshal]::SizeOf($sa)
        $sa.bInheritHandle = 1; $saPtr = Put-NrStruct $sa
        $stdinHandle = Invoke-NrNative 'CreateFileW' @('NUL',[uint32]2147483648,[uint32]3,$saPtr,[uint32]3,[uint32]0,[IntPtr]::Zero)
        Assert-NrNative ($stdinHandle -ne [IntPtr](-1)) 'Open null stdin'; $handles.Add($stdinHandle)
        $stdoutHandle = Invoke-NrNative 'CreateFileW' @($Stdout,[uint32]1073741824,[uint32]1,$saPtr,[uint32]1,[uint32]0,[IntPtr]::Zero)
        Assert-NrNative ($stdoutHandle -ne [IntPtr](-1)) 'Create NEW stdout'; $handles.Add($stdoutHandle)
        $stderrHandle = Invoke-NrNative 'CreateFileW' @($Stderr,[uint32]1073741824,[uint32]1,$saPtr,[uint32]1,[uint32]0,[IntPtr]::Zero)
        Assert-NrNative ($stderrHandle -ne [IntPtr](-1)) 'Create NEW stderr'; $handles.Add($stderrHandle)
        $sizePtr = Allocate-Nr 8
        $null = Invoke-NrNative 'InitializeProcThreadAttributeList' @([IntPtr]::Zero,[uint32]1,[uint32]0,$sizePtr)
        $attributeBytes = [Runtime.InteropServices.Marshal]::ReadInt64($sizePtr)
        if ($attributeBytes -le 0 -or $attributeBytes -gt 65536) { throw 'Invalid/beyond-budget native attribute size.' }
        $attributes = Allocate-Nr ([int]$attributeBytes)
        Assert-NrNative ((Invoke-NrNative 'InitializeProcThreadAttributeList' @($attributes,[uint32]1,[uint32]0,$sizePtr)) -ne 0) 'Initialize handle-list attributes'
        $attributeReady = $true
        $handleList = Allocate-Nr 24
        [Runtime.InteropServices.Marshal]::WriteIntPtr($handleList,0,$stdinHandle)
        [Runtime.InteropServices.Marshal]::WriteIntPtr($handleList,8,$stdoutHandle)
        [Runtime.InteropServices.Marshal]::WriteIntPtr($handleList,16,$stderrHandle)
        Assert-NrNative ((Invoke-NrNative 'UpdateProcThreadAttribute' @($attributes,[uint32]0,[UIntPtr]::new([uint64]0x20002),
            $handleList,[UIntPtr]::new([uint64]24),[IntPtr]::Zero,[IntPtr]::Zero)) -ne 0) 'Restrict inherited handles'
        $si = [Activator]::CreateInstance($script:NrSI); $six = [Activator]::CreateInstance($script:NrSIEX)
        $si.cb = [uint32][Runtime.InteropServices.Marshal]::SizeOf($six); $si.dwFlags = [uint32]0x100
        $si.hStdInput=$stdinHandle; $si.hStdOutput=$stdoutHandle; $si.hStdError=$stderrHandle
        $six.StartupInfo=$si; $six.lpAttributeList=$attributes; $siPtr=Put-NrStruct $six
        $pi = [Activator]::CreateInstance($script:NrPI); $piPtr=Put-NrStruct $pi
        $accounting = [Activator]::CreateInstance($script:NrJobAccounting); $accountingPtr=Put-NrStruct $accounting
        $exitPtr=Allocate-Nr 4
        $command = (@(Quote-NrArgument $Executable) + @($Arguments | ForEach-Object { Quote-NrArgument $_ })) -join ' '
        if ($command.Length -ge 32767) { throw 'Windows command-line length budget exceeded.' }
        $commandLine = [Text.StringBuilder]::new($command,32768)
        $environmentPtr=[IntPtr]::Zero; $creationFlags=[uint32]0x80004
        if ($EnvironmentOverrides.Count) {
            $environment=[Collections.Generic.SortedDictionary[string,string]]::new([StringComparer]::OrdinalIgnoreCase)
            foreach ($entry in [Environment]::GetEnvironmentVariables().GetEnumerator()) {
                $environment[[string]$entry.Key]=[string]$entry.Value
            }
            foreach ($entry in $EnvironmentOverrides.GetEnumerator()) {
                if ([string]$entry.Key -notmatch '^[A-Za-z_][A-Za-z0-9_]*$' -or [string]$entry.Value -match "`0") {
                    throw 'Invalid child environment override.'
                }
                $environment[[string]$entry.Key]=[string]$entry.Value
            }
            $environmentText=($environment.GetEnumerator() | ForEach-Object {$_.Key+'='+$_.Value}) -join "`0"
            $environmentBytes=[Text.Encoding]::Unicode.GetBytes($environmentText+"`0`0")
            $environmentPtr=Allocate-Nr $environmentBytes.Length
            [Runtime.InteropServices.Marshal]::Copy($environmentBytes,0,$environmentPtr,$environmentBytes.Length)
            $creationFlags=$creationFlags -bor [uint32]0x400 # CREATE_UNICODE_ENVIRONMENT
        }
        Assert-NrNative ((Invoke-NrNative 'CreateProcessW' @($Executable,$commandLine,[IntPtr]::Zero,[IntPtr]::Zero,
            [int32]1,$creationFlags,$environmentPtr,$WorkingDirectory,$siPtr,$piPtr)) -ne 0) 'Create suspended command'
        $created=$true; $r.process_created=$true
        $pi=[Runtime.InteropServices.Marshal]::PtrToStructure($piPtr,[Type]$script:NrPI)
        $process=$pi.hProcess; $thread=$pi.hThread; $r.process_id=$pi.dwProcessId
        Assert-NrNative ((Invoke-NrNative 'AssignProcessToJobObject' @($job,$process)) -ne 0) 'Assign suspended command to job'
        $assigned=$true; $r.group_assigned_before_resume=$true
        Assert-NrNative ((Invoke-NrNative 'ResumeThread' @($thread)) -ne [uint32]::MaxValue) 'Resume supervised command'
        $r.user_code_started=$true; Close-NrHandle $thread; $thread=[IntPtr]::Zero
        $clock=[Diagnostics.Stopwatch]::StartNew()
        while ($true) {
            if ($process -ne [IntPtr]::Zero) {
                $wait=Invoke-NrNative 'WaitForSingleObject' @($process,[uint32]0)
                if ($wait -eq [uint32]::MaxValue) { throw 'Parent wait failed.' }
                if ($wait -eq 0) {
                    Assert-NrNative ((Invoke-NrNative 'GetExitCodeProcess' @($process,$exitPtr)) -ne 0) 'Read parent exit'
                    $r.numeric_exit_code=[uint32]([int64][Runtime.InteropServices.Marshal]::ReadInt32($exitPtr) -band [int64]4294967295)
                    $r.parent_exit_observed=$true
                    Close-NrHandle $process; $process=[IntPtr]::Zero
                }
            }
            Assert-NrNative ((Invoke-NrNative 'QueryInformationJobObject' @($job,[int32]1,$accountingPtr,
                [uint32][Runtime.InteropServices.Marshal]::SizeOf($accounting),[IntPtr]::Zero)) -ne 0) 'Query command group'
            $observed=[Runtime.InteropServices.Marshal]::PtrToStructure($accountingPtr,[Type]$script:NrJobAccounting)
            $r.last_active_processes=$observed.ActiveProcesses
            if ($r.parent_exit_observed -and $observed.ActiveProcesses -eq 0) { $r.group_empty_observed=$true; break }
            if ($clock.Elapsed.TotalSeconds -ge $TimeoutSeconds) {
                $r.timed_out=$true
                $snapshot=[ordered]@{ parent_exit_observed=$r.parent_exit_observed; active_processes=$observed.ActiveProcesses;
                    members=@(); error=$null }
                try {
                    # Preserve the cause before cleanup replaces the active count with zero.
                    $pidList=Allocate-Nr (8+8*128)
                    Assert-NrNative ((Invoke-NrNative 'QueryInformationJobObject' @($job,[int32]3,$pidList,
                        [uint32](8+8*128),[IntPtr]::Zero)) -ne 0) 'Query timed-out group members'
                    $count=[Runtime.InteropServices.Marshal]::ReadInt32($pidList,4)
                    if ($count -lt 0 -or $count -gt 128) { throw 'Timed-out group exceeds member observation budget.' }
                    for ($memberIndex=0;$memberIndex -lt $count;$memberIndex++) {
                        $memberId=[Runtime.InteropServices.Marshal]::ReadInt64($pidList,8+8*$memberIndex)
                        $member=[ordered]@{process_id=$memberId;name=$null;path=$null;error=$null}
                        $memberProcess=$null
                        try {
                            $memberProcess=[Diagnostics.Process]::GetProcessById([int]$memberId)
                            $member.name=$memberProcess.ProcessName
                            $member.path=$memberProcess.MainModule.FileName
                        } catch { $member.error=$_.Exception.Message }
                        finally { if ($memberProcess) { $memberProcess.Dispose() } }
                        $snapshot.members+=@($member)
                    }
                } catch { $snapshot.error=$_.Exception.Message }
                $r.timeout_observation=$snapshot
                throw 'Command group timeout.'
            }
            Start-Sleep -Milliseconds 20
        }
    } catch { $r.errors += $_.Exception.ToString() }
    finally {
        if ($created -and !$r.group_empty_observed) {
            $r.termination_requested=$true
            try {
                if ($assigned) {
                    Assert-NrNative ((Invoke-NrNative 'TerminateJobObject' @($job,[uint32]1)) -ne 0) 'Terminate command group'
                } elseif ($process -ne [IntPtr]::Zero) {
                    # User code never resumed, so this suspended process cannot have created descendants.
                    Assert-NrNative ((Invoke-NrNative 'TerminateProcess' @($process,[uint32]1)) -ne 0) 'Terminate unassigned suspended command'
                }
                if ($process -ne [IntPtr]::Zero) {
                    Assert-NrNative ((Invoke-NrNative 'WaitForSingleObject' @($process,[uint32]5000)) -eq 0) 'Observe parent termination'
                    Assert-NrNative ((Invoke-NrNative 'GetExitCodeProcess' @($process,$exitPtr)) -ne 0) 'Read terminated parent exit'
                    $r.numeric_exit_code=[uint32]([int64][Runtime.InteropServices.Marshal]::ReadInt32($exitPtr) -band [int64]4294967295)
                    $r.parent_exit_observed=$true; Close-NrHandle $process; $process=[IntPtr]::Zero
                }
                $cleanupClock=[Diagnostics.Stopwatch]::StartNew()
                while ($cleanupClock.Elapsed.TotalSeconds -lt 5) {
                    Assert-NrNative ((Invoke-NrNative 'QueryInformationJobObject' @($job,[int32]1,$accountingPtr,
                        [uint32][Runtime.InteropServices.Marshal]::SizeOf($accounting),[IntPtr]::Zero)) -ne 0) 'Query group after termination'
                    $observed=[Runtime.InteropServices.Marshal]::PtrToStructure($accountingPtr,[Type]$script:NrJobAccounting)
                    $r.last_active_processes=$observed.ActiveProcesses
                    if ($r.parent_exit_observed -and $observed.ActiveProcesses -eq 0) { $r.group_empty_observed=$true; break }
                    Start-Sleep -Milliseconds 20
                }
            } catch { $r.cleanup_errors += $_.Exception.ToString() }
        }
        if ($r.group_empty_observed) {
            try {
                Assert-NrNative ((Invoke-NrNative 'FlushFileBuffers' @($stdoutHandle)) -ne 0) 'Flush stdout after group empty'
                Assert-NrNative ((Invoke-NrNative 'FlushFileBuffers' @($stderrHandle)) -ne 0) 'Flush stderr after group empty'
                $r.capture_complete=$true
            } catch { $r.cleanup_errors += $_.Exception.ToString() }
        }
        if ($attributeReady) {
            try { $null=Invoke-NrNative 'DeleteProcThreadAttributeList' @($attributes) }
            catch { $r.cleanup_errors += $_.Exception.ToString() }
        }
        foreach ($h in (@($thread,$process,$job) + $handles.ToArray())) {
            try { Close-NrHandle $h } catch { $r.cleanup_errors += $_.Exception.ToString(); $r.capture_complete=$false }
        }
        foreach ($a in $allocations) { [Runtime.InteropServices.Marshal]::FreeHGlobal($a) }
        $r.ended_utc=[DateTime]::UtcNow.ToString('o')
    }
    return $r
}
