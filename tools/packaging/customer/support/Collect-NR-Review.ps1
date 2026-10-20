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
 public static string Io(string p) {return @"\\?\"+Full(p);}
 [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] static extern bool CreateDirectory(string p,IntPtr s);
 public static void CreateNewDirectory(string p) {
  if(!CreateDirectory(Io(p),IntPtr.Zero))throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error(),"Cannot create fresh diagnostic staging directory: "+p);
 }
 static SafeFileHandle Open(string p,bool directory) {
  var h=CreateFile(Io(p),directory?0x80u:0x80000000u,3,IntPtr.Zero,3,0x00200000u|(directory?0x02000000u:0x08000000u),IntPtr.Zero);
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
 $stream=[IO.File]::Open([NRReviewIO]::Io($Path),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
 try{$bytes=[Text.UTF8Encoding]::new($false).GetBytes(($Value|ConvertTo-Json -Depth 32));$stream.Write($bytes,0,$bytes.Length)}finally{$stream.Dispose()}
}
function Stream-Hash($Stream) { $hash=[Security.Cryptography.SHA256]::Create();try{return [BitConverter]::ToString($hash.ComputeHash($Stream)).Replace('-','')}finally{$hash.Dispose()} }
# NR_FLIGHT_METADATA_BEGIN
# Pure text metadata only. Correlation never establishes the currently running
# session, renderer eligibility, NR delivery or complete flight coverage.
function ConvertFrom-NrFlightIdentity([string]$Payload) {
 $bad=[pscustomobject]@{parsed=$false;complete=$false;reason='malformed_identity';identity=$null;key=$null}
 $fields=@('identity_schema','identity_clock','build','pid','process_creation_100ns','session','export','export_utc_100ns','provenance','rows','first_sequence','last_sequence')
 $values=@{}
 foreach($part in $Payload.Split(';')){
  $match=[regex]::Match($part.Trim(),'\A([a-z_][a-z_0-9]*)=([A-Za-z0-9_.+-]+)\z')
  if(-not $match.Success -or $values.ContainsKey($match.Groups[1].Value)){return $bad}
  $values[$match.Groups[1].Value]=$match.Groups[2].Value
 }
 foreach($field in $fields){if(-not $values.ContainsKey($field)){return $bad}}
 if($values.identity_schema -cne '1' -or $values.identity_clock -cne 'windows_filetime_100ns'){return $bad}
 $complete=$true
 if($values.build -ceq 'unknown'){$complete=$false}
 elseif($values.build.Length -gt 128){return $bad}
 foreach($field in @('session','export')){
  if($values[$field] -ceq 'unknown'){$complete=$false;continue}
  if($values[$field] -cnotmatch '\A[0-9a-fA-F]{32}\z' -or $values[$field] -ceq ('0'*32)){return $bad}
  $values[$field]=$values[$field].ToLowerInvariant()
 }
 foreach($field in @('pid','process_creation_100ns','export_utc_100ns','rows','first_sequence','last_sequence')){
  if($field -in @('pid','process_creation_100ns','export_utc_100ns') -and $values[$field] -ceq 'unknown'){$complete=$false;continue}
  [uint64]$number=0
  if($values[$field] -cnotmatch '\A(?:0|[1-9][0-9]*)\z' -or
     -not [uint64]::TryParse($values[$field],[Globalization.NumberStyles]::None,[Globalization.CultureInfo]::InvariantCulture,[ref]$number)){return $bad}
  if($field -in @('pid','process_creation_100ns','export_utc_100ns') -and $number -eq 0){return $bad}
  if($field -ceq 'pid' -and $number -gt [uint32]::MaxValue){return $bad}
 }
 if($values.provenance -cne 'manual_explicit'){$complete=$false}
 [uint64]$rows=$values.rows;[uint64]$first=$values.first_sequence;[uint64]$last=$values.last_sequence
 if(($rows -eq 0 -and ($first -ne 0 -or $last -ne 0)) -or ($rows -gt 0 -and ($first -eq 0 -or $last -lt $first))){return $bad}
 $key=($values.GetEnumerator()|Sort-Object Key|ForEach-Object {$_.Key+'='+$_.Value}) -join '; '
 return [pscustomobject]@{parsed=$true;complete=$complete;reason=$(if($complete){'identity_declared'}else{'identity_unknown'});identity=$values;key=$key}
}
function Read-NrFlightCsv([IO.TextReader]$Reader) {
 $result=[pscustomobject]@{legacy=$false;valid=$false;complete=$false;reason='invalid_csv';identity=$null;key=$null}
 $header=$Reader.ReadLine()
 if($null -eq $header -or $header.IndexOf('identity_schema=',[StringComparison]::Ordinal) -lt 0){
  $result.legacy=$true;$result.reason='legacy_or_missing_identity';return $result
 }
 if(-not $header.StartsWith('# Vulkan observation trace v1; clock=steady_ns; coverage=partial;',[StringComparison]::Ordinal)){return $result}
 $identity=ConvertFrom-NrFlightIdentity ($header.Substring($header.IndexOf('identity_schema=',[StringComparison]::Ordinal)))
 if(-not $identity.parsed){$result.reason=$identity.reason;return $result}
 $result.identity=$identity.identity
 if($Reader.ReadLine() -cne 'sequence,ticks,thread,kind,operation,evaluation,command,use,queue,detail,result'){return $result}
 [uint64]$count=0;[uint64]$first=0;[uint64]$last=0
 while($null -ne ($line=$Reader.ReadLine())){
  $columns=$line.Split(',');if($columns.Count -ne 11){return $result}
  for($i=0;$i -lt 10;++$i){
   [uint64]$number=0
   if(-not [uint64]::TryParse($columns[$i],[Globalization.NumberStyles]::None,[Globalization.CultureInfo]::InvariantCulture,[ref]$number)){return $result}
  }
  if([uint64]$columns[3] -gt [uint32]::MaxValue){return $result}
  [int32]$status=0
  if(-not [int32]::TryParse($columns[10],[Globalization.NumberStyles]::AllowLeadingSign,[Globalization.CultureInfo]::InvariantCulture,[ref]$status)){return $result}
  [uint64]$sequence=$columns[0];if($sequence -le $last){return $result}
  if($count -eq 0){$first=$sequence};$last=$sequence;++$count
 }
 if($count -ne [uint64]$identity.identity.rows -or $first -ne [uint64]$identity.identity.first_sequence -or $last -ne [uint64]$identity.identity.last_sequence){
  $result.reason='incomplete_or_mismatching_rows';return $result
 }
 $result.valid=$true;$result.complete=$identity.complete;$result.key=$identity.key;$result.reason=$identity.reason
 return $result
}
function ConvertFrom-NrFlightLogLine([string]$Line) {
 $marker='NR_VK_FLIGHT_EXPORT ';$at=$Line.IndexOf($marker,[StringComparison]::Ordinal)
 if($at -lt 0){return $null}
 if($at -gt 0 -and ([char]::IsLetterOrDigit($Line[$at-1]) -or $Line[$at-1] -eq '_')){return $null}
 $identity=ConvertFrom-NrFlightIdentity ($Line.Substring($at+$marker.Length))
 if($identity.parsed -and $identity.complete){return $identity};return $null
}
function Get-NrFlightCorrelation($Csv,[object[]]$Markers,[bool]$Changed=$false) {
 $classification='identity_unverified';$reason=$Csv.reason;$matching=@()
 if($Csv.legacy){$classification='legacy_unverified'}
 elseif($Changed){$reason='source_changed_during_collection'}
 elseif($Csv.valid -and $Csv.complete){
  $matching=@($Markers|Where-Object {[string]::Equals($_.key,$Csv.key,[StringComparison]::Ordinal)})
  if($matching.Count){$classification='correlated_exact_export';$reason='identity_and_row_bounds_match_collected_export_marker'}
  else{$reason='no_matching_stable_collected_log_marker'}
 }
 return [pscustomobject]@{classification=$classification;reason=$reason;current_session='not_established';identity=$Csv.identity;
  matching_logs=@($matching|Select-Object file,archive_member,sha256,line)}
}
# NR_FLIGHT_METADATA_END

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
 if(-not [IO.Directory]::Exists($outputRoot)){$pins.Pin([IO.Path]::GetDirectoryName($outputRoot));[IO.Directory]::CreateDirectory([NRReviewIO]::Io($outputRoot))|Out-Null}
 $pins.Pin($outputRoot)
 if($ResultPath){$ResultPath=[NRReviewIO]::Full($ResultPath);$pins.Pin([IO.Path]::GetDirectoryName($ResultPath));if(Test-Path -LiteralPath $ResultPath){throw 'Result path already exists'};$resultValidated=$true}
 $name='nr-game-'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N')
 $folder=Join-Path $outputRoot $name;[NRReviewIO]::CreateNewDirectory($folder)
 $stagingPin=[NRReviewIO]::new();$stagingPin.Pin($folder)
 $rows=[Collections.Generic.List[object]]::new();$omissions=[Collections.Generic.List[object]]::new()
 function Collect-File([string]$Source,[string]$Relative,[bool]$Copy) {
  $lock=[NRReviewIO]::new();$stream=$null;$destination=$null;$operation='source_probe'
  try {
   if(-not (Test-Path -LiteralPath $Source)){$rows.Add([pscustomobject]@{file=$Relative;present=$false;status='absent'});return}
   $operation='source_open';$lock.Pin([IO.Path]::GetDirectoryName($Source));$stream=[NRReviewIO]::Read($Source)
   $before=$stream.Length;$modified=[IO.File]::GetLastWriteTimeUtc([NRReviewIO]::Io($Source)).ToString('o')
   if($Copy){
    $member=$Relative.Replace('\','__').Replace('/','__');$destination=Join-Path $folder $member
    $operation='destination_create';$write=[IO.File]::Open([NRReviewIO]::Io($destination),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    try{$operation='destination_write';$stream.CopyTo($write,65536)}finally{$write.Dispose()}
    $operation='destination_verify';$check=[NRReviewIO]::Read($destination);try{$digest=Stream-Hash $check;$bytes=$check.Length}finally{$check.Dispose()}
   }else{$digest=Stream-Hash $stream;$bytes=$stream.Length;$member=$null}
   $changed=$before -ne $stream.Length -or $modified -ne [IO.File]::GetLastWriteTimeUtc([NRReviewIO]::Io($Source)).ToString('o')
   $rows.Add([pscustomobject]@{file=$Relative;present=$true;status=$(if($Copy){'copied'}else{'hash_only'});bytes=$bytes;last_write_utc=$modified;sha256=$digest;archive_member=$member;changed_during_collection=$changed})
   if($changed){$omissions.Add([pscustomobject]@{file=$Relative;reason='Source changed during collection; close the game and collect again for a stable snapshot'})}
  }catch{
   $rows.Add([pscustomobject]@{file=$Relative;present=$true;status='unreadable_or_refused';operation=$operation;reason=$_.Exception.Message})
   $omissions.Add([pscustomobject]@{file=$Relative;reason=$_.Exception.Message})
   if($destination -and [IO.File]::Exists([NRReviewIO]::Io($destination))){[IO.File]::Delete([NRReviewIO]::Io($destination))}
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
 if([IO.File]::Exists([NRReviewIO]::Io($ini))){
  try{
   if(([IO.FileInfo]::new([NRReviewIO]::Io($ini))).Length -gt 8388608){throw 'INI exceeds 8 MiB; configured log discovery skipped'}
   $inLog=$false
   foreach($line in [IO.File]::ReadAllLines([NRReviewIO]::Io($ini))){
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
   $operation='destination_create';$write=[IO.File]::Open([NRReviewIO]::Io($destination),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
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
 # Inspect only the exact already-copied staging snapshots, preserving raw files.
 # Missing/old/unverified metadata is diagnostic evidence, never an NR/render gate.
 $flightMarkers=[Collections.Generic.List[object]]::new();$flightEvidence=[Collections.Generic.List[object]]::new()
 foreach($log in @($rows.ToArray()|Where-Object {$_.status -ceq 'copied' -and $_.file -like '*.log' -and -not $_.changed_during_collection})){
  $reader=$null
  try{
   $reader=[IO.StreamReader]::new([NRReviewIO]::Read((Join-Path $folder $log.archive_member)));[uint64]$lineNumber=0
   while($null -ne ($line=$reader.ReadLine())){
    ++$lineNumber;$marker=ConvertFrom-NrFlightLogLine $line
    if($null -ne $marker){$flightMarkers.Add([pscustomobject]@{key=$marker.key;file=$log.file;archive_member=$log.archive_member;sha256=$log.sha256;line=$lineNumber})}
   }
  }catch{$omissions.Add([pscustomobject]@{file=$log.file;reason=('Flight marker metadata unavailable: '+$_.Exception.Message)})}
  finally{if($reader){$reader.Dispose()}}
 }
 foreach($csv in @($rows.ToArray()|Where-Object {[IO.Path]::GetFileName($_.file) -ieq 'NeuRotic-Vulkan-trace.csv'})){
  $reader=$null
  $evidence=[pscustomobject]@{classification=$(if($csv.status -ceq 'absent'){'absent'}else{'identity_unverified'});
   reason='manual_export_not_collected';current_session='not_established';identity=$null;matching_logs=@()}
  try{
   if($csv.status -ceq 'copied'){
    $reader=[IO.StreamReader]::new([NRReviewIO]::Read((Join-Path $folder $csv.archive_member)))
    $evidence=Get-NrFlightCorrelation (Read-NrFlightCsv $reader) @($flightMarkers.ToArray()) ([bool]$csv.changed_during_collection)
   }
  }catch{$evidence.reason='flight_metadata_unavailable'}finally{if($reader){$reader.Dispose()}}
  $flightEvidence.Add([pscustomobject]@{file=$csv.file;archive_member=$csv.archive_member;sha256=$csv.sha256;
   classification=$evidence.classification;reason=$evidence.reason;current_session=$evidence.current_session;
   identity=$evidence.identity;matching_logs=@($evidence.matching_logs)})
 }
 $report=[ordered]@{schema_version=3;kind='NR_NATIVE_ADA_MFG_REVIEW';collected_utc=[DateTime]::UtcNow.ToString('o');game_directory=$game;game_executable=$GameExecutable;
  flight_csv_evidence=@($flightEvidence.ToArray());
  collector='shared-bat-hub';files=@($rows.ToArray());streamline_modules=@($streamline.ToArray());fallback_diagnostics=@($fallbacks.ToArray());omissions=@($omissions.ToArray());
  scope='Known game-local native, ReShade and prepared renderer diagnostics including numbered/configured local logs, installer receipts, optional cached Hub inspection, selected-target admission fallbacks, bounded prepared binary hashes and module inventory. Missing optional files are recorded. External configured logs, opt-in developer captures, unrelated Hub/game data and binary payloads are excluded.';
  provider_execution='NOT_RUN_BY_COLLECTOR';uploads='NONE'}
 Save-Json (Join-Path $folder 'COLLECTION.json') $report
 Add-Type -AssemblyName System.IO.Compression.FileSystem
 $archive=$folder+'.zip';[IO.Compression.ZipFile]::CreateFromDirectory([NRReviewIO]::Io($folder),[NRReviewIO]::Io($archive),[IO.Compression.CompressionLevel]::Optimal,$true)
 # Preserve staging on failure. Delete only owned flat files after full SHA verification.
 $staged=@(([IO.DirectoryInfo]::new([NRReviewIO]::Io($folder))).GetFileSystemInfos())
 if(@($staged|Where-Object {$_.Attributes -band ([IO.FileAttributes]::Directory -bor [IO.FileAttributes]::ReparsePoint)}).Count){throw 'Unexpected staging contents; preserving folder'}
 $zip=[IO.Compression.ZipFile]::OpenRead([NRReviewIO]::Io($archive))
 try{
  $entries=@($zip.Entries|Where-Object {$_.Name})
  if($entries.Count -ne $staged.Count){throw 'Archive file count mismatch; preserving folder'}
  foreach($file in $staged){
   $entry=@($entries|Where-Object {$_.FullName.Replace('\','/') -ceq ($name+'/'+$file.Name)})
   if($entry.Count -ne 1 -or $entry[0].Length -ne $file.Length){throw 'Archive member mismatch; preserving folder'}
   $stream=$entry[0].Open();try{$digest=Stream-Hash $stream}finally{$stream.Dispose()}
   $stream=[NRReviewIO]::Read((Join-Path $folder $file.Name));try{if($digest -ne (Stream-Hash $stream)){throw 'Archive hash mismatch; preserving folder'}}finally{$stream.Dispose()}
  }
 }finally{$zip.Dispose()}
 foreach($file in $staged){[IO.File]::Delete($file.FullName)}
 $stagingPin.Dispose();[IO.Directory]::Delete([NRReviewIO]::Io($folder),$false)
 $archiveStream=[NRReviewIO]::Read($archive);try{$archiveDigest=Stream-Hash $archiveStream}finally{$archiveStream.Dispose()}
 $receipt=[ordered]@{success=$true;path=$archive;status=$(if($omissions.Count){'ZIP exported with omissions; see COLLECTION.json.'}else{'Diagnostic ZIP exported.'});omission_count=$omissions.Count;sha256=$archiveDigest}
 if($ResultPath){Save-Json $ResultPath $receipt}
 Write-Host "REVIEW_ZIP=$archive"
}catch{
 $failure=$_.Exception.Message
 if($resultValidated -and -not (Test-Path -LiteralPath $ResultPath)){
  try{Save-Json $ResultPath ([ordered]@{success=$false;status=('Collection failed: '+$failure+'. Partial files, if any, remain in the chosen folder.');path=''})}catch{}
 }
 throw
}finally{if($stagingPin){$stagingPin.Dispose()};$pins.Dispose()}
