[CmdletBinding()]
param([Parameter(Mandatory=$true,Position=0)][string]$GameExecutable,
      [Parameter(ValueFromRemainingArguments=$true)][string[]]$GameArguments,
      [ValidatePattern('^[1-9][0-9]{0,9}$')][string]$SteamAppId,
      [switch]$CheckOnly)
$ErrorActionPreference='Stop'
$exe=(Resolve-Path -LiteralPath $GameExecutable).Path
if([IO.Path]::GetExtension($exe) -ine '.exe'){throw 'Select your x86 game executable'}
$stream=[IO.File]::OpenRead($exe)
try{
 if($stream.Length -lt 128){throw 'Invalid executable'}
 $reader=[IO.BinaryReader]::new($stream);if($reader.ReadUInt16() -ne 0x5a4d){throw 'Invalid executable'}
 $stream.Position=0x3c;$pe=$reader.ReadUInt32()
 if($pe -lt 64 -or $pe -gt 16777216 -or $pe+248 -gt $stream.Length){throw 'Invalid executable header'}
 $stream.Position=$pe
 if($reader.ReadUInt32() -ne 0x4550 -or $reader.ReadUInt16() -ne 0x14c){throw 'This launcher requires an x86 executable'}
 $stream.Position=$pe+20;$optional=$reader.ReadUInt16();$attributes=$reader.ReadUInt16();$magic=$reader.ReadUInt16()
 if($optional -lt 224 -or $optional -gt 4096 -or $pe+24+$optional -gt $stream.Length -or ($attributes -band 0x2000) -or $magic -ne 0x10b){throw 'Invalid x86 executable architecture'}
}finally{$stream.Dispose()}
$root=Split-Path $exe -Parent
if(-not ('NeuRoticPreparedIdentity' -as [type])){Add-Type -TypeDefinition @'
using System;using System.IO;using System.Runtime.InteropServices;using Microsoft.Win32.SafeHandles;
public static class NeuRoticPreparedIdentity {
 [StructLayout(LayoutKind.Sequential)] struct Info{public uint Attributes;public System.Runtime.InteropServices.ComTypes.FILETIME Creation,Access,Write;public uint Volume,SizeHigh,SizeLow,Links,High,Low;}
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern SafeFileHandle CreateFileW(string p,uint a,uint s,IntPtr sec,uint c,uint f,IntPtr t);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool GetFileInformationByHandle(SafeFileHandle h,out Info i);
 public static SafeFileHandle Pin(string path){var h=CreateFileW(path,0x80000000,3,IntPtr.Zero,3,0x02200000,IntPtr.Zero);Info i;if(h.IsInvalid||!GetFileInformationByHandle(h,out i)||(i.Attributes&0x400)!=0){h.Dispose();throw new IOException("Prepared target is linked or unavailable");}return h;}
 public static string Identity(SafeFileHandle h){Info i;if(!GetFileInformationByHandle(h,out i))throw new IOException("Target identity unavailable");return i.Volume.ToString("X8")+i.High.ToString("X8")+i.Low.ToString("X8");}
 public static void CheckFile(SafeFileHandle h){Info i;if(!GetFileInformationByHandle(h,out i)||i.Links!=1||(i.Attributes&0x410)!=0)throw new IOException("Prepared component is linked or not a file");}
}
'@
}
function PreparedPath([string]$relative){
 if([IO.Path]::IsPathRooted($relative) -or $relative -match '[:*?]|(^|[\\/])\.{1,2}([\\/]|$)'){throw 'Invalid prepared component path'}
 $path=[IO.Path]::GetFullPath((Join-Path $root $relative));$walk=$path
 while($walk){if(Test-Path -LiteralPath $walk){if([IO.File]::GetAttributes($walk) -band [IO.FileAttributes]::ReparsePoint){throw 'Linked prepared paths are not supported'}};$walk=[IO.Path]::GetDirectoryName($walk)}
 if(-not $path.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Prepared path leaves the game'}
 return $path
}
$targetLease=[NeuRoticPreparedIdentity]::Pin($root)
$mutex=New-Object Threading.Mutex($false,('Global\NeuRotic-Installer-'+[NeuRoticPreparedIdentity]::Identity($targetLease)))
$entered=$false;$leases=New-Object Collections.Generic.List[object]
try{
 try{$entered=$mutex.WaitOne(5000)}catch [Threading.AbandonedMutexException]{$entered=$true}
 if(-not $entered){throw 'Another installation or launch owns this game. Try again after it finishes.'}
 foreach($name in @('Install-Transaction.json','Restore-Transaction.json','Uninstall-Transaction.json','History-Transaction.json','Cleanup-Pending.json')){if(Test-Path -LiteralPath (PreparedPath ('NeuRotic\Installer\'+$name))){throw 'Finish pending installer recovery before starting this game.'}}
 foreach($folder in @('NeuRotic-backups','NeuRotic-test-backups')){
  $backupRoot=PreparedPath $folder;if(-not(Test-Path -LiteralPath $backupRoot -PathType Container)){continue}
  foreach($directory in Get-ChildItem -LiteralPath $backupRoot -Directory -Force){
   $recordPath=PreparedPath ($folder+'\'+$directory.Name+'\INSTALL-MANIFEST.json')
   if(Test-Path -LiteralPath $recordPath -PathType Leaf){$pendingRecord=Get-Content -LiteralPath $recordPath -Raw|ConvertFrom-Json;if($pendingRecord.status -in @('backed-up','failed-rollback-incomplete')){throw 'Finish incomplete legacy installer recovery before starting this game.'}}
  }
 }
 $state=Get-Content -LiteralPath (PreparedPath 'NeuRotic\Installer\Current-Install.json') -Raw|ConvertFrom-Json
 if($state.kind -ne 'neurotic-public-install' -or $state.schema_version -ne 2 -or $state.status -ne 'installed-verified' -or $state.install_route -ne 'prepared-x86' -or $state.game_executable -ine $exe){throw 'This game needs a current compatible prepared installation. Use Repair or NR Anything.'}
 if(@($state.restore_chain).Count -gt 4096){throw 'Prepared history exceeds its recovery bound'}
 $effective=@{}
 foreach($recordPath in $state.restore_chain){
  if(-not $recordPath.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Prepared record leaves the selected game'}
  $recordFile=PreparedPath $recordPath.Substring($root.Length+1)
  $entry=@($state.restore_record_ledger|Where-Object{$_.path -ieq $recordPath});if($entry.Count -ne 1){throw 'Prepared launch requires the committed recovery ledger; repair this installation'}
  $bytes=[Text.Encoding]::UTF8.GetBytes($entry[0].record_json);$sha=[Security.Cryptography.SHA256]::Create();try{$hash=([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-','')}finally{$sha.Dispose()}
  if($hash -ne $entry[0].sha256){throw 'Prepared recovery ledger changed'}
  if([IO.File]::Exists($recordFile) -and (Get-FileHash -LiteralPath $recordFile -Algorithm SHA256).Hash -ne $hash){throw 'Prepared record differs from its committed ledger'}
  $record=$entry[0].record_json|ConvertFrom-Json
  if($record.status -ne 'installed-verified' -or $record.game_executable -ine $exe){throw 'Prepared record does not describe the current game'}
  foreach($file in $record.files){$effective[[string]$file.path]=$file}
 }
 $required=@('dxgi.dll','NeuRotic\Prepared\NeuRotic-PreparedGuides.addon32','NeuRotic\Prepared\NeuRotic.GpuHost\NeuRotic.PreparedGpuHost.exe','NeuRotic\Prepared\NeuRotic.GpuHost\OptiScaler.dll','NeuRotic\Prepared\NeuRotic.GpuHost\nvngx.dll_dlssnr.dll','NeuRotic\Prepared\NeuRotic.GpuHost\WinPixEventRuntime.dll','NeuRotic\Prepared\Start-PreparedGame.ps1')
 foreach($name in @($effective.Keys)){
  if($name -match '^NeuRotic\\Prepared\\NeuRotic\.GpuHost\\(?:nvngx_dlssnr\.dll|OptiScaler\\streamline\\[^\\]+\.dll)$' -and -not($effective[$name].PSObject.Properties.Name -contains 'installed_exists' -and -not $effective[$name].installed_exists)){$required += $name}
 }
 foreach($name in $required){
  $entry=$effective[$name];if(-not $entry -or ($entry.PSObject.Properties.Name -contains 'installed_exists' -and -not $entry.installed_exists)){throw ('Prepared component is not owned: '+$name)}
  $file=[IO.File]::Open((PreparedPath $name),[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read);$leases.Add($file);[NeuRoticPreparedIdentity]::CheckFile($file.SafeFileHandle)
  $sha=[Security.Cryptography.SHA256]::Create();try{$hash=([BitConverter]::ToString($sha.ComputeHash($file))).Replace('-','')}finally{$sha.Dispose()}
  if($hash -ne $entry.installed_hash){throw ('Prepared component changed; use Repair: '+$name)}
 }
function QuoteNr([string]$value){return '"'+[regex]::Replace([regex]::Replace($value,'(\\*)"','$1$1\"'),'(\\+)$','$1$1')+'"'}
$start=[Diagnostics.ProcessStartInfo]::new()
$start.FileName=$exe;$start.WorkingDirectory=Split-Path $exe -Parent
$start.UseShellExecute=$false;$start.WindowStyle=[Diagnostics.ProcessWindowStyle]::Normal
$start.Arguments=(@($GameArguments|ForEach-Object {QuoteNr $_}) -join ' ')
$start.EnvironmentVariables['NEUROTIC_PREPARED_GUIDES']='1'
if($SteamAppId){$start.EnvironmentVariables['SteamAppId']=$SteamAppId;$start.EnvironmentVariables['SteamGameId']=$SteamAppId}
if($CheckOnly){Write-Output ('PASS x86 prepared process launch preflight; SteamAppId='+$SteamAppId+'; no process or configuration changed');return}
$process=[Diagnostics.Process]::Start($start)
$process.WaitForExit();exit $process.ExitCode
}finally{foreach($lease in $leases){$lease.Dispose()};if($entered){$mutex.ReleaseMutex()};$mutex.Dispose();$targetLease.Dispose()}
