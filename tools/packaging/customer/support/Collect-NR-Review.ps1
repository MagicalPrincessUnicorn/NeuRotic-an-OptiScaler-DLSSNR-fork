param([string]$GameDirectory,[string]$OutputDirectory,[string]$GameExecutable,[string]$HubSnapshotPath,[string]$ResultPath)
$ErrorActionPreference='Stop'
# Shared BAT/Hub collector. Stream known files in full; hash binaries only.
if(-not ('NRReviewIO' -as [type])) { Add-Type -TypeDefinition @'
using System;
using System.IO;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;
public sealed class NRReviewIO : IDisposable {
 [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] static extern SafeFileHandle CreateFile(string n,uint a,uint s,IntPtr p,uint c,uint f,IntPtr t);
 [DllImport("kernel32.dll", SetLastError=true)] static extern bool GetFileInformationByHandle(SafeFileHandle h,out Info i);
 [StructLayout(LayoutKind.Sequential)] struct Info { public uint attr; public System.Runtime.InteropServices.ComTypes.FILETIME c,a,w; public uint volume,hi,lo,links,indexHi,indexLo; }
 readonly List<SafeFileHandle> locks=new List<SafeFileHandle>();
 public static string Full(string p) {
  if(String.IsNullOrWhiteSpace(p)||p.Length<3||p[1]!=':'||(p[2]!='\\'&&p[2]!='/')||!Char.IsLetter(p[0])||p.IndexOf(':',2)>=0||p.IndexOf('\0')>=0)throw new IOException("A local ordinary absolute path is required");
  string full=Path.GetFullPath(p).TrimEnd('\\');if(full.Length==2)full+="\\";return full;
 }
 static SafeFileHandle Open(string p,bool directory) {
  var h=CreateFile(p,directory?0x80u:0x80000000u,3,IntPtr.Zero,3,0x00200000u|(directory?0x02000000u:0x08000000u),IntPtr.Zero);
  Info i;if(h.IsInvalid||!GetFileInformationByHandle(h,out i)||(i.attr&0x400)!=0||((i.attr&0x10)!=0)!=directory){h.Dispose();throw new IOException("Unreadable, linked or unexpected path: "+p);}return h;
 }
 public void Pin(string path) {
  path=Full(path);string current=Path.GetPathRoot(path);locks.Add(Open(current,true));
  foreach(string part in path.Substring(current.Length).Split(new[]{'\\','/'},StringSplitOptions.RemoveEmptyEntries)){current=Path.Combine(current,part);locks.Add(Open(current,true));}
 }
 public static FileStream Read(string path) {return new FileStream(Open(Full(path),false),FileAccess.Read,65536,false);}
 public void Dispose(){foreach(var h in locks)h.Dispose();locks.Clear();}
}
'@ }
function Save-Json([string]$Path,$Value) {
 $stream=[IO.File]::Open($Path,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
 try{$bytes=[Text.UTF8Encoding]::new($false).GetBytes(($Value|ConvertTo-Json -Depth 32));$stream.Write($bytes,0,$bytes.Length)}finally{$stream.Dispose()}
}
function Stream-Hash($Stream) { $hash=[Security.Cryptography.SHA256]::Create();try{return [BitConverter]::ToString($hash.ComputeHash($Stream)).Replace('-','')}finally{$hash.Dispose()} }
$pins=[NRReviewIO]::new();$stagingPin=$null;$resultValidated=$false
try {
 if(-not $GameDirectory){$GameDirectory=Read-Host 'Paste the game EXE folder (close the game first)'}
 $game=[NRReviewIO]::Full($GameDirectory.Trim('"'));$pins.Pin($game)
 if($GameExecutable){
  $exe=[NRReviewIO]::Full($GameExecutable)
  if([IO.Path]::GetDirectoryName($exe).TrimEnd('\') -ine $game.TrimEnd('\')){throw 'Selected executable is outside the selected game directory'}
  $probe=[NRReviewIO]::Read($exe);$probe.Dispose()
 }
 if(-not $OutputDirectory){$OutputDirectory=Join-Path (Split-Path $PSScriptRoot -Parent) 'reviews'}
 $outputRoot=[NRReviewIO]::Full($OutputDirectory)
 if(-not [IO.Directory]::Exists($outputRoot)){$pins.Pin([IO.Path]::GetDirectoryName($outputRoot));[IO.Directory]::CreateDirectory($outputRoot)|Out-Null}
 $pins.Pin($outputRoot)
 if($ResultPath){$ResultPath=[NRReviewIO]::Full($ResultPath);$pins.Pin([IO.Path]::GetDirectoryName($ResultPath));if(Test-Path -LiteralPath $ResultPath){throw 'Result path already exists'};$resultValidated=$true}
 $name='nr-game-'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N')
 $folder=Join-Path $outputRoot $name;New-Item -ItemType Directory -Path $folder -ErrorAction Stop|Out-Null
 $stagingPin=[NRReviewIO]::new();$stagingPin.Pin($folder)
 $rows=[Collections.Generic.List[object]]::new();$omissions=[Collections.Generic.List[object]]::new()
 function Collect-File([string]$Source,[string]$Relative,[bool]$Copy) {
  $lock=[NRReviewIO]::new();$stream=$null;$destination=$null
  try {
   if(-not (Test-Path -LiteralPath $Source)){$rows.Add([pscustomobject]@{file=$Relative;present=$false;status='absent'});return}
   $lock.Pin([IO.Path]::GetDirectoryName($Source));$stream=[NRReviewIO]::Read($Source)
   $before=$stream.Length;$modified=[IO.File]::GetLastWriteTimeUtc($Source).ToString('o')
   if($Copy){
    $member=$Relative.Replace('\','__').Replace('/','__');$destination=Join-Path $folder $member
    $write=[IO.File]::Open($destination,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    try{$stream.CopyTo($write,65536)}finally{$write.Dispose()}
    $check=[NRReviewIO]::Read($destination);try{$digest=Stream-Hash $check;$bytes=$check.Length}finally{$check.Dispose()}
   }else{$digest=Stream-Hash $stream;$bytes=$stream.Length;$member=$null}
   $changed=$before -ne $stream.Length -or $modified -ne [IO.File]::GetLastWriteTimeUtc($Source).ToString('o')
   $rows.Add([pscustomobject]@{file=$Relative;present=$true;status=$(if($Copy){'copied'}else{'hash_only'});bytes=$bytes;last_write_utc=$modified;sha256=$digest;archive_member=$member;changed_during_collection=$changed})
   if($changed){$omissions.Add([pscustomobject]@{file=$Relative;reason='Source changed during collection; close the game and collect again for a stable snapshot'})}
  }catch{
   $rows.Add([pscustomobject]@{file=$Relative;present=$true;status='unreadable_or_refused';reason=$_.Exception.Message})
   $omissions.Add([pscustomobject]@{file=$Relative;reason=$_.Exception.Message})
   if($destination -and [IO.File]::Exists($destination)){[IO.File]::Delete($destination)}
  }finally{if($stream){$stream.Dispose()};$lock.Dispose()}
 }
 $helperRelative='NeuRotic\Prepared\NeuRotic.GpuHost';$helperRoot=Join-Path $game $helperRelative
 foreach($relative in @('OptiScaler.log','OptiScaler.ini','ReShade.log','ReShade.ini','NeuRotic-NR-admission.json','NeuRotic-Vulkan-trace.csv',
  ($helperRelative+'\OptiScaler.log'),($helperRelative+'\OptiScaler.ini'),($helperRelative+'\NeuRotic-NR-admission.json'),($helperRelative+'\NeuRotic-Vulkan-trace.csv'),
  'NeuRotic\Installer\Current-Install.json','NeuRotic\Installer\Uninstall-Transaction.json','NeuRotic\Installer\Cleanup-Pending.json','NeuRotic\UserData\OptiScaler.ini')){Collect-File (Join-Path $game $relative) $relative $true}
 # Config.cpp emits <stem>.log or <stem>_<ticks>.log. Relative configured
 # directories are discarded by the runtime; external absolute logs stay out.
 function Collect-LocalLogs([string]$LogRoot,[string]$Prefix) {
 $logStems=[Collections.Generic.List[string]]::new();$logStems.Add('OptiScaler')
 $iniRelative=$(if($Prefix){$Prefix+'\OptiScaler.ini'}else{'OptiScaler.ini'})
 $ini=Join-Path $folder ($iniRelative.Replace('\','__'))
 if([IO.File]::Exists($ini)){
  try{
   if((Get-Item -LiteralPath $ini).Length -gt 8388608){throw 'INI exceeds 8 MiB; configured log discovery skipped'}
   $inLog=$false
   foreach($line in [IO.File]::ReadAllLines($ini)){
    if($line -match '^\s*\[([^\]]+)\]'){$inLog=$Matches[1] -ieq 'Log';continue}
    if(-not $inLog -or $line -notmatch '^\s*LogFileName\s*=\s*(.*?)\s*$'){continue}
    $configured=$Matches[1].Trim().Trim('"')
    if(-not $configured -or $configured -ieq 'auto'){continue}
    if([IO.Path]::IsPathRooted($configured) -and [IO.Path]::GetDirectoryName([NRReviewIO]::Full($configured)).TrimEnd('\') -ine $LogRoot.TrimEnd('\')){
     $omissions.Add([pscustomobject]@{file=$configured;reason='Configured log is outside its selected runtime directory; not collected'});continue
    }
    $stem=[IO.Path]::GetFileNameWithoutExtension($configured)
    if($stem -and $stem -notin $logStems){$logStems.Add($stem)}
   }
  }catch{$omissions.Add([pscustomobject]@{file=$iniRelative;reason=('Configured log discovery unavailable: '+$_.Exception.Message)})}
 }
 if(-not [IO.Directory]::Exists($LogRoot)){return}
 $logPin=[NRReviewIO]::new()
 try{$logPin.Pin($LogRoot)
 foreach($item in @(Get-ChildItem -LiteralPath $LogRoot -Filter '*.log' -File -Force|Select-Object -First 256)){
  if($item.Name -ieq 'OptiScaler.log'){continue}
  foreach($stem in $logStems){
   if($item.Name -match ('^'+[regex]::Escape($stem)+'(?:_\d+)?\.log$')){$relative=$(if($Prefix){$Prefix+'\'+$item.Name}else{$item.Name});Collect-File $item.FullName $relative $true;break}
  }
 }
 }catch{$omissions.Add([pscustomobject]@{file=$Prefix;reason=('Runtime log inventory refused: '+$_.Exception.Message)})}finally{$logPin.Dispose()}
 }
 Collect-LocalLogs $game ''
 Collect-LocalLogs $helperRoot $helperRelative
 # The prepared addon and helper live in two fixed game-local directories.
 # Hash all immediate binary files so private/vendor model filenames remain
 # evidence without ever placing their binary payloads in the archive.
 foreach($binaryDirectory in @('NeuRotic\Prepared',$helperRelative,($helperRelative+'\_storage_'))){
  $binaryRoot=Join-Path $game $binaryDirectory;if(-not [IO.Directory]::Exists($binaryRoot)){continue}
  $binaryPin=[NRReviewIO]::new()
  try{$binaryPin.Pin($binaryRoot)
   foreach($item in @(Get-ChildItem -LiteralPath $binaryRoot -File -Force|Select-Object -First 256)){
    if($item.Extension -notin @('.dll','.exe','.addon32','.addon64')){continue}
    Collect-File $item.FullName ($binaryDirectory+'\'+$item.Name) $false
   }
  }catch{$omissions.Add([pscustomobject]@{file=$binaryDirectory;reason=('Prepared binary inventory refused: '+$_.Exception.Message)})}finally{$binaryPin.Dispose()}
 }
 foreach($relative in @('nvngx.dll_dlssnr.dll','nvngx_dlssnr.dll','dxgi.dll','OptiScaler.dll','OptiScaler.asi','version.dll','winmm.dll','dbghelp.dll','d3d12.dll','wininet.dll','winhttp.dll','ReShade64.dll',
  '_storage_\nvngx.dll_dlssnr.dll','_storage_\nvngx_dlssnr.dll','_storage_\dxgi.dll')){Collect-File (Join-Path $game $relative) $relative $false}
 if($HubSnapshotPath){Collect-File ([NRReviewIO]::Full($HubSnapshotPath)) 'HUB-INSPECTION.json' $true}
 $fallbacks=[Collections.Generic.List[object]]::new()
 foreach($item in @(Get-ChildItem -LiteralPath $env:TEMP -Filter 'NeuRotic-NR-admission-*.json' -File -ErrorAction SilentlyContinue)){
  if($item.LastWriteTimeUtc -lt [DateTime]::UtcNow.AddDays(-1)){continue}
  $lock=[NRReviewIO]::new();$reader=$null
  try{
   $lock.Pin($item.DirectoryName);$stream=[NRReviewIO]::Read($item.FullName)
   if($stream.Length -gt 1048576){$stream.Dispose();throw 'Fallback JSON exceeds 1 MiB'}
   $reader=[IO.StreamReader]::new($stream);$text=$reader.ReadToEnd();$record=$text|ConvertFrom-Json
   $providerParent=[IO.Path]::GetDirectoryName([NRReviewIO]::Full($record.requested_provider)).TrimEnd('\')
   if($providerParent -notin @($game.TrimEnd('\'),$helperRoot.TrimEnd('\'))){continue}
   if($providerParent -ieq $helperRoot.TrimEnd('\')){$lock.Pin($helperRoot)}
   # Export the exact parsed snapshot; reopening could admit changed target binding.
   $destination=Join-Path $folder $item.Name
   $write=[IO.File]::Open($destination,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
   try{$bytes=[Text.UTF8Encoding]::new($false).GetBytes($text);$write.Write($bytes,0,$bytes.Length)}finally{$write.Dispose()}
   $check=[NRReviewIO]::Read($destination);try{$digest=Stream-Hash $check}finally{$check.Dispose()}
   $fallbacks.Add([pscustomobject]@{file=$item.Name;last_write_utc=$item.LastWriteTimeUtc.ToString('o');sha256=$digest})
  }catch{$omissions.Add([pscustomobject]@{file=$item.Name;reason='Fallback unavailable or unbound: '+$_.Exception.Message})}
  finally{if($reader){$reader.Dispose()};$lock.Dispose()}
 }
 $streamline=[Collections.Generic.List[object]]::new();$pending=[Collections.Generic.Stack[string]]::new();$pending.Push($game);$visited=0
 while($pending.Count){
  if(++$visited -gt 10000){$omissions.Add([pscustomobject]@{file='streamline_modules';reason='Inventory stopped at 10000 directories'});break}
  $directory=$pending.Pop();$lock=[NRReviewIO]::new()
  try{
   $lock.Pin($directory)
   foreach($child in Get-ChildItem -LiteralPath $directory -Force -ErrorAction Stop){
    if($child.Attributes -band [IO.FileAttributes]::ReparsePoint){$omissions.Add([pscustomobject]@{file=$child.FullName;reason='Linked path skipped'});continue}
    if($child.PSIsContainer){if($child.Name -notin @('NeuRotic-backups','NeuRotic-test-backups','reviews') -and -not $child.FullName.StartsWith($outputRoot+'\',[StringComparison]::OrdinalIgnoreCase) -and $child.FullName -ine $outputRoot){$pending.Push($child.FullName)};continue}
    if($child.Name -notin @('nvngx_dlssg.dll','sl.dlss_g.dll')){continue}
    $relative=$child.FullName.Substring($game.Length).TrimStart('\');Collect-File $child.FullName $relative $false
    $row=$rows[$rows.Count-1]
    $streamline.Add([pscustomobject]@{file=$relative;bytes=$row.bytes;sha256=$row.sha256;status=$row.status;file_version=$child.VersionInfo.FileVersion;product_version=$child.VersionInfo.ProductVersion})
   }
  }catch{$omissions.Add([pscustomobject]@{file=$directory;reason=$_.Exception.Message})}finally{$lock.Dispose()}
 }
 $report=[ordered]@{schema_version=3;kind='NR_NATIVE_ADA_MFG_REVIEW';collected_utc=[DateTime]::UtcNow.ToString('o');game_directory=$game;game_executable=$GameExecutable;
  collector='shared-bat-hub';files=@($rows.ToArray());streamline_modules=@($streamline.ToArray());fallback_diagnostics=@($fallbacks.ToArray());omissions=@($omissions.ToArray());
  scope='Known game-local native, ReShade and prepared renderer diagnostics including numbered/configured local logs, installer receipts, optional cached Hub inspection, selected-target admission fallbacks, bounded prepared binary hashes and module inventory. Missing optional files are recorded. External configured logs, opt-in developer captures, unrelated Hub/game data and binary payloads are excluded.';
  provider_execution='NOT_RUN_BY_COLLECTOR';uploads='NONE'}
 Save-Json (Join-Path $folder 'COLLECTION.json') $report
 Add-Type -AssemblyName System.IO.Compression.FileSystem
 $archive=$folder+'.zip';[IO.Compression.ZipFile]::CreateFromDirectory($folder,$archive,[IO.Compression.CompressionLevel]::Optimal,$true)
 # Preserve staging on failure. Delete only owned flat files after full SHA verification.
 $staged=@(Get-ChildItem -LiteralPath $folder -Force)
 if(@($staged|Where-Object {$_.PSIsContainer -or ($_.Attributes -band [IO.FileAttributes]::ReparsePoint)}).Count){throw 'Unexpected staging contents; preserving folder'}
 $zip=[IO.Compression.ZipFile]::OpenRead($archive)
 try{
  $entries=@($zip.Entries|Where-Object {$_.Name})
  if($entries.Count -ne $staged.Count){throw 'Archive file count mismatch; preserving folder'}
  foreach($file in $staged){
   $entry=@($entries|Where-Object {$_.FullName.Replace('\','/') -ceq ($name+'/'+$file.Name)})
   if($entry.Count -ne 1 -or $entry[0].Length -ne $file.Length){throw 'Archive member mismatch; preserving folder'}
   $stream=$entry[0].Open();try{$digest=Stream-Hash $stream}finally{$stream.Dispose()}
   $stream=[NRReviewIO]::Read($file.FullName);try{if($digest -ne (Stream-Hash $stream)){throw 'Archive hash mismatch; preserving folder'}}finally{$stream.Dispose()}
  }
 }finally{$zip.Dispose()}
 foreach($file in $staged){[IO.File]::Delete($file.FullName)}
 $stagingPin.Dispose();[IO.Directory]::Delete($folder,$false)
 $receipt=[ordered]@{success=$true;path=$archive;status=$(if($omissions.Count){'ZIP exported with omissions; see COLLECTION.json.'}else{'Diagnostic ZIP exported.'});omission_count=$omissions.Count;sha256=(Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash}
 if($ResultPath){Save-Json $ResultPath $receipt}
 Write-Host "REVIEW_ZIP=$archive"
}catch{
 $failure=$_.Exception.Message
 if($resultValidated -and -not (Test-Path -LiteralPath $ResultPath)){
  try{Save-Json $ResultPath ([ordered]@{success=$false;status=('Collection failed: '+$failure+'. Partial files, if any, remain in the chosen folder.');path=''})}catch{}
 }
 throw
}finally{if($stagingPin){$stagingPin.Dispose()};$pins.Dispose()}
