$ErrorActionPreference='Stop'
$repo=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
. (Join-Path $repo 'tools/packaging/customer/support/NeuRotic-HubProtocol.ps1')
function Assert($condition,$message){if(-not $condition){throw $message};Write-Output "PASS $message"}
foreach($action in @('SaveAndReplace','Replace','Skip','Cancel')){
 $text=@{protocolVersion=1;kind='Plan';operation='Update';fileDecisions=@(@{path='dxgi.dll';sha256=('a'*64);action=$action})}|ConvertTo-Json -Depth 6
 $request=Read-HubRequestText $text
 Assert ($request.fileDecisions[0].action -ceq $action) "Per-file choice accepted: $action"
}
foreach($entry in @(
 @{path='../outside.dll';sha256=('a'*64);action='Replace'},
 @{path='C:\outside.dll';sha256=('a'*64);action='Replace'},
 @{path='dxgi.dll:stream';sha256=('a'*64);action='Replace'},
 @{path='dxgi.dll';sha256='';action='Replace'},
 @{path='dxgi.dll';sha256=('a'*64);action='Overwrite'},
 @{path='dxgi.dll';sha256=('a'*64);action='Replace';unexpected=$true}
)){
 $refused=$false;try{Read-HubRequestText (@{protocolVersion=1;kind='Plan';operation='Update';fileDecisions=@($entry)}|ConvertTo-Json -Depth 6)|Out-Null}catch{$refused=$true}
 Assert $refused 'Invalid file decision refused before planning'
}
$entry=@{path='dxgi.dll';sha256=('a'*64);action='Replace'}
$refused=$false;try{Read-HubRequestText (@{protocolVersion=1;kind='Plan';operation='Update';fileDecisions=@($entry,$entry)}|ConvertTo-Json -Depth 6)|Out-Null}catch{$refused=$true}
Assert $refused 'Duplicate path decisions refused'
$refused=$false;try{Read-HubRequestText (@{protocolVersion=1;kind='Execute';fileDecisions=@($entry)}|ConvertTo-Json -Depth 6)|Out-Null}catch{$refused=$true}
Assert $refused 'Execute cannot inject unreviewed decisions'
$target=[pscustomobject]@{executable='C:\fixture\Game.exe';directory='C:\fixture'}
$args=Get-HubEngineArguments (Read-HubRequestText (@{protocolVersion=1;kind='Plan';operation='Update';proxyName='dxgi.dll';fileDecisions=@($entry)}|ConvertTo-Json -Depth 6)) $target ([pscustomobject]@{root='C:\package'})
Assert (($args.FileDecisionsJson|ConvertFrom-Json)[0].action -eq 'Replace') 'Engine receives typed decisions'

$request=Read-HubRequestText '{"protocolVersion":1,"kind":"Inspect","verifyFiles":true}'
Assert ($request.verifyFiles -eq $true) 'Explicit file verification accepted on inspection'
foreach($json in @('{"protocolVersion":1,"kind":"Inspect","verifyFiles":"true"}','{"protocolVersion":1,"kind":"Plan","operation":"Install","verifyFiles":true}')){
 $refused=$false;try{Read-HubRequestText $json|Out-Null}catch{$refused=$true}
 Assert $refused 'Verification flag rejects wrong type and operation'
}

# Isolate status reads: no package, game or installer writes, and no payload hashing.
function SafePath($root,$relative){return Join-Path $root $relative}
function Test-Path {return $false}
function Get-HubSettingsRoot($target){return $target.directory}
function Get-HubSettingsRelativePath($target){return 'OptiScaler.ini'}
function Get-HubSettingsProjection($root){return @()}
function Get-HubInventory {throw 'Quick status traversed inventory'}
function Get-HubCurrentFiles {throw 'Quick status hashed current files'}
function HashFile {throw 'Quick status hashed a file'}
function HashBytes {return 'fixture-revision'}
$inspection=Get-HubInspection $target -Quick
Assert ($inspection.verification -eq 'NotChecked') 'Quick status explicitly reports payloads not checked'
Assert ($inspection.inventory.Count -eq 0) 'Quick status has no payload inventory'
