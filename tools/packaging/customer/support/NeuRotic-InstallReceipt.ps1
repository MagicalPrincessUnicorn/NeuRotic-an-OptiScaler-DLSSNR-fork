# Small relative ownership record. This service never reads backup payloads or
# hashes installed files. Resolve each recorded destination again before I/O.
. (Join-Path $PSScriptRoot 'NeuRotic-LoggingPolicy.ps1')
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
  $entry=[ordered]@{path=$path;removeOnUninstall=$file.removeOnUninstall;role=[string]$file.role}
  $objectId=Get-NeuroticField $file 'nrDllObjectId'
  if($null -ne $objectId){
   if($file.role -cne 'nr-runtime' -or $objectId -isnot [string] -or $objectId -cnotmatch '\A[0-9A-Fa-f]{24}\z'){throw "Invalid NR DLL object identity: $path"}
   $entry.nrDllObjectId=$objectId.ToUpperInvariant()
  }
  $entries[$path]=[pscustomobject]$entry
 }
 return @($entries.Values|Sort-Object path)
}
function ConvertTo-NeuroticInstallReceipt($Receipt) {
 if($Receipt.schemaVersion -ne 3 -or $Receipt.kind -ne 'neurotic-game-install' -or $Receipt.status -notin @('Installed','Partial')){throw 'Unsupported ownership receipt'}
 $selected=ConvertTo-NeuroticRelativePath ([string]$Receipt.selectedExecutable)
 $proxy=ConvertTo-NeuroticRelativePath ([string]$Receipt.proxy)
 # Architecture is observed metadata, not a list of supported machines.
 # Match the bounded identity emitted by the PE reader without admitting paths
 # or malformed values into the durable ownership record.
 if($Receipt.architecture -isnot [string] -or $Receipt.architecture -cnotmatch '\A(?:x64|x86|unknown|pe(?:32|64)-machine-[0-9a-f]{4})\z'){throw 'Invalid receipt architecture'}
 $normalized=[ordered]@{
  schemaVersion=3;kind='neurotic-game-install';selectedExecutable=$selected
  architecture=[string]$Receipt.architecture;packageId=[string]$Receipt.packageId
  proxy=$proxy;status=[string]$Receipt.status;files=@(Merge-NeuroticOwnedFiles @() $Receipt.files)
 }
 if($Receipt.profileId){$normalized.profileId=[string]$Receipt.profileId}
 if($Receipt.PSObject.Properties['loggingPolicy']){$normalized.loggingPolicy=ConvertTo-NeuroticLoggingPolicyReceipt $Receipt.loggingPolicy}
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

# Component ownership deliberately has no proxy or whole-install status. Pending
# intent below is separate and never grants removal authority.
function ConvertTo-NeuroticNrDllOwnership($Receipt){
 $version=Get-NeuroticField $Receipt 'schemaVersion'
 if(($version -isnot [int] -and $version -isnot [long]) -or $version -ne 1 -or (Get-NeuroticField $Receipt 'kind') -cne 'neurotic-nr-dll-ownership'){throw 'Unsupported NR DLL component ownership'}
 $operation=Get-NeuroticField $Receipt 'operationId'
 if($operation -isnot [string] -or $operation -cnotmatch '\A[a-f0-9]{32}\z'){throw 'Invalid NR DLL ownership operation'}
 $selected=ConvertTo-NeuroticRelativePath ([string](Get-NeuroticField $Receipt 'selectedExecutable'))
 $files=@(Merge-NeuroticOwnedFiles @() (Get-NeuroticField $Receipt 'files' @()))
 if($files.Count -lt 1 -or $files.Count -gt 64){throw 'NR DLL component ownership exceeds its metadata bound'}
 foreach($file in $files){
  if($file.role -cne 'nr-runtime' -or -not $file.removeOnUninstall -or -not (Get-NeuroticField $file 'nrDllObjectId') -or [IO.Path]::GetFileName($file.path) -ine 'nvngx_dlssnr.dll'){throw 'Invalid NR DLL component file'}
 }
 return [pscustomobject][ordered]@{schemaVersion=1;kind='neurotic-nr-dll-ownership';operationId=$operation;selectedExecutable=$selected;files=@($files)}
}
function Read-NeuroticNrDllOwnership([string]$GameRoot){
 $receipt=$null;$notes=@();$present=$false
 try{
  $path=Resolve-NeuroticOwnedPath $GameRoot 'NeuRotic/Installer/NR-DLL-Ownership.json'
  if(Test-Path -LiteralPath $path){
   $present=$true
   if(-not [IO.File]::Exists($path) -or (Get-Item -LiteralPath $path).Length -gt 1048576){throw 'NR DLL ownership metadata is not a bounded file'}
   $receipt=ConvertTo-NeuroticNrDllOwnership (Get-Content -LiteralPath $path -Raw|ConvertFrom-Json)
  }
 }catch{$notes+=('NR DLL ownership metadata was preserved: '+$_.Exception.Message)}
 return [pscustomobject]@{receipt=$receipt;notes=@($notes);present=$present}
}
function Save-NeuroticNrDllMetadata([string]$GameRoot,[string]$RelativePath,$Document){
 $path=Resolve-NeuroticOwnedPath $GameRoot $RelativePath;$directory=Split-Path -Parent $path
 [IO.Directory]::CreateDirectory($directory)|Out-Null
 # Re-resolve after directory creation; file I/O never follows a linked parent.
 $path=Resolve-NeuroticOwnedPath $GameRoot $RelativePath
 [void][NeuRoticSimpleFileIo]::DirectoryIdentity($directory)
 $temporary=Join-Path $directory ('.NR-DLL.'+[Guid]::NewGuid().ToString('N')+'.writing')
 try{
  $bytes=[Text.Encoding]::UTF8.GetBytes(($Document|ConvertTo-Json -Depth 12))
  [NeuRoticSimpleFileIo]::Write($bytes,$temporary)
  if([IO.File]::Exists($path)){
   for($attempt=0;;$attempt++){
    try{[IO.File]::Replace($temporary,$path,[NullString]::Value);break}
    catch [IO.IOException]{if($attempt -ge 9){throw};[Threading.Thread]::Sleep(25)}
   }
  }else{[IO.File]::Move($temporary,$path)}
 }finally{if([IO.File]::Exists($temporary)){[NeuRoticSimpleFileIo]::Remove($temporary)}}
}
function Save-NeuroticNrDllOwnership([string]$GameRoot,$Receipt){
 $normalized=ConvertTo-NeuroticNrDllOwnership $Receipt;$prior=Read-NeuroticNrDllOwnership $GameRoot
 if($prior.present -and -not $prior.receipt){throw (New-NeuroticCodedError 'desktop.nrdll.ownership_metadata_invalid' 'NR DLL ownership metadata could not be updated. It was preserved.')}
 Save-NeuroticNrDllMetadata $GameRoot 'NeuRotic/Installer/NR-DLL-Ownership.json' $normalized
}
function Get-NeuroticConfirmedNrDllFiles([string]$GameRoot,$Receipt){
 $confirmed=@()
 foreach($file in @(Get-NeuroticField $Receipt 'files' @())){
  try{
   $path=Resolve-NeuroticOwnedPath $GameRoot $file.path
   $identity=[NeuRoticSimpleFileIo]::Stat($path)
   if($identity -cne 'Absent' -and $identity.Split(':')[0] -ceq $file.nrDllObjectId){$confirmed+=,$file}
  }catch{}
 }
 return @($confirmed)
}
function Remove-NeuroticNrDllMetadata([string]$GameRoot,[string]$RelativePath){
 $path=Resolve-NeuroticOwnedPath $GameRoot $RelativePath
 [NeuRoticSimpleFileIo]::Remove($path)
}
