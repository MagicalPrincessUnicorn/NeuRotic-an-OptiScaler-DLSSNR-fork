[CmdletBinding()]
param([string]$InstallRoot)
$ErrorActionPreference='Stop'
if(-not $InstallRoot){$InstallRoot=Read-Host 'Folder containing your installed NeuRotic.Hub.exe'}
$root=[IO.Path]::GetFullPath($InstallRoot)
$manifest=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'UPDATE-MANIFEST.json') -Raw | ConvertFrom-Json
if($manifest.schemaVersion -ne 1 -or $manifest.files.Count -ne 3){throw 'Invalid Anything update manifest'}
$allowed=@('NeuRotic.Hub.exe','Tools/WindowWorker/NeuRotic.WindowWorker.exe','Tools/WindowWorker/nvngx.dll_dlssnr.dll')
$leases=@();$originals=[Collections.Generic.List[byte[]]]::new();$changed=$false
function Assert-PlainPath([string]$Path){
 for($node=$Path;$node;$node=[IO.Path]::GetDirectoryName($node)){
  if(Test-Path -LiteralPath $node){if((Get-Item -LiteralPath $node -Force).Attributes -band [IO.FileAttributes]::ReparsePoint){throw "Junction/symbolic-link paths are unsupported: $node"}}
 }
}
function Hash-Bytes([byte[]]$Bytes){
 $algorithm=[Security.Cryptography.SHA256]::Create()
 try{return ([BitConverter]::ToString($algorithm.ComputeHash($Bytes))).Replace('-','').ToLowerInvariant()}finally{$algorithm.Dispose()}
}
function Read-Stream([IO.FileStream]$Stream){
 if($Stream.Length -gt 32MB){throw 'Unexpected update target size'}
 $bytes=New-Object byte[] ([int]$Stream.Length);$Stream.Position=0;$offset=0
 while($offset -lt $bytes.Length){$count=$Stream.Read($bytes,$offset,$bytes.Length-$offset);if($count -eq 0){throw 'Incomplete binary read'};$offset+=$count}
 return ,$bytes
}
try{
 Assert-PlainPath $root
 $seen=@{}
 foreach($file in $manifest.files){
  if($file.path -notin $allowed -or $seen.ContainsKey($file.path)){throw 'Unexpected or duplicate update path'};$seen[$file.path]=$true
  $target=Join-Path $root $file.path;$source=Join-Path $PSScriptRoot $file.path
  Assert-PlainPath $target;Assert-PlainPath $source
  $payload=[IO.File]::ReadAllBytes($source)
  if((Hash-Bytes $payload) -ne $file.sha256 -or $payload.Length -ne $file.bytes){throw "Update file failed verification: $($file.path)"}
  # Hold every binary exclusively before modifying any. Close the App and worker first.
  $stream=[IO.File]::Open($target,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
  $leases+=$stream;$original=Read-Stream $stream;$originals.Add([byte[]]$original)
  $accepted=@($file.baselineSha256,$file.sha256)+@($file.acceptedInstalledSha256)
  if((Hash-Bytes $original) -notin $accepted){throw "Installed file does not match the supported flagship baseline: $($file.path)"}
 }
 for($index=0;$index -lt $manifest.files.Count;$index++){
  $payload=[IO.File]::ReadAllBytes((Join-Path $PSScriptRoot $manifest.files[$index].path))
  if((Hash-Bytes $payload) -ne $manifest.files[$index].sha256){throw 'Update payload changed during verification'}
  $changed=$true;$stream=$leases[$index];$stream.Position=0;$stream.SetLength($payload.Length);$stream.Write($payload,0,$payload.Length);$stream.Flush($true)
  if((Hash-Bytes (Read-Stream $stream)) -ne $manifest.files[$index].sha256){throw 'Updated binary failed verification'}
 }
 Write-Output 'Anything update applied. Open NeuRotic.Hub.exe normally. Settings and your NR model were preserved.'
}catch{
 $failure=$_.Exception.Message;$rollbackErrors=@()
 if($changed){for($index=0;$index -lt $originals.Count;$index++){
  try{$bytes=$originals[$index];$stream=$leases[$index];$stream.Position=0;$stream.SetLength($bytes.Length);$stream.Write($bytes,0,$bytes.Length);$stream.Flush($true);if((Hash-Bytes (Read-Stream $stream)) -ne (Hash-Bytes $bytes)){throw 'Rollback hash mismatch'}}catch{$rollbackErrors+=$_.Exception.Message}
 }}
 if($rollbackErrors.Count){throw ($failure+'; rollback needs attention: '+($rollbackErrors -join '; '))}
 throw ($failure+$(if($changed){'; original binaries restored'}else{'; no files changed'}))
}finally{foreach($stream in $leases){$stream.Dispose()}}
