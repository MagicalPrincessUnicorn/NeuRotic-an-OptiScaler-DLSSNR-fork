# Small relative ownership record. This service never reads backup payloads or
# hashes installed files. Resolve each recorded destination again before I/O.
function ConvertTo-NeuroticRelativePath([string]$Path) {
 if([string]::IsNullOrWhiteSpace($Path) -or [IO.Path]::IsPathRooted($Path) -or $Path -match '[:\x00-\x1f*?"<>|]'){throw "Unsafe game-relative path: $Path"}
 $parts=$Path.Replace('\','/').Split('/')
 foreach($part in $parts){
  if(-not $part -or $part -in @('.','..') -or $part -match '[. ]$' -or $part -match '^(?i:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\.|$)'){throw "Unsafe game-relative path: $Path"}
 }
 return $parts -join '/'
}
function Resolve-NeuroticOwnedPath([string]$GameRoot,[string]$RelativePath) {
 $relative=ConvertTo-NeuroticRelativePath $RelativePath
 $root=[IO.Path]::GetFullPath($GameRoot)
 $destination=[IO.Path]::GetFullPath((Join-Path $root $relative))
 if(-not $destination.StartsWith($root.TrimEnd('\','/')+'\',[StringComparison]::OrdinalIgnoreCase)){throw "Destination escapes selected game: $RelativePath"}
 if((Get-Item -LiteralPath $root -Force).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked game root is unsupported'}
 $current=$root
 # Never descend through a junction or symbolic link inside the selected root.
 foreach($part in $relative.Split('/')){
  $current=Join-Path $current $part
  if(Test-Path -LiteralPath $current){
   $entry=Get-Item -LiteralPath $current -Force
   if($entry.Attributes -band [IO.FileAttributes]::ReparsePoint){throw "Linked destination is unsupported: $RelativePath"}
  }
 }
 return $destination
}
function Merge-NeuroticOwnedFiles($Existing,$Intended) {
 $entries=New-Object 'Collections.Generic.Dictionary[string,object]' ([StringComparer]::OrdinalIgnoreCase)
 foreach($file in @($Existing)+@($Intended)){
  if($null -eq $file){continue}
  $path=ConvertTo-NeuroticRelativePath ([string]$file.path)
  if($file.removeOnUninstall -isnot [bool]){throw "Invalid ownership flag: $path"}
  $entries[$path]=[pscustomobject][ordered]@{path=$path;removeOnUninstall=$file.removeOnUninstall;role=[string]$file.role}
 }
 return @($entries.Values|Sort-Object path)
}
function ConvertTo-NeuroticInstallReceipt($Receipt) {
 if($Receipt.schemaVersion -ne 3 -or $Receipt.kind -ne 'neurotic-game-install' -or $Receipt.status -notin @('Installed','Partial')){throw 'Unsupported ownership receipt'}
 $selected=ConvertTo-NeuroticRelativePath ([string]$Receipt.selectedExecutable)
 $proxy=ConvertTo-NeuroticRelativePath ([string]$Receipt.proxy)
 if($Receipt.architecture -notin @('x64','x86','unknown')){throw 'Invalid receipt architecture'}
 $normalized=[ordered]@{
  schemaVersion=3;kind='neurotic-game-install';selectedExecutable=$selected
  architecture=[string]$Receipt.architecture;packageId=[string]$Receipt.packageId
  proxy=$proxy;status=[string]$Receipt.status;files=@(Merge-NeuroticOwnedFiles @() $Receipt.files)
 }
 if($Receipt.profileId){$normalized.profileId=[string]$Receipt.profileId}
 if($Receipt.reshadeRename){
  $from=ConvertTo-NeuroticRelativePath ([string]$Receipt.reshadeRename.from)
  $to=ConvertTo-NeuroticRelativePath ([string]$Receipt.reshadeRename.to)
  if($from -ine 'dxgi.dll' -or $to -ine 'ReShade64.dll'){throw 'Unsupported ReShade rename record'}
  $normalized.reshadeRename=[pscustomobject]@{from=$from;to=$to}
 }
 return [pscustomobject]$normalized
}
function Read-NeuroticInstallReceipt([string]$GameRoot) {
 $notes=@();$receipt=$null
 try {
  $path=Resolve-NeuroticOwnedPath $GameRoot 'NeuRotic/Installer/Current-Install.json'
  if(Test-Path -LiteralPath $path -PathType Leaf){
   $info=Get-Item -LiteralPath $path
   if($info.Length -gt 1048576){throw 'Ownership receipt exceeds the metadata limit'}
   $document=Get-Content -LiteralPath $path -Raw|ConvertFrom-Json
   if($document.kind -eq 'neurotic-public-install' -and $document.schema_version -eq 2){
    . (Join-Path $PSScriptRoot 'NeuRotic-LegacyReceipt.ps1')
    $legacy=ConvertFrom-NeuroticLegacyReceipt $document $GameRoot;$receipt=$legacy.receipt;$notes+=@($legacy.notes)
   }else{$receipt=ConvertTo-NeuroticInstallReceipt $document}
  }
 }catch{$notes+=('Ownership metadata could not be used: '+$_.Exception.Message)}
 return [pscustomobject]@{receipt=$receipt;notes=@($notes)}
}
function Save-NeuroticInstallReceipt([string]$GameRoot,$Receipt) {
 $normalized=ConvertTo-NeuroticInstallReceipt $Receipt
 $path=Resolve-NeuroticOwnedPath $GameRoot 'NeuRotic/Installer/Current-Install.json'
 $directory=Split-Path -Parent $path
 New-Item -ItemType Directory -Path $directory -Force|Out-Null
 $temporary=Join-Path $directory ('.Current-Install.'+[Guid]::NewGuid().ToString('N')+'.writing')
 try {
  [IO.File]::WriteAllText($temporary,($normalized|ConvertTo-Json -Depth 8),(New-Object Text.UTF8Encoding($false)))
  if(Test-Path -LiteralPath $path -PathType Leaf){
   # Windows scanners can briefly open newly written metadata without delete
   # sharing. Retry only this atomic receipt publication, never a game-tree pass.
   for($attempt=0;;$attempt++){
    try{[IO.File]::Replace($temporary,$path,[NullString]::Value);break}
    catch [IO.IOException]{if($attempt -ge 9){throw};[Threading.Thread]::Sleep(25)}
   }
  }
  else{[IO.File]::Move($temporary,$path)}
 }finally{if(Test-Path -LiteralPath $temporary){Remove-Item -LiteralPath $temporary -Force}}
}
