$ErrorActionPreference='Stop'
$repo=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$engine=Join-Path $repo 'tools/packaging/customer/support/NeuRotic-Setup-Engine.ps1'
$tokens=$null;$errors=$null;$ast=[Management.Automation.Language.Parser]::ParseFile($engine,[ref]$tokens,[ref]$errors)
if($errors.Count){throw $errors[0]}
foreach($definition in $ast.EndBlock.Statements|Where-Object{$_ -is [Management.Automation.Language.FunctionDefinitionAst]}){. ([scriptblock]::Create($definition.Extent.Text))}
$work=Join-Path $repo 'builds/audit-installer-fixtures';[void][IO.Directory]::CreateDirectory($work)
function Assert($condition,$message){if(-not $condition){throw $message};Write-Output "PASS $message"}
$case=Join-Path $work 'ledger';[void][IO.Directory]::CreateDirectory($case)
$record=Join-Path $case 'record.json';$script:RestoreRecordLedger=@{}
$before=[Text.Encoding]::UTF8.GetBytes('{"existed":true}');[IO.File]::WriteAllBytes($record,$before);$script:RestoreRecordLedger[$record]=$before
[IO.File]::WriteAllText($record,'{"existed":false}')
$refused=$false;try{[void](Get-InstallRecordBytes $record)}catch{$refused=$true}
Assert $refused 'Present metadata must match its committed ledger'
[IO.File]::Delete($record)
Assert ((HashBytes (Get-InstallRecordBytes $record)) -eq (HashBytes $before)) 'Absent metadata uses intact committed ledger'
$script:RestoreRecordLedger=@{}
$exe=Join-Path $work 'powershell.exe';[IO.File]::WriteAllText($exe,'inert unrelated executable')
CheckGameClosed $exe
Assert $true 'An unrelated same-name process is not the selected executable'
Initialize-NeuroticFileSafety
$destination=Join-Path $work 'atomic.bin';[IO.File]::WriteAllText($destination,'before')
$beforeHash=HashFile $destination;$bytes=[Text.Encoding]::UTF8.GetBytes('complete after')
$wrong=[Text.Encoding]::UTF8.GetBytes('incorrect digest')
$refused=$false;try{[void][NeuRoticFileSafety]::Write($bytes,$destination,(HashBytes $wrong),$beforeHash)}catch{$refused=$true}
Assert ($refused -and [IO.File]::ReadAllText($destination) -eq 'before') 'Invalid staged content never mutates destination'
Assert ([NeuRoticFileSafety]::Write($bytes,$destination,(HashBytes $bytes),$beforeHash)) 'Verified content publishes atomically'
Assert ([IO.File]::ReadAllText($destination) -eq 'complete after') 'Published content is complete'
Assert (-not [NeuRoticFileSafety]::Write($before,$destination,(HashBytes $before),$beforeHash)) 'Stale compare-and-swap refuses foreign content'
$refused=$false;try{Assert-NeuroticHistoryCapacity @((1..512)|ForEach-Object{[string]$_})}catch{$refused=$true}
Assert $refused 'History reserves capacity before another installation can commit'
Assert-NeuroticHistoryCapacity @((1..511)|ForEach-Object{[string]$_})
Assert $true 'The last readable history slot remains usable'
