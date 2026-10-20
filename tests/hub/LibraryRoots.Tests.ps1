param([string]$Helper=(Join-Path $PSScriptRoot '../../builds/hub/bin/discovery/NeuRotic.Discovery.exe'))
$ErrorActionPreference='Stop'
$fixture=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../builds/hub/library-root-protocol-fixture'))
$allowed=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../builds/hub'))+[IO.Path]::DirectorySeparatorChar
if(-not $fixture.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase)-or(Test-Path -LiteralPath $fixture)){throw 'Unsafe or retained root fixture'}
for($parent=$fixture;$parent;$parent=Split-Path -Parent $parent){if((Test-Path -LiteralPath $parent)-and((Get-Item -LiteralPath $parent -Force).Attributes-band[IO.FileAttributes]::ReparsePoint)){throw 'Linked fixture ancestor'}}
$root=Join-Path $fixture 'Library';$outside=Join-Path $fixture 'Outside';$link=Join-Path $root 'Genre/Linked'
New-Item -ItemType Directory -Path (Join-Path $root 'Genre/Series/Game'),$outside -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $env:SystemRoot 'System32/cmd.exe') -Destination (Join-Path $root 'Genre/Series/Game/Game.exe')
Copy-Item -LiteralPath (Join-Path $env:SystemRoot 'System32/cmd.exe') -Destination (Join-Path $outside 'Outside.exe')
try{
 New-Item -ItemType Junction -Path $link -Target $outside | Out-Null
 $before=@{};foreach($file in @((Join-Path $root 'Genre/Series/Game/Game.exe'),(Join-Path $outside 'Outside.exe'))){$before[$file]=[pscustomobject]@{Hash=(Get-FileHash -LiteralPath $file).Hash;Time=(Get-Item -LiteralPath $file).LastWriteTimeUtc}}
 $request=@{protocolVersion=2;kind='DiscoverFolder';roots=@($root)}|ConvertTo-Json -Compress
 $reply=$request|& $Helper
 if($LASTEXITCODE-ne 0){throw 'Recursive root request failed'}
 $result=$reply|ConvertFrom-Json
 if(@($result.games).Count-ne 1-or$result.games[0].title-ne 'Game'-or@($result.directories).Count-ne 1-or$result.directories[0].path-ne $root){throw 'Recursive scan reported a game directory/root escape'}
 'PASS ROOT-PROTOCOL recursive nested game, single root and child junction refusal'
 $request=@{protocolVersion=2;kind='DiscoverFolder';roots=@($root);excludedRoots=@((Join-Path $root 'Genre/Series'))}|ConvertTo-Json -Compress
 $reply=$request|& $Helper
 if($LASTEXITCODE-ne 0){throw 'Folder root exclusions were rejected'}
 $result=$reply|ConvertFrom-Json
 if(@($result.games).Count){throw 'Folder rescan entered a removed descendant root'}
 'PASS ROOT-PROTOCOL folder-only scans honor removed descendant roots'
 $request=@{protocolVersion=2;kind='DiscoverGames';roots=@($root,($root.ToUpperInvariant()+'\'));excludedRoots=@($root)}|ConvertTo-Json -Compress
 $reply=$request|& $Helper
 if($LASTEXITCODE-ne 0){throw 'Excluded root request failed'}
 $result=$reply|ConvertFrom-Json
 if(@($result.games|Where-Object {$_.installRoot.StartsWith($root,[StringComparison]::OrdinalIgnoreCase)}).Count-or@($result.directories|Where-Object {$_.path.StartsWith($root,[StringComparison]::OrdinalIgnoreCase)}).Count){throw 'Excluded root was scanned or reported'}
 'PASS ROOT-PROTOCOL removed custom root excludes future discovery'
 foreach($file in $before.Keys){if((Get-FileHash -LiteralPath $file).Hash-ne $before[$file].Hash-or(Get-Item -LiteralPath $file).LastWriteTimeUtc-ne $before[$file].Time){throw 'Discovery modified a fixture'}}
 'PASS ROOT-PROTOCOL games and outside files retain hashes/timestamps'
}finally{
 if(Test-Path -LiteralPath $link){[IO.Directory]::Delete($link)}
 Get-ChildItem -LiteralPath $fixture -Recurse -Force|Where-Object {$_.Attributes-band[IO.FileAttributes]::ReparsePoint}|ForEach-Object {throw 'Linked fixture cleanup refused'}
 Remove-Item -LiteralPath $fixture -Recurse -Force
}
