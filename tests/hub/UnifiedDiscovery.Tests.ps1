param([string]$Helper=(Join-Path $PSScriptRoot '../../builds/hub/bin/discovery/NeuRotic.Discovery.exe'))
$ErrorActionPreference='Stop'
$fixture=Join-Path $PSScriptRoot '../../builds/hub/unified-protocol-fixture'
$fixture=[IO.Path]::GetFullPath($fixture)
$fixtureRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../builds/hub'))+[IO.Path]::DirectorySeparatorChar
if(-not $fixture.StartsWith($fixtureRoot,[StringComparison]::OrdinalIgnoreCase)){throw 'Unexpected fixture path'}
if(Test-Path -LiteralPath $fixture){throw 'Previous discovery fixture remains; inspect before reuse'}
for($fixtureParent=$fixture; $fixtureParent; $fixtureParent=Split-Path -Parent $fixtureParent){
 if((Test-Path -LiteralPath $fixtureParent) -and ((Get-Item -LiteralPath $fixtureParent -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Linked fixture ancestor refused'}
}
New-Item -ItemType Directory -Path (Join-Path $fixture 'Chronos') -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $env:SystemRoot 'System32/cmd.exe') -Destination (Join-Path $fixture 'Chronos/Chronos.exe') -Force
try {
 $request=@{protocolVersion=2;kind='DiscoverGames';roots=@($fixture)} | ConvertTo-Json -Compress
 $reply=$request | & $Helper
 if($LASTEXITCODE -ne 0){throw 'v2 unified discovery request was rejected'}
 $result=$reply | ConvertFrom-Json
 $game=@($result.games | Where-Object title -eq 'Chronos')
 if($result.protocolVersion -ne 2 -or $game.Count -ne 1 -or -not $game[0].executable.EndsWith('Chronos.exe')){throw 'Unified scan did not identify the custom game executable'}
 'PASS NH-07-PROTOCOL: unified scan identifies a custom x64 game without a picker'
 $linked=Join-Path $fixture 'Linked'
 New-Item -ItemType Junction -Path $linked -Target (Join-Path $fixture 'Chronos') | Out-Null
 try {
  $request=@{protocolVersion=2;kind='DiscoverGames';roots=@($linked)} | ConvertTo-Json -Compress
  $reply=$request | & $Helper
  if($LASTEXITCODE -ne 0){throw 'Linked-root request failed unexpectedly'}
  $result=$reply | ConvertFrom-Json
  if(@($result.games | Where-Object { $_.installRoot -eq $linked }).Count){throw 'Linked root was traversed'}
  'PASS NH-07-JUNCTION: linked search root excluded by real protocol'
 } finally { [IO.Directory]::Delete($linked) }

} finally {
 Get-ChildItem -LiteralPath $fixture -Recurse -Force | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint } | ForEach-Object {throw 'Linked fixture refused'}
 Remove-Item -LiteralPath $fixture -Recurse -Force
}
