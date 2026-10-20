$ErrorActionPreference='Stop'
$repo=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$work=Join-Path $repo 'builds/hub/anticheat-collector-fixtures'
if(Test-Path $work){throw 'Inspect retained collector fixtures before reuse'}
New-Item -ItemType Directory -Path $work -Force|Out-Null
Add-Type -Path (Join-Path $repo 'tools/packaging/customer/support/NeuRotic-AntiCheatCollector.cs') -ReferencedAssemblies System.Web.Extensions
. (Join-Path $repo 'tools/packaging/customer/support/NeuRotic-AntiCheatPolicy.ps1')
$rules=Get-Content -Raw (Join-Path $repo 'tools/packaging/customer/support/NeuRotic-AntiCheatProviders.json')|ConvertFrom-Json
$names=[string[]]@($rules.file_signals|ForEach-Object {$_.basenames})
function Assert($c,$m){if(-not $c){throw $m};Write-Output ('PASS '+$m)}
$script:Measurements=New-Object Collections.Generic.List[object]
function Scan($root,$exe){$v=[NeuRoticAntiCheatCollector]::Collect($root,$exe,$names)|ConvertFrom-Json;$v|Add-Member -NotePropertyName report -NotePropertyValue (Invoke-HubAntiCheatPolicy $rules $v.snapshot);$script:Measurements.Add([ordered]@{counters=$v.counters;game_state=$v.report.game_state;scan_status=$v.report.scan_status;issues=$v.report.issues;providers=@($v.report.findings|ForEach-Object {$_.provider})});return $v}
try{
 $game=Join-Path $work 'Game';$other=Join-Path $work 'Game2';New-Item -ItemType Directory -Path (Join-Path $game 'Binaries/Win64'),(Join-Path $game 'EasyAntiCheat'),$other -Force|Out-Null
 $exe=Join-Path $game 'Binaries/Win64/Real.exe';Copy-Item (Join-Path $env:SystemRoot 'System32/cmd.exe') $exe
 Copy-Item $exe (Join-Path $game 'EasyAntiCheat/EasyAntiCheat_EOS_Setup.exe');Copy-Item $exe (Join-Path $game 'start_protected_game.exe');[IO.File]::WriteAllText((Join-Path $game 'EasyAntiCheat/Settings.json'),'{"productid":"private-do-not-export","exe":"../../Game2/secret.exe","url":"https://invalid.example/never-follow"}')
 $scan=Scan $game $exe;Assert ($scan.report.game_state -eq 'detected' -and $scan.report.scan_status -eq 'complete') 'N01 full trusted root detects EAC above nested EXE'
 Assert (($scan.report|ConvertTo-Json -Depth 16) -notmatch 'private-do-not-export|invalid.example') 'N15 raw configuration and its paths never exported or followed'
 Copy-Item $exe (Join-Path $other 'BEService.exe');$scan=Scan $game $exe;Assert (@($scan.snapshot.files|Where-Object {$_.path -like '*BEService*'}).Count -eq 0) 'N02 N03 sibling and same-prefix root contents never read'
 $link=Join-Path $game 'escape';New-Item -ItemType Junction -Path $link -Target $other|Out-Null
 $scan=Scan $game $exe;Assert ($scan.report.game_state -eq 'detected' -and $scan.report.scan_status -eq 'incomplete' -and $scan.snapshot.issues -contains 'descendant_reparse') 'N04 N09 partial positive survives descendant junction refusal'
 [IO.Directory]::Delete($link)
 $race=Join-Path $game 'replace';New-Item -ItemType Directory $race|Out-Null
 $script:renameDenied=$false
 [NeuRoticAntiCheatCollector]::TestBoundary=[Action[string,string]]{param($phase,$path) if($phase -eq 'before_enumerate' -and $path -eq $race){try{[IO.Directory]::Move($race,$race+'-moved')}catch{$script:renameDenied=$true}}}
 $scan=Scan $game $exe;Assert ($script:renameDenied -and $scan.report.game_state -eq 'detected') 'N06 open directory cannot be exchanged during enumeration'
 [NeuRoticAntiCheatCollector]::TestBoundary=$null
 $file=Join-Path $game 'EasyAntiCheat/EasyAntiCheat_EOS_Setup.exe';$script:fileRenameDenied=$false
 [NeuRoticAntiCheatCollector]::TestBoundary=[Action[string,string]]{param($phase,$path) if($phase -eq 'before_read' -and $path -eq $file.Replace('/','\')){try{[IO.File]::Move($file,$file+'.moved')}catch{$script:fileRenameDenied=$true}}}
 $scan=Scan $game $exe;Assert ($script:fileRenameDenied -and $scan.report.game_state -eq 'detected') 'N07 candidate cannot be replaced between scope proof and content observation'
 [NeuRoticAntiCheatCollector]::TestBoundary=$null
 [IO.File]::WriteAllText((Join-Path $game 'EasyAntiCheat/Settings.json'),('a'*65537));$scan=Scan $game $exe;Assert ($scan.report.game_state -eq 'possible' -and $scan.report.scan_status -eq 'incomplete') 'N10 oversized settings bounded and positive component retained'
 [IO.File]::WriteAllText((Join-Path $game 'EasyAntiCheat/Settings.json'),'{"a":1,"a":2}');$scan=Scan $game $exe;Assert ($scan.report.game_state -eq 'possible' -and $scan.report.issues -contains 'candidate_config_unreadable') 'N10 duplicate settings keys cannot corroborate layout'
 [IO.File]::WriteAllText((Join-Path $game 'EasyAntiCheat/Settings.json'),'{"a":1}')
 $extra=Join-Path $game 'Unusual/Components';New-Item -ItemType Directory -Path $extra -Force|Out-Null;Copy-Item $exe (Join-Path $extra 'EAAntiCheat.Installer.exe');$scan=Scan $game $exe;Assert ($scan.report.findings.provider -contains 'ea_javelin') 'General bounded BFS retains components outside expected near-root layouts'
 $swap=Join-Path $game 'swap-before-open';New-Item -ItemType Directory $swap|Out-Null;$script:swapped=$false
 [NeuRoticAntiCheatCollector]::TestBoundary=[Action[string,string]]{param($phase,$path) if($phase -eq 'before_open' -and $path -eq $swap -and -not $script:swapped){[IO.Directory]::Move($swap,$swap+'-original');New-Item -ItemType Junction -Path $swap -Target $other|Out-Null;$script:swapped=$true}}
 $scan=Scan $game $exe;[NeuRoticAntiCheatCollector]::TestBoundary=$null;Assert ($script:swapped -and $scan.report.scan_status -eq 'incomplete' -and @($scan.snapshot.files|Where-Object {$_.path -like '*BEService*'}).Count -eq 0) 'N06 replacement junction before directory open cannot access unrelated contents';[IO.Directory]::Delete($swap);[IO.Directory]::Move($swap+'-original',$swap)
 [NeuRoticAntiCheatCollector]::TestBoundary=[Action[string,string]]{param($phase,$path) if($phase -eq 'before_open' -and $path -eq $file.Replace('/','\')){throw (New-Object ComponentModel.Win32Exception(5))}}
 $scan=Scan $game $exe;[NeuRoticAntiCheatCollector]::TestBoundary=$null;Assert ($scan.report.game_state -eq 'possible' -and $scan.report.scan_status -eq 'incomplete') 'N08 access-denied candidate retains filename evidence without invented digest'
 [NeuRoticAntiCheatCollector]::TestBoundary=[Action[string,string]]{param($phase,$path) if($phase -eq 'before_enumerate'){throw (New-Object OperationCanceledException)}}
 $scan=Scan $game $exe;[NeuRoticAntiCheatCollector]::TestBoundary=$null;Assert ($scan.report.scan_status -eq 'cancelled' -and -not $scan.report.requires_ack) 'N13 cooperative cancellation does not produce installation approval'
 $deep=Join-Path $game 'a/b/c/d/e/f/g/h/i/j/k/l/m';New-Item -ItemType Directory -Path $deep -Force|Out-Null;$scan=Scan $game $exe;Assert ($scan.snapshot.issues -contains 'depth_limit' -and $scan.report.game_state -eq 'detected') 'N12 depth budget enforced while retaining findings'
 $many=Join-Path $work 'ManyCandidates';New-Item -ItemType Directory $many|Out-Null;Copy-Item $exe (Join-Path $many 'Real.exe')
 for($i=0;$i -lt 257;$i++){$dir=Join-Path $many ('p'+$i);[IO.Directory]::CreateDirectory($dir)|Out-Null;[IO.File]::WriteAllText((Join-Path $dir 'EAAntiCheat.Installer.exe'),'not-a-PE')}
 $scan=Scan $many (Join-Path $many 'Real.exe');Assert ($scan.counters.candidates -eq 256 -and $scan.report.scan_status -eq 'incomplete' -and $scan.report.game_state -eq 'possible') 'N12 candidate 257 stops scheduling and retains first provider evidence'
 $entriesRoot=Join-Path $work 'ManyEntries';[IO.Directory]::CreateDirectory($entriesRoot)|Out-Null;Copy-Item $exe (Join-Path $entriesRoot 'Real.exe');[IO.File]::WriteAllText((Join-Path $entriesRoot 'EasyAntiCheat_EOS_Setup.exe'),'not-a-PE')
 for($i=0;$i -lt 50001;$i++){[IO.File]::WriteAllText((Join-Path $entriesRoot ('f'+$i+'.txt')),'')}
 $scan=Scan $entriesRoot (Join-Path $entriesRoot 'Real.exe');Assert ($scan.counters.entries -le 50001 -and $scan.report.scan_status -eq 'incomplete' -and $scan.report.game_state -eq 'possible') 'N12 entry budget enforced during enumeration and priority finding retained'
 $before=(Get-Process -Id $PID).HandleCount;for($i=0;$i -lt 10;$i++){[void](Scan $game $exe)};[GC]::Collect();[GC]::WaitForPendingFinalizers();$after=(Get-Process -Id $PID).HandleCount;Assert ($after -le $before+20) 'N13 repeated scans release native directory and candidate handles'
 $rootLink=Join-Path $work 'root-link';New-Item -ItemType Junction -Path $rootLink -Target $game|Out-Null;$scan=Scan $rootLink (Join-Path $rootLink 'Binaries/Win64/Real.exe');Assert ($scan.report.scan_status -eq 'incomplete' -and $scan.counters.inspected_bytes -eq 0) 'N05 unresolved linked root refuses content before scope proof';[IO.Directory]::Delete($rootLink)
 $scan=Scan '\\127.0.0.1\share' '\\127.0.0.1\share\real.exe';Assert ($scan.report.scan_status -eq 'incomplete' -and $scan.counters.inspected_bytes -eq 0) 'N19 UNC root refused without traversal'
 $bytes=[Convert]::FromBase64String([NeuRoticAntiCheatCollector]::ReadBounded((Join-Path $game 'EasyAntiCheat/Settings.json'),65536));Assert ($bytes.Length -eq 7) 'Handle-safe bounded manifest/rule reader remains functional'
 $measurementPath=Join-Path $repo 'docs/implementation-evidence/hub-anticheat-detector-20261003/collector-measurements.json'
 $receipt=[ordered]@{fixture_only=$true;os=[Environment]::OSVersion.VersionString;collector_limits=[ordered]@{deadline_ms=5000;entries=50000;directories=4096;depth=12;candidates=256;pe_header_bytes=1048576;config_bytes=65536};measurements=$script:Measurements.ToArray()}
 [IO.File]::WriteAllText($measurementPath,($receipt|ConvertTo-Json -Depth 10))
}finally{
 [NeuRoticAntiCheatCollector]::TestBoundary=$null
 $absolute=[IO.Path]::GetFullPath($work);if($absolute -ne [IO.Path]::GetFullPath((Join-Path $repo 'builds/hub/anticheat-collector-fixtures'))){throw 'Cleanup target mismatch'}
 foreach($entry in Get-ChildItem -LiteralPath $work -Recurse -Force){if($entry.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked cleanup refused; unlink fixture explicitly'}}
 Remove-Item -LiteralPath $work -Recurse -Force
}
