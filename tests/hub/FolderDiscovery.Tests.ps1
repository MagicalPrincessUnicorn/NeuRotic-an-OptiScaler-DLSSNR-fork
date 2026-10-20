param([string]$Helper=(Join-Path $PSScriptRoot '../../builds/hub/bin/discovery/NeuRotic.Discovery.exe'))
$ErrorActionPreference='Stop'
$fixture=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../builds/hub/folder-protocol-fixture'))
$fixtureRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../builds/hub'))+[IO.Path]::DirectorySeparatorChar
if(-not $fixture.StartsWith($fixtureRoot,[StringComparison]::OrdinalIgnoreCase)){throw 'Unexpected fixture path'}
if(Test-Path -LiteralPath $fixture){throw 'Previous discovery fixture remains; inspect before reuse'}
for($fixtureParent=$fixture; $fixtureParent; $fixtureParent=Split-Path -Parent $fixtureParent){
 if((Test-Path -LiteralPath $fixtureParent) -and ((Get-Item -LiteralPath $fixtureParent -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Linked fixture ancestor refused'}
}
New-Item -ItemType Directory -Path (Join-Path $fixture 'Added/LocalGame') -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $env:SystemRoot 'System32/cmd.exe') -Destination (Join-Path $fixture 'Added/LocalGame/LocalGame.exe') -Force
function Invoke-Request($request) {
 $json=$request | ConvertTo-Json -Compress -Depth 8
 # Rejected requests deliberately write stderr; inspect the exit code rather
 # than letting Windows PowerShell turn expected native stderr into a throw.
 $previousPreference=$ErrorActionPreference
 try {$ErrorActionPreference='Continue';$reply=$json | & $Helper 2>&1;$requestExit=$LASTEXITCODE}
 finally {$ErrorActionPreference=$previousPreference}
 [pscustomobject]@{Exit=$requestExit;Reply=($reply -join "`n")}
}
try {
 $added=[IO.Path]::GetFullPath((Join-Path $fixture 'Added'))
 $response=Invoke-Request @{protocolVersion=2;kind='DiscoverFolder';roots=@($added)}
 if($response.Exit -ne 0){throw "One-folder request rejected: $($response.Reply)"}
 $result=$response.Reply | ConvertFrom-Json
 if($result.protocolVersion -ne 2 -or @($result.games).Count -ne 1 -or $result.games[0].title -ne 'LocalGame' -or $result.games[0].store -ne 'Custom'){throw 'One-folder request escaped its added folder'}
 if(@($result.directories).Count -ne 1 -or $result.directories[0].path -ne $added -or $result.directories[0].source -ne 'Custom' -or $result.directories[0].automatic){throw 'Local directory metadata is incorrect'}
 'PASS NH-09-FOLDER-PROTOCOL: only the added folder is scanned and reported'
 $engine=Join-Path $fixture 'RuntimeLayouts/ExampleGame/Engine/Binaries/Win64'
 New-Item -ItemType Directory -Path $engine -Force | Out-Null
 Copy-Item -LiteralPath (Join-Path $env:SystemRoot 'System32/cmd.exe') -Destination (Join-Path $engine 'ActualGame.exe')
 Copy-Item -LiteralPath (Join-Path $env:SystemRoot 'System32/cmd.exe') -Destination (Join-Path $fixture 'RuntimeLayouts/ExampleGame/ExampleGameLauncher.exe')
 $response=Invoke-Request @{protocolVersion=2;kind='DiscoverFolder';roots=@((Join-Path $fixture 'RuntimeLayouts'))}
 $runtimeResult=$response.Reply | ConvertFrom-Json
 if($response.Exit -ne 0 -or @($runtimeResult.games).Count -ne 1 -or $runtimeResult.games[0].executable -ne (Join-Path $engine 'ActualGame.exe') -or @($runtimeResult.games[0].executableCandidates).Count -ne 1){throw 'Runtime discovery selected launcher or skipped Engine runtime'}
 'PASS AUDIT-DESK05: launcher excluded; actual Engine runtime selected'
 $invalid=@(
  @{protocolVersion=2;kind='DiscoverFolder';roots=@()},
  @{protocolVersion=2;kind='DiscoverFolder';roots=@($added,$fixture)},
  @{protocolVersion=2;kind='DiscoverFolder';roots=@($added);excludedRoots=@('relative')},
  @{protocolVersion=2;kind='DiscoverFolder';roots=@($added);unknown=$true},
  @{protocolVersion=1;kind='DiscoverSteam';roots=@($added);excludedRoots=@()},
  @{protocolVersion=2;kind='DiscoverFolder';roots=@('relative')},
  @{protocolVersion=2;kind='DiscoverFolder';roots=@('\\server\share\game')},
  @{protocolVersion=2;kind='DiscoverFolder';roots=@([IO.Path]::GetPathRoot($added))}
 )
 foreach($request in $invalid){if((Invoke-Request $request).Exit -eq 0){throw 'Invalid folder/protocol request accepted'}}
 'PASS NH-09-FOLDER-REFUSAL: root count, local path and strict field boundaries'
 $response=Invoke-Request @{protocolVersion=1;kind='DiscoverSteam';roots=@($added)}
 if($response.Exit -ne 0 -or ($response.Reply | ConvertFrom-Json).protocolVersion -ne 1 -or (($response.Reply | ConvertFrom-Json).PSObject.Properties.Name -contains 'directories')){throw 'v1 protocol compatibility changed'}
 'PASS NH-09-V1: legacy supplied-root discovery keeps its response shape'
 $linked=Join-Path $fixture 'Linked'
 New-Item -ItemType Junction -Path $linked -Target (Join-Path $fixture 'Added/LocalGame') | Out-Null
 try {
  $response=Invoke-Request @{protocolVersion=2;kind='DiscoverFolder';roots=@($linked)}
  if($response.Exit -ne 0){throw 'Linked folder request failed unexpectedly'}
  $result=$response.Reply | ConvertFrom-Json
  if(@($result.games).Count -ne 0 -or @($result.directories).Count -ne 0){throw 'Linked folder was traversed or reported'}
  'PASS NH-09-FOLDER-JUNCTION: linked added folders are refused without traversal'
 } finally { [IO.Directory]::Delete($linked) }
} finally {
 Get-ChildItem -LiteralPath $fixture -Recurse -Force | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint } | ForEach-Object {throw 'Linked fixture refused'}
 Remove-Item -LiteralPath $fixture -Recurse -Force
}
