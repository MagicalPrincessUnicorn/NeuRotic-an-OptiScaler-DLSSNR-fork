[CmdletBinding()]
param(
    [string]$GameExecutable,
    [ValidateSet("DXVK","dgVoodoo2")] [string]$ConnectionWrapper,
    [string]$ConnectionDependencyManifest,
    [ValidateSet('dxgi.dll','winmm.dll','version.dll','dbghelp.dll','d3d12.dll','wininet.dll','winhttp.dll','OptiScaler.asi','OptiScaler.dll')]
    [string]$ProxyName,
    [ValidateSet('RenameReShade','Replace','ChooseAnother','Cancel')]
    [string]$ExistingProxyAction,
    [ValidateSet('Rename','Delete','Cancel')]
    [string]$ExistingDxgiAction,
    [switch]$CheckOnly,
    [switch]$ConfirmInstall,
    [switch]$ContinueWithoutRuntime,
    [switch]$IncludeInspector,
    [string]$HubExpectedRuntimeIdentity,
    [switch]$Restore,
    [switch]$Uninstall,
    [switch]$Installed,
    [string]$GameDirectory,
    [ValidateSet('Update','Repair','ChangeProxy','Uninstall','Cancel')]
    [string]$ExistingInstallAction,
    [switch]$AdoptModifiedInstall,
    [switch]$RetireObsoleteOptiScalerProxy,
    [ValidateSet('KeepSettings','RemoveSettings','Full')]
    [string]$UninstallMode,
    [switch]$RemoveBackupRecords,
    [ValidateSet('dxgi.dll','winmm.dll','version.dll','dbghelp.dll','d3d12.dll','wininet.dll','winhttp.dll','OptiScaler.asi','OptiScaler.dll')]
    [string]$ManualProxyName,
    [string]$LegacyManifest,
    [switch]$RemovePrivateModel,
    [switch]$ConfirmUninstall,
    [string]$HubRequestPath,
    [string]$HubResultPath,
    [string]$HubPackageRoot,
    [string]$HubPlanPreviewPath,
    [string]$HubOperationResultPath,
    [string]$FileDecisionsJson,
    [switch]$HubNonInteractive
)
Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'
$script:UninstallCompleted = $false
$explicitProxyAction=$ExistingProxyAction
$script:RestoreRecordLedger=@{}
$script:RestoreMetadataMissing=$false

# A decision authorizes exactly the observed bytes of exactly one destination.
# Confirmation of the overall operation is never consent to unknown file loss.
function Resolve-InstallFileDecisions($Files,$Observed,[string]$Json,[switch]$NonInteractive,[string]$LegacyProxy,[string]$LegacyAction,[switch]$Review) {
    $known=@{};foreach($entry in $Observed){$known[[string]$entry.path]=$entry}
    $provided=@{};$decisions=@{};$conflicts=@()
    if($Json){
        foreach($entry in @($Json|ConvertFrom-Json)){
            if(-not $entry.path -or $entry.sha256 -notmatch '^[a-fA-F0-9]{64}$' -or $entry.action -notin @('SaveAndReplace','Replace','Skip','Cancel')){throw 'Invalid per-file decision.'}
            if($provided.ContainsKey([string]$entry.path)){throw 'Duplicate per-file decision.'}
            $provided[[string]$entry.path]=$entry
        }
    }
    foreach($file in $Files){
        if(-not $file.existed -or $file.operation -eq 'preserve-existing' -or (Test-SettingsTarget $file.path) -or $file.previous_hash -eq $file.installed_hash){continue}
        $owned=$known[[string]$file.path]
        if($owned -and (-not ($owned.PSObject.Properties.Name -contains 'changed') -or -not $owned.changed) -and $owned.sha256 -eq $file.previous_hash){continue}
        $decision=$provided[[string]$file.path]
        if($decision){
            if($decision.sha256 -ne $file.previous_hash){
                if($Review){$conflicts+=[pscustomobject]@{path=[string]$file.path;sha256=[string]$file.previous_hash};continue}
                throw ('File changed since its decision: '+$file.path)
            }
            $decisions[[string]$file.path]=[string]$decision.action
        }elseif($file.path -ieq $LegacyProxy -and $LegacyAction -in @('Replace','RenameReShade')){
            $decisions[[string]$file.path]='SaveAndReplace'
        }elseif($NonInteractive){
            $conflicts+= [pscustomobject]@{path=[string]$file.path;sha256=[string]$file.previous_hash}
        }else{
            Write-Host ($file.path+' appears to not have come from NeuRotic. How should we proceed?')
            Write-Host '1. Save and Replace';Write-Host '2. Replace';Write-Host '3. Skip';Write-Host '0. Cancel'
            $answer=(Read-Host 'Choose 0, 1, 2, or 3').Trim()
            $decisions[[string]$file.path]=$(switch($answer){'1'{'SaveAndReplace'}'2'{'Replace'}'3'{'Skip'}default{'Cancel'}})
        }
    }
    foreach($path in $provided.Keys){
        if($Review -and @($conflicts|Where-Object{$_.path -ieq $path}).Count){continue}
        if(-not @($Files|Where-Object{$_.path -ieq $path -and $_.existed -and $_.previous_hash -eq $provided[$path].sha256}).Count){throw ('File decision is no longer applicable: '+$path)}
    }
    return [pscustomobject]@{conflicts=@($conflicts);decisions=$decisions}
}

# Draw through PowerShell so CMD launchers do not depend on a batch code page.
Write-Host @'
███╗   ██╗███████╗██╗   ██╗██████╗  ██████╗ ████████╗██╗ ██████╗
████╗  ██║██╔════╝██║   ██║██╔══██╗██╔═══██╗╚══██╔══╝██║██╔════╝
██╔██╗ ██║█████╗  ██║   ██║██████╔╝██║   ██║   ██║   ██║██║
██║╚██╗██║██╔══╝  ██║   ██║██╔══██╗██║   ██║   ██║   ██║██║
██║ ╚████║███████╗╚██████╔╝██║  ██║╚██████╔╝   ██║   ██║╚██████╗
╚═╝  ╚═══╝╚══════╝ ╚═════╝ ╚═╝  ╚═╝ ╚═════╝    ╚═╝   ╚═╝ ╚═════╝
'@
Write-Host ''

# Native console hosts draw the bar; redirected CMD output keeps readable stages.
function Show-SetupProgress([string]$Stage,[int]$Done=0,[int]$Total=0,[string]$Detail='') {
    $percent = $(if ($Total -gt 0) { [Math]::Min(100,[int](100.0*$Done/$Total)) } else { -1 })
    $status = 'Please wait - ' + $(if ($Total -gt 0) { "$Done of $Total files" } else { 'working' })
    if ($Detail) { $status += " - $Detail" }
    if (-not [Console]::IsOutputRedirected) {
        Write-Progress -Id 1 -Activity $Stage -Status $status -PercentComplete $percent
    }
    $now = [DateTime]::UtcNow
    if (-not (Get-Variable -Name lastProgressStage -Scope Script -ErrorAction SilentlyContinue) -or
        $script:lastProgressStage -ne $Stage -or ($Total -gt 0 -and $Done -eq $Total) -or
        ($now - $script:lastProgressTime).TotalSeconds -ge 1) {
        Write-Host ("{0}: {1}" -f $Stage,$status)
        if($HubNonInteractive){Write-Host ('[NR-PROGRESS] '+(@{stage=$Stage;done=$Done;total=$Total}|ConvertTo-Json -Compress))}
        $script:lastProgressStage = $Stage; $script:lastProgressTime = $now
    }
}
function Complete-SetupProgress {
    if (-not [Console]::IsOutputRedirected) { Write-Progress -Id 1 -Activity 'NeuRotic' -Completed }
}

# Preserved user edits are separate from rollback caches and Full cleanup targets.
function Save-ChangedFiles([string]$GameRoot,$Files,$Journal=$null) {
    $existing = @($Files | Where-Object { $_.existed } | Sort-Object path -Unique)
    if (-not $existing.Count) { return }
    $folder = SafePath $GameRoot ('NeuRotic-preserved-files\changes-' + [Guid]::NewGuid().ToString('N').Substring(0,12))
    $entries = @(); $index = 0
    Show-SetupProgress 'Preserving changed files' 0 $existing.Count
    foreach ($file in $existing) {
        $source = SafePath $GameRoot $file.path
        if ($Journal) {
            $entry = @($Journal.files | Where-Object { $_.path -eq $file.path })[0]
            $source = SafePath (SafePath $GameRoot $Journal.recovery) (Get-UninstallSnapshotRelative $entry)
        }
        if ((HashFile $source) -ne $file.sha256) { throw "File changed before preservation: $source" }
        $snapshot = 'f\' + ('{0:x4}.bin' -f $index)
        CopyVerified $source (SafePath $folder $snapshot) $file.sha256
        $entries += [pscustomobject]@{path=$file.path;snapshot=$snapshot;sha256=$file.sha256}
        $index++; Show-SetupProgress 'Preserving changed files' $index $existing.Count $file.path
    }
    SaveRecord (Join-Path $folder 'FILES.json') ([ordered]@{kind='neurotic-preserved-files';game_directory=$GameRoot;created_utc=[DateTime]::UtcNow.ToString('o');files=$entries})
    Write-Host "Your changed files are preserved in: $folder (kept even by Full cleanup)."
}

function Assert-LiveFiles([string]$GameRoot,$Files) {
    foreach ($file in $Files) {
        $path = SafePath $GameRoot $file.path
        $exists = Test-Path -LiteralPath $path -PathType Leaf
        if (((Test-Path -LiteralPath $path) -and -not $exists) -or $exists -ne $file.existed -or
            ($exists -and (HashFile $path) -ne $file.sha256)) { throw "File changed while planning the operation: $path" }
    }
}

function Get-RelocatedProxies([string]$GameRoot,[string[]]$Chain) {
    if (-not $Chain.Count) { return }
    $newest = Read-InstallRecord $Chain[0]
    $loader = @($newest.files | Where-Object { $_.path -ieq $newest.selected_proxy })
    if ($loader.Count -ne 1) { throw 'The recorded loader is missing or ambiguous.' }
    $recorded = @{}
    foreach ($recordPath in $Chain) {
        foreach ($file in (Read-InstallRecord $recordPath).files) {
            if (-not $recorded.ContainsKey([string]$file.path)) { $recorded[[string]$file.path] = $file }
        }
    }
    foreach ($name in $allowedProxies) {
        if ($name -ieq $newest.selected_proxy) { continue }
        if ($recorded.ContainsKey($name)) {
            $expected = $recorded[$name]
            $expectsFile = -not ($expected.PSObject.Properties.Name -contains 'installed_exists') -or [bool]$expected.installed_exists
            if ($expectsFile -and $expected.installed_hash -eq $loader[0].installed_hash) { continue }
        }
        $path = SafePath $GameRoot $name
        if ((Test-Path -LiteralPath $path -PathType Leaf) -and (HashFile $path) -eq $loader[0].installed_hash) {
            [pscustomobject]@{path=$name;existed=$true;sha256=$loader[0].installed_hash;recorded=$recorded.ContainsKey($name)}
        }
    }
}

function Initialize-NeuroticFileSafety {
 if('NeuRoticFileSafety' -as [type]){return}
 Add-Type -TypeDefinition @'
using System;
using System.IO;
using System.Text;
using System.Collections.Generic;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using Microsoft.Win32.SafeHandles;
public static class NeuRoticFileSafety {
 [StructLayout(LayoutKind.Sequential)] struct Info {
  public uint Attributes; public System.Runtime.InteropServices.ComTypes.FILETIME Creation,Access,Write;
  public uint Volume,SizeHigh,SizeLow,Links,IndexHigh,IndexLow;
 }
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern SafeFileHandle CreateFileW(string p,uint access,uint share,IntPtr security,uint creation,uint flags,IntPtr template);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool GetFileInformationByHandle(SafeFileHandle h,out Info info);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern uint GetFinalPathNameByHandleW(SafeFileHandle h,StringBuilder p,uint length,uint flags);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool SetFileInformationByHandle(SafeFileHandle h,int kind,ref byte value,uint size);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool SetFileInformationByHandle(SafeFileHandle h,int kind,IntPtr value,uint size);
 static Exception Error(string p){return new IOException("Safe file operation refused: "+p,new Win32Exception(Marshal.GetLastWin32Error()));}
 static string Final(SafeFileHandle h){var b=new StringBuilder(32768);uint n=GetFinalPathNameByHandleW(h,b,32768,1);if(n==0||n>=32768)throw Error("canonical volume path");return b.ToString().TrimEnd('\\');}
 static Info Check(SafeFileHandle h,string path,bool directory){
  Info i;if(h.IsInvalid||!GetFileInformationByHandle(h,out i))throw Error(path);
  if((i.Attributes&0x400)!=0||((i.Attributes&0x10)!=0)!=directory||(!directory&&i.Links!=1))throw new IOException("Linked or unexpected target refused: "+path);
  return i;
 }
 public sealed class Lease:IDisposable {
  internal readonly List<SafeFileHandle> Handles=new List<SafeFileHandle>();
  internal string Parent;
  public void Dispose(){for(int i=Handles.Count-1;i>=0;i--)Handles[i].Dispose();Handles.Clear();}
 }
 public static Lease Parents(string path,bool create){
  var result=new Lease();try{
   string full=Path.GetFullPath(path);string directory=Path.GetDirectoryName(full);var pending=new Stack<string>();
   while(!String.IsNullOrEmpty(directory)){pending.Push(directory);string next=Path.GetDirectoryName(directory);if(next==directory)break;directory=next;}
   string previous=null;
   while(pending.Count>0){string item=pending.Pop();
    if(!Directory.Exists(item)){if(!create)throw new IOException("Parent missing: "+item);Directory.CreateDirectory(item);}
    var h=CreateFileW(item,0x80000000,3,IntPtr.Zero,3,0x02200000,IntPtr.Zero);
    try{Check(h,item,true);string actual=Final(h);
     if(previous!=null&&!actual.Equals(previous+"\\"+Path.GetFileName(item),StringComparison.OrdinalIgnoreCase))throw new IOException("Parent identity changed: "+item);
     previous=actual;result.Parent=actual;result.Handles.Add(h);h=null;
    }finally{if(h!=null)h.Dispose();}
   }
   return result;
  }catch{result.Dispose();throw;}
 }
 static SafeFileHandle Open(string path,uint access,uint creation,bool directory,Lease parents,uint share=1){
  var h=CreateFileW(Path.GetFullPath(path),access,share,IntPtr.Zero,creation,0x00200000|(directory?0x02000000u:0u),IntPtr.Zero);
  try{Check(h,path,directory);string expected=parents.Parent+"\\"+Path.GetFileName(Path.GetFullPath(path).TrimEnd('\\'));
   if(!Final(h).Equals(expected,StringComparison.OrdinalIgnoreCase))throw new IOException("Opened target identity differs: "+path);
   return h;
  }catch{h.Dispose();throw;}
 }
 static string Hash(Stream stream){using(var sha=SHA256.Create()){return BitConverter.ToString(sha.ComputeHash(stream)).Replace("-","");}}
 public static string Identity(string directory){
  using(var parents=Parents(Path.Combine(directory,"identity-placeholder"),false)){
   var h=parents.Handles[parents.Handles.Count-1];Info i=Check(h,directory,true);
   return i.Volume.ToString("X8")+i.IndexHigh.ToString("X8")+i.IndexLow.ToString("X8");
  }
 }
 public static Lease PinTarget(string directory){return Parents(Path.Combine(directory,"target-placeholder"),false);}
 public static void Copy(string source,string destination,string expected,string current){
  try {
  using(var parents=Parents(source,false))using(var handle=Open(source,0x80000000,3,false,parents))using(var input=new FileStream(handle,FileAccess.Read)){
   // The bundled Character Inspector OpenCV module is about 71 MiB. Keep a
   // bounded allocation while permitting that verified offline dependency.
   if(input.Length>268435456)throw new IOException("Managed copy exceeds 256 MiB");
   if(!Hash(input).Equals(expected,StringComparison.OrdinalIgnoreCase))throw new IOException("Source changed: "+source);
   input.Position=0;byte[] bytes=new byte[(int)input.Length];int offset=0;while(offset<bytes.Length){int n=input.Read(bytes,offset,bytes.Length-offset);if(n==0)throw new EndOfStreamException();offset+=n;}
   if(!Write(bytes,destination,expected,current)){var e=new IOException("Destination changed at copy boundary: "+destination);e.Data["NeuRoticRefusedPath"]=destination;throw e;}
  }
  }catch(Exception e){if(!e.Data.Contains("NeuRoticWriteStarted")||!(bool)e.Data["NeuRoticWriteStarted"])e.Data["NeuRoticRefusedPath"]=destination;throw;}
 }
 static void PublicationFixtureBoundary(string destination,string phase){
  if(Environment.GetEnvironmentVariable("NEUROTIC_PUBLICATION_TEST_BOUNDARY")!=phase)return;
  string root=Environment.GetEnvironmentVariable("NEUROTIC_PUBLICATION_TEST_ROOT");
  if(String.IsNullOrEmpty(root)||!Path.GetDirectoryName(Path.GetFullPath(destination)).Equals(Path.GetFullPath(root).TrimEnd('\\'),StringComparison.OrdinalIgnoreCase))throw new IOException("Publication fault injection requires its exact fixture directory");
  System.Diagnostics.Process.GetCurrentProcess().Kill();
 }
 public static bool Write(byte[] bytes,string destination,string expected,string current){
  using(var input=new MemoryStream(bytes,false)){if(!Hash(input).Equals(expected,StringComparison.OrdinalIgnoreCase))throw new IOException("Staged content hash mismatch: "+destination);}
  using(var parents=Parents(destination,true)){
   SafeFileHandle existing=null;FileStream old=null;
   string stage=destination+".nr-stage-"+Guid.NewGuid().ToString("N");
   try{
    if(File.Exists(destination)){
     if(current=="Absent")return false;
     existing=Open(destination,0x80000000,3,false,parents,5);old=new FileStream(existing,FileAccess.Read);
     if(!String.IsNullOrEmpty(current)&&!Hash(old).Equals(current,StringComparison.OrdinalIgnoreCase))return false;
    }else if(!String.IsNullOrEmpty(current)&&current!="Absent")return false;
    using(var handle=Open(stage,0xC0010000,1,false,parents))using(var stream=new FileStream(handle,FileAccess.ReadWrite)){
     bool published=false;try{
      PublicationFixtureBoundary(destination,"stage-created");
      stream.Write(bytes,0,bytes.Length);stream.Flush(true);stream.Position=0;
      PublicationFixtureBoundary(destination,"stage-flushed");
      if(!Hash(stream).Equals(expected,StringComparison.OrdinalIgnoreCase))throw new IOException("Staged bytes failed verification: "+destination);
      // Keep the verified old inode open without write sharing. Windows atomic
      // replacement needs delete sharing; recheck the destination file identity
      // immediately before rename while all directory ancestors remain pinned.
      // Unsupported filesystems fail here, before touching the live destination.
      byte[] name=Encoding.Unicode.GetBytes(Path.GetFullPath(destination));int rootOffset=IntPtr.Size==8?8:4;int lengthOffset=rootOffset+IntPtr.Size;int nameOffset=lengthOffset+4;
      IntPtr info=Marshal.AllocHGlobal(nameOffset+name.Length+2);
      try{
       if(existing!=null)using(var observed=Open(destination,0x80000000,3,false,parents,5)){
        Info a=Check(existing,destination,false),b=Check(observed,destination,false);
        if(a.Volume!=b.Volume||a.IndexHigh!=b.IndexHigh||a.IndexLow!=b.IndexLow)return false;
       }
       for(int i=0;i<nameOffset;i++)Marshal.WriteByte(info,i,0);
       Marshal.WriteInt32(info,existing==null?0:3);Marshal.WriteInt32(info,lengthOffset,name.Length);Marshal.Copy(name,0,IntPtr.Add(info,nameOffset),name.Length);Marshal.WriteInt16(info,nameOffset+name.Length,0);
       PublicationFixtureBoundary(destination,"before-publish");
       if(!SetFileInformationByHandle(handle,22,info,(uint)(nameOffset+name.Length+2)))throw Error(destination);
       PublicationFixtureBoundary(destination,"after-publish");
       published=true;return true;
      }finally{Marshal.FreeHGlobal(info);}
     }finally{if(!published){byte remove=1;SetFileInformationByHandle(handle,4,ref remove,1);}}
    }
   }finally{if(old!=null)old.Dispose();else if(existing!=null)existing.Dispose();}
  }
 }
 public static void DeleteDirectory(string path,string expectedIdentity){
  using(var parents=Parents(path,false))using(var handle=Open(path,0x10000u,3,true,parents)){
   Info i=Check(handle,path,true);
   string identity=i.Volume.ToString("X8")+i.IndexHigh.ToString("X8")+i.IndexLow.ToString("X8");
   if(!identity.Equals(expectedIdentity,StringComparison.Ordinal))throw new IOException("Directory identity changed: "+path);
   // The same verified directory handle is removed nonrecursively. A file
   // replacement or a newly nonempty directory must never be deleted here.
   byte remove=1;if(!SetFileInformationByHandle(handle,4,ref remove,1))throw Error(path);
  }
 }
 public static void Delete(string path,string expected){
  bool directory=Directory.Exists(path);
  using(var parents=Parents(path,false))using(var handle=Open(path,directory?0x10000u:0x80010000u,3,directory,parents)){
   if(!directory&&!String.IsNullOrEmpty(expected))using(var stream=new FileStream(handle,FileAccess.Read)){
    if(!Hash(stream).Equals(expected,StringComparison.OrdinalIgnoreCase)){var e=new IOException("File changed at deletion boundary: "+path);e.Data["NeuRoticRefusedPath"]=path;throw e;}
    byte removeFile=1;if(!SetFileInformationByHandle(handle,4,ref removeFile,1))throw Error(path);
    return;
   }
   byte remove=1;if(!SetFileInformationByHandle(handle,4,ref remove,1))throw Error(path);
  }
 }
}
'@
}
function Remove-NeuroticPath([string]$LiteralPath,[switch]$Force,[string]$ExpectedHash){
 Initialize-NeuroticFileSafety
 if(-not $ExpectedHash -and (Test-Path -LiteralPath $LiteralPath -PathType Leaf)){$ExpectedHash=HashFile $LiteralPath}
 [NeuRoticFileSafety]::Delete($LiteralPath,$ExpectedHash)
}
function Ensure-NeuroticDirectory([string]$Path){
 Initialize-NeuroticFileSafety
 $lease=[NeuRoticFileSafety]::Parents((Join-Path $Path 'directory-placeholder'),$true)
 $lease.Dispose()
}
function Get-NeuroticFailureProperty($Exception,[string]$Name){
 while($Exception){if($Exception.Data.Contains($Name)){return $Exception.Data[$Name]};$Exception=$Exception.InnerException}
 return $null
}
function HashFile([string]$Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash }
function HashBytes([byte[]]$Bytes) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($Bytes))).Replace('-','') }
    finally { $sha.Dispose() }
}
function Get-NeuroticRuntimeProjectionIdentity($Files){
    $identity=@($Files|Where-Object{$_.PSObject.Properties.Name -contains 'component_generation'}|Sort-Object path|ForEach-Object{
        [ordered]@{path=[string]$_.path;source=[string]$_.source;sha256=[string]$_.installed_hash;generation=[string]$_.component_generation}
    })
    return HashBytes ([Text.Encoding]::UTF8.GetBytes((ConvertTo-Json -InputObject $identity -Depth 4 -Compress)))
}
function Get-UninstallSnapshotRelative($Entry) {
    if ($Entry.PSObject.Properties.Name -contains 'snapshot') {
        $snapshot = [string]$Entry.snapshot
        if ($snapshot -notmatch '^(?:f\\[a-fA-F0-9]{4}|files\\[a-fA-F0-9]{64})\.bin$') { throw 'Invalid uninstall recovery snapshot path.' }
        return $snapshot
    }
    return [string]$Entry.path
}
function SaveRecord([string]$Path, $Value) {
    Initialize-NeuroticFileSafety
    $bytes=[Text.Encoding]::UTF8.GetBytes(($Value | ConvertTo-Json -Depth 20))
    $before=if(Test-Path -LiteralPath $Path -PathType Leaf){HashFile $Path}else{'Absent'}
    if(-not [NeuRoticFileSafety]::Write($bytes,$Path,(HashBytes $bytes),$before)){throw "Metadata changed at publication boundary: $Path"}
}
function SafePath([string]$Root, [string]$Relative) {
    if ([string]::IsNullOrWhiteSpace($Relative) -or [IO.Path]::IsPathRooted($Relative) -or
        $Relative -match '[:*?]' -or @($Relative -split '[\\/]' | Where-Object { $_ -in @('..','.','') }).Count) {
        throw "Invalid relative path: $Relative"
    }
    $base = [IO.Path]::GetFullPath($Root).TrimEnd('\')
    $path = [IO.Path]::GetFullPath([IO.Path]::Combine($base,$Relative))
    if (-not $path.StartsWith($base + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Path leaves destination.' }
    $walk = $path
    while ($walk) {
        # Check every ancestor on every call; no cache may hide a newly added link.
        # Native attribute reads avoid repeated PowerShell provider enumeration.
        try {
            if ([IO.File]::GetAttributes($walk) -band [IO.FileAttributes]::ReparsePoint) {
                throw "Linked destinations are not supported: $walk"
            }
        } catch [IO.FileNotFoundException] {} catch [IO.DirectoryNotFoundException] {}
        $walk = [IO.Path]::GetDirectoryName($walk)
    }
    return $path
}
function CopyVerified([string]$Source, [string]$Destination, [string]$Hash, [string]$ExpectedCurrentHash) {
    Initialize-NeuroticFileSafety
    if(-not $ExpectedCurrentHash){$ExpectedCurrentHash=$(if(Test-Path -LiteralPath $Destination -PathType Leaf){HashFile $Destination}else{'Absent'})}
    [NeuRoticFileSafety]::Copy($Source,$Destination,$Hash,$ExpectedCurrentHash)
}
function WriteVerifiedBytes([byte[]]$Bytes, [string]$Destination, [string]$Hash, [string]$ExpectedCurrentHash) {
    Initialize-NeuroticFileSafety
    $written=[NeuRoticFileSafety]::Write($Bytes,$Destination,$Hash,$ExpectedCurrentHash)
    if($ExpectedCurrentHash){return $written}
}
function CheckGameClosed([string]$Executable) {
    $selected=[IO.Path]::GetFullPath($Executable)
    foreach($process in @(Get-Process -Name ([IO.Path]::GetFileNameWithoutExtension($Executable)) -ErrorAction SilentlyContinue)) {
        $path=$null;try{$path=$process.Path}catch{}
        if(-not $path){throw ('Cannot verify executable identity for process '+$process.Id+'. Close that process or retry with access to its executable path.')}
        if([string]::Equals([IO.Path]::GetFullPath($path),$selected,[StringComparison]::OrdinalIgnoreCase)){throw 'Close the game before continuing.'}
    }
}

# Shared by BAT, Hub and recovery. No file is created in the target to lock it.
function Enter-NeuroticTargetLock([string]$Root) {
    Initialize-NeuroticFileSafety
    $lease=[NeuRoticFileSafety]::PinTarget($Root)
    $identity=[NeuRoticFileSafety]::Identity($Root)
    $mutex = New-Object Threading.Mutex($false, ('Global\NeuRotic-Installer-' + $identity))
    try { $entered = $mutex.WaitOne(0) } catch [Threading.AbandonedMutexException] { $entered = $true }
    if (-not $entered) { $mutex.Dispose();$lease.Dispose(); throw 'Another installer operation owns this target. Try again after it finishes.' }
    if (-not (Get-Variable -Name NeuroticTargetLocks -Scope Script -ErrorAction SilentlyContinue)) { $script:NeuroticTargetLocks = New-Object Collections.Generic.List[object] }
    $script:NeuroticTargetLocks.Add($mutex)
    $script:NeuroticTargetLocks.Add($lease)
    return $mutex
}
function Get-NeuroticLockCount {
 if(Get-Variable -Name NeuroticTargetLocks -Scope Script -ErrorAction SilentlyContinue){return $script:NeuroticTargetLocks.Count};return 0
}
function Release-NeuroticTargetLocks([int]$Start=0){
 if(-not (Get-Variable -Name NeuroticTargetLocks -Scope Script -ErrorAction SilentlyContinue)){return}
 for($i=$script:NeuroticTargetLocks.Count-1;$i -ge $Start;$i--){
  $item=$script:NeuroticTargetLocks[$i]
  if($item -is [Threading.Mutex]){try{$item.ReleaseMutex()}catch{}}
  $item.Dispose();$script:NeuroticTargetLocks.RemoveAt($i)
 }
}

# Return edited bytes while retaining the source file's encoding, BOM and line endings.
function PlanLoadReshadeEdit([byte[]]$Bytes) {
    $offset = 0
    $preamble = New-Object byte[] 0
    if ($Bytes.Length -ge 4 -and $Bytes[0] -eq 0x00 -and $Bytes[1] -eq 0x00 -and $Bytes[2] -eq 0xFE -and $Bytes[3] -eq 0xFF) {
        $encoding = New-Object Text.UTF32Encoding($true,$true,$true); $offset = 4
    } elseif ($Bytes.Length -ge 4 -and $Bytes[0] -eq 0xFF -and $Bytes[1] -eq 0xFE -and $Bytes[2] -eq 0x00 -and $Bytes[3] -eq 0x00) {
        $encoding = New-Object Text.UTF32Encoding($false,$true,$true); $offset = 4
    } elseif ($Bytes.Length -ge 3 -and $Bytes[0] -eq 0xEF -and $Bytes[1] -eq 0xBB -and $Bytes[2] -eq 0xBF) {
        $encoding = New-Object Text.UTF8Encoding($true,$true); $offset = 3
    } elseif ($Bytes.Length -ge 2 -and $Bytes[0] -eq 0xFE -and $Bytes[1] -eq 0xFF) {
        $encoding = New-Object Text.UnicodeEncoding($true,$true,$true); $offset = 2
    } elseif ($Bytes.Length -ge 2 -and $Bytes[0] -eq 0xFF -and $Bytes[1] -eq 0xFE) {
        $encoding = New-Object Text.UnicodeEncoding($false,$true,$true); $offset = 2
    } else {
        $encoding = New-Object Text.UTF8Encoding($false,$true)
        try { [void]$encoding.GetString($Bytes) }
        catch { $encoding = [Text.Encoding]::Default }
    }
    if ($offset) { $preamble = $Bytes[0..($offset-1)] }
    $text = $encoding.GetString($Bytes, $offset, $Bytes.Length - $offset)
    $pattern = '(?im)^(?<prefix>[ \t]*LoadReshade[ \t]*=[ \t]*)(?<value>[^;\r\n]*?)(?<suffix>[ \t]*(?:;[^\r\n]*)?)(?<ending>\r?)$'
    $matches = [regex]::Matches($text, $pattern)
    if ($matches.Count -gt 1) { throw 'OptiScaler.ini contains more than one LoadReshade setting.' }
    if ($matches.Count -eq 1) {
        $match = $matches[0]
        $replacement = $match.Groups['prefix'].Value + 'true' + $match.Groups['suffix'].Value + $match.Groups['ending'].Value
        $text = $text.Substring(0,$match.Index) + $replacement + $text.Substring($match.Index + $match.Length)
    } else {
        $newline = $(if ($text.Contains("`r`n")) { "`r`n" } else { "`n" })
        $plugins = [regex]::Match($text, '(?im)^[ \t]*\[Plugins\][ \t]*\r?$')
        if ($plugins.Success) {
            $sectionRegex = New-Object regex '(?im)^[ \t]*\[[^\]\r\n]+\][ \t]*\r?$'
            $next = $sectionRegex.Match($text, $plugins.Index + $plugins.Length)
            $insertAt = $(if ($next.Success) { $next.Index } else { $text.Length })
            $before = $text.Substring(0,$insertAt)
            if (-not $before.EndsWith($newline)) { $before += $newline }
            $text = $before + 'LoadReshade = true' + $newline + $text.Substring($insertAt)
        } else {
            if ($text.Length -and -not $text.EndsWith($newline)) { $text += $newline }
            $text += '[Plugins]' + $newline + 'LoadReshade = true' + $newline
        }
    }
    $body = $encoding.GetBytes($text)
    $result = New-Object byte[] ($preamble.Length + $body.Length)
    if ($preamble.Length) { [Array]::Copy($preamble,0,$result,0,$preamble.Length) }
    [Array]::Copy($body,0,$result,$preamble.Length,$body.Length)
    return [pscustomobject]@{Bytes=$result;Encoding=$encoding.WebName;HadBom=($preamble.Length -gt 0)}
}

$allowedProxies = @('dxgi.dll','winmm.dll','version.dll','dbghelp.dll','d3d12.dll','wininet.dll','winhttp.dll','OptiScaler.asi','OptiScaler.dll')
function SelectProxyName {
    Write-Host ''
    Write-Host 'Choose the filename the game should load NeuRotic as:'
    Write-Host '1. dxgi.dll        (normal first choice for DirectX games)'
    Write-Host '2. winmm.dll       (normal first choice for Vulkan games)'
    Write-Host '3. version.dll'
    Write-Host '4. dbghelp.dll'
    Write-Host '5. d3d12.dll'
    Write-Host '6. wininet.dll'
    Write-Host '7. winhttp.dll'
    Write-Host '8. OptiScaler.asi'
    Write-Host '9. OptiScaler.dll'
    Write-Host '0. Cancel'
    $choice = (Read-Host 'Choose 0 through 9').Trim()
    if ($choice -eq '0') { return $null }
    $index = 0
    if (-not [int]::TryParse($choice,[ref]$index) -or $index -lt 1 -or $index -gt $allowedProxies.Count) {
        Write-Host 'That is not a valid choice.'
        return SelectProxyName
    }
    return $allowedProxies[$index-1]
}
function SelectExistingAction([string]$Name) {
    Write-Host ''
    Write-Host "A $Name already exists in this game folder. What would you like to do?"
    Write-Host ''
    if ($Name -ieq 'dxgi.dll') {
        Write-Host '1. Replace the file (Backup of original will be created)'
        Write-Host '2. Rename to ReShade64.dll - Choose this if you want to use NeuRotic and ReShade'
        Write-Host '3. Choose a different Filename'
        Write-Host '4. Cancel'
        $choice = (Read-Host 'Choose 1, 2, 3, or 4').Trim()
        return $(switch ($choice) { '1' {'Replace'} '2' {'RenameReShade'} '3' {'ChooseAnother'} default {'Cancel'} })
    }
    Write-Host "1. Back it up, delete it, and install NeuRotic as $Name"
    Write-Host '2. Choose a different filename'
    Write-Host '3. Cancel'
    $choice = (Read-Host 'Choose 1, 2, or 3').Trim()
    return $(switch ($choice) { '1' {'Replace'} '2' {'ChooseAnother'} default {'Cancel'} })
}
function AllowedTarget([string]$Relative) {
    return ($Relative -in $allowedProxies -or $Relative -in @('d3d8.dll','d3d9.dll','dgVoodoo.conf','ReShade64.dll','nvngx.dll_dlssnr.dll','OptiScaler.ini','ReShade.ini','NeuRotic-LICENSE.txt','NeuRotic-PreparedGuides.addon64','WinPixEventRuntime.dll','NeuRotic.Fsr3.Vulkan.dll') -or
        $Relative -match '^(OptiScaler|Licenses|NeuRotic\.GpuHost|NeuRotic\\Prepared)\\[^:]+$')
}

function Test-SettingsTarget([string]$Relative) {
    return $Relative -in @('OptiScaler.ini','ReShade.ini','NeuRotic\Prepared\NeuRotic.GpuHost\OptiScaler.ini')
}

function Get-RecordConfigName($Record) {
    # Earlier trusted restore records predate explicit configuration metadata.
    if($Record.PSObject.Properties.Name -notcontains 'original_ini'){return 'OptiScaler.ini'}
    $name=[string]$Record.original_ini.name
    if($name -notin @('OptiScaler.ini','ReShade.ini')){throw 'Unexpected managed configuration identity.'}
    return $name
}

function Get-ManagedReShadeRename([string[]]$Chain) {
    foreach($recordPath in $Chain){
        $record=Read-InstallRecord $recordPath
        $renamed=@($record.files|Where-Object{$_.path -ieq 'ReShade64.dll' -and ($_.PSObject.Properties.Name -contains 'operation') -and $_.operation -eq 'rename-existing-proxy'})
        if($renamed.Count){return [pscustomobject]@{record=$record;file=$renamed[0]}}
    }
    return $null
}

function Get-ReShadePathList([string]$Value) {
    $items=New-Object System.Collections.Generic.List[object]
    $start=0
    for($index=0;$index -lt $Value.Length;$index++){
        if($Value[$index] -ne ','){continue}
        # ReShade escapes a literal comma by doubling it. Only an unmatched
        # comma separates paths; retain raw spelling for the eventual write.
        if($index+1 -lt $Value.Length -and $Value[$index+1] -eq ','){$index++;continue}
        $raw=$Value.Substring($start,$index-$start)
        $items.Add([pscustomobject]@{raw=$raw;value=$raw.Replace(',,',',').Trim()})
        $start=$index+1
    }
    $raw=$Value.Substring($start)
    $items.Add([pscustomobject]@{raw=$raw;value=$raw.Replace(',,',',').Trim()})
    return $items.ToArray()
}

function Merge-ReShadePathList([string]$Existing,[string]$Prepared) {
    $existingItems=@(Get-ReShadePathList $Existing)
    $paths=@($existingItems|ForEach-Object{$_.value})
    $result=$Existing
    foreach($item in @(Get-ReShadePathList $Prepared)){
        if(-not $item.value -or $item.value -in $paths){continue}
        $raw=$item.raw.Trim()
        if($result.Length){$result+=','}
        $result+=$raw
        $paths+=$item.value
    }
    return $result
}

# Package identity chooses the loader; a picker or filename never selects bitness.
function Get-SetupPEBits([string]$Path) {
    $stream=[IO.File]::OpenRead($Path);$reader=New-Object IO.BinaryReader($stream)
    try {
        if($stream.Length -lt 64 -or $reader.ReadUInt16() -ne 0x5a4d){throw 'Installation architecture requires a PE file'}
        $stream.Position=60;$offset=$reader.ReadUInt32()
        if($offset -gt $stream.Length-26){throw 'Invalid installation PE header offset'}
        $stream.Position=$offset;if($reader.ReadUInt32() -ne 0x4550){throw 'Invalid installation PE signature'}
        $machine=$reader.ReadUInt16();$stream.Position=$offset+24;$magic=$reader.ReadUInt16()
        if($machine -eq 0x14c -and $magic -eq 0x10b){return 32}
        if($machine -eq 0x8664 -and $magic -eq 0x20b){return 64}
        throw 'Unsupported installation architecture'
    } finally {$reader.Dispose();$stream.Dispose()}
}

# Only package-owned routing keys are merged. Other sections, comments, BOM and
# line endings survive, and the entire original file is in the restore chain.
function Plan-PreparedReShadeEdit([byte[]]$Bytes,[byte[]]$PackageBytes) {
    $offset=0;$encoding=New-Object Text.UTF8Encoding($false,$true)
    if($Bytes.Length -ge 4 -and $Bytes[0] -eq 0xff -and $Bytes[1] -eq 0xfe -and $Bytes[2] -eq 0 -and $Bytes[3] -eq 0){$encoding=New-Object Text.UTF32Encoding($false,$true,$true);$offset=4}
    elseif($Bytes.Length -ge 4 -and $Bytes[0] -eq 0 -and $Bytes[1] -eq 0 -and $Bytes[2] -eq 0xfe -and $Bytes[3] -eq 0xff){$encoding=New-Object Text.UTF32Encoding($true,$true,$true);$offset=4}
    elseif($Bytes.Length -ge 3 -and $Bytes[0] -eq 0xef -and $Bytes[1] -eq 0xbb -and $Bytes[2] -eq 0xbf){$offset=3}
    elseif($Bytes.Length -ge 2 -and $Bytes[0] -eq 0xff -and $Bytes[1] -eq 0xfe){$encoding=New-Object Text.UnicodeEncoding($false,$true,$true);$offset=2}
    elseif($Bytes.Length -ge 2 -and $Bytes[0] -eq 0xfe -and $Bytes[1] -eq 0xff){$encoding=New-Object Text.UnicodeEncoding($true,$true,$true);$offset=2}
    if(-not $offset){try{[void]$encoding.GetString($Bytes)}catch{$encoding=[Text.Encoding]::Default}}
    $text=$encoding.GetString($Bytes,$offset,$Bytes.Length-$offset)
    $newline=$(if($text.Contains("`r`n")){"`r`n"}else{"`n"})
    $owned=@{GENERAL=@('EffectSearchPaths','TextureSearchPaths','PresetPath','PreprocessorDefinitions','PerformanceMode');ADDON=@('AddonPath');NeuRoticPreparedGuides=@('FlowBackend','ProbeContent','SharedGpu','TransportOnly')}
    $section='';$defaults=[Text.Encoding]::UTF8.GetString($PackageBytes).TrimStart([char]0xfeff)
    foreach($line in ($defaults -split '\r?\n')) {
        if($line -match '^\s*\[([^\]]+)\]\s*$'){$section=$Matches[1];continue}
        if($line -notmatch '^\s*([^=;]+?)\s*=(.*)$'){continue}
        $key=$Matches[1].Trim();$value=$Matches[2].Trim()
        if(-not $owned.ContainsKey($section) -or $key -notin $owned[$section]){continue}
        $sections=[regex]::Matches($text,'(?im)^\s*\['+[regex]::Escape($section)+'\][ \t]*\r?$')
        if($sections.Count -gt 1){throw "Duplicate ReShade routing section: $section"}
        if(-not $sections.Count){if($text.Length -and -not $text.EndsWith($newline)){$text+=$newline};$text+='['+$section+']'+$newline+$key+'='+$value+$newline;continue}
        $start=$sections[0].Index+$sections[0].Length
        $next=[regex]::Match($text.Substring($start),'(?m)^[ \t]*\[')
        $end=$(if($next.Success){$start+$next.Index}else{$text.Length})
        $body=$text.Substring($start,$end-$start)
        $matches=[regex]::Matches($body,'(?im)^(?<prefix>[ \t]*'+[regex]::Escape($key)+'[ \t]*=[ \t]*)(?<value>[^\r\n]*)(?<ending>\r?)$')
        if($matches.Count -gt 1){throw "Duplicate ReShade routing key: $section/$key"}
        if($matches.Count){
            $match=$matches[0];$old=$match.Groups['value'].Value.Trim()
            if($key -in @('EffectSearchPaths','TextureSearchPaths','AddonPath')){$value=Merge-ReShadePathList $old $value}
            elseif($key -eq 'PreprocessorDefinitions'){$values=@($old -split ','|Where-Object{$_ -and $_.Trim() -notmatch '^NRPG_MV_PROVIDER\s*='});$value=(@($values)+@($value -split ','|Where-Object{$_.Trim() -match '^NRPG_MV_PROVIDER\s*='})) -join ','}
            $replacement=$match.Groups['prefix'].Value+$value+$match.Groups['ending'].Value
            $body=$body.Substring(0,$match.Index)+$replacement+$body.Substring($match.Index+$match.Length)
        }else{if(-not $body.EndsWith($newline)){$body+=$newline};$body+=$key+'='+$value+$newline}
        $text=$text.Substring(0,$start)+$body+$text.Substring($end)
    }
    $bodyBytes=$encoding.GetBytes($text);$result=New-Object byte[] ($offset+$bodyBytes.Length)
    if($offset){[Array]::Copy($Bytes,0,$result,0,$offset)};[Array]::Copy($bodyBytes,0,$result,$offset,$bodyBytes.Length)
    return [pscustomobject]@{Bytes=$result;Encoding=$encoding.WebName;HadBom=($offset -gt 0)}
}

# Prior experimental installers owned these exact basenames. Recognizing their
# recovery records does not admit new flagship payloads or import their runtime.
function AllowedRestoreTarget([string]$Relative) {
    return ((AllowedTarget $Relative) -or $Relative -in @('NeuRotic-PreparedGuides.addon64','WinPixEventRuntime.dll','Uninstall NeuRotic.cmd','NeuRotic\Installer\NeuRotic-Setup-Engine.ps1','nvngx_dlssnr.dll') -or $Relative -match '^streamline\\(?:sl\.[a-z_]+|nvngx_dlssg)\.dll$')
}
function Get-NeuroticUninstallLauncherBytes {
    return ,([Text.Encoding]::ASCII.GetBytes("@echo off`r`nsetlocal`r`ntitle Uninstall NeuRotic`r`nset `"PSModulePath=`"`r`n`"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe`" -NoProfile -ExecutionPolicy Bypass -File `"%~dp0NeuRotic\Installer\NeuRotic-Setup-Engine.ps1`" -Uninstall -Installed -GameDirectory `"%~dp0.`" %*`r`nset `"result=%ERRORLEVEL%`"`r`npause`r`nexit /b %result%`r`n"))
}
function Get-NeuroticPendingTransaction([string]$Root) {
    foreach($name in @('Install-Transaction.json','Restore-Transaction.json','Uninstall-Transaction.json','Cleanup-Pending.json','History-Transaction.json')){
        if(Test-Path -LiteralPath (SafePath $Root ('NeuRotic\Installer\'+$name))){return $name}
    }
    foreach($folder in @('NeuRotic-backups','NeuRotic-test-backups')){
        $base=SafePath $Root $folder;if(-not(Test-Path -LiteralPath $base -PathType Container)){continue}
        foreach($directory in @(Get-ChildItem -LiteralPath $base -Directory -Force)){
            $path=SafePath $Root ($folder+'\'+$directory.Name+'\INSTALL-MANIFEST.json')
            if(-not(Test-Path -LiteralPath $path -PathType Leaf)){continue}
            $record=Get-Content -LiteralPath $path -Raw|ConvertFrom-Json
            if($record.status -in @('backed-up','failed-rollback-incomplete')){return $path}
        }
    }
    return $null
}
function Invoke-NeuroticInstallBoundary([string]$Root,[string]$Stage){
    if(-not $env:NEUROTIC_INSTALL_TEST_BOUNDARY){return}
    if(-not $env:NEUROTIC_INSTALL_TEST_ROOT -or [IO.Path]::GetFullPath($env:NEUROTIC_INSTALL_TEST_ROOT).TrimEnd('\') -ine $Root){throw 'Install fault injection requires its exact isolated fixture root'}
    if($env:NEUROTIC_INSTALL_TEST_BOUNDARY -ceq $Stage){[Diagnostics.Process]::GetCurrentProcess().Kill()}
}
function New-NeuroticMutationJournal([string]$Root,[string]$Kind,$Writes,[string]$RecordPath=$null) {
    $pending=Get-NeuroticPendingTransaction $Root
    if($pending -and $pending -ine $RecordPath){throw ('Finish pending recovery before changing files: '+$pending)}
    $relative='NeuRotic-backups\transaction-'+[Guid]::NewGuid().ToString('N')
    $entries=@();$index=0;$seen=@{}
    foreach($write in $Writes){
        if($seen.ContainsKey([string]$write.path)){throw 'Duplicate mutation target'};$seen[[string]$write.path]=$true
        $target=SafePath $Root $write.path;$exists=Test-Path -LiteralPath $target -PathType Leaf
        $before=if($exists){HashFile $target}else{'Absent'};$snapshot='f\'+$index.ToString('x4')+'.bin';$index++
        if($exists){CopyVerified $target (SafePath (SafePath $Root $relative) $snapshot) $before 'Absent'}
        $entries+= [pscustomobject]@{path=$write.path;before=$before;after=[string]$write.after;snapshot=$snapshot}
    }
    Initialize-NeuroticFileSafety
    $journal=[pscustomobject]@{kind='neurotic-mutation';operation=$Kind;root=$Root;rootIdentity=[NeuRoticFileSafety]::Identity($Root);recovery=$relative;record=$RecordPath;files=$entries}
    SaveRecord (SafePath $Root ('NeuRotic\Installer\'+$Kind+'-Transaction.json')) $journal
    return $journal
}
function Complete-NeuroticMutation([string]$Root,$Journal) {
    $path=SafePath $Root ('NeuRotic\Installer\'+$Journal.operation+'-Transaction.json')
    Remove-NeuroticPath -LiteralPath $path -ExpectedHash (HashFile $path)
    if($Journal.operation -eq 'Install'){
        foreach($entry in $Journal.files){if($entry.before -ne 'Absent'){
            try{Remove-OwnedFile $Root ($Journal.recovery+'\'+$entry.snapshot) $entry.before}catch{Write-Warning 'Completed transaction snapshot cleanup was deferred; original restore records remain intact.'}
        }}
    }
}
function Restore-NeuroticMutation([string]$Root,[string]$Kind) {
    $path=SafePath $Root ('NeuRotic\Installer\'+$Kind+'-Transaction.json');$journal=Get-Content -LiteralPath $path -Raw|ConvertFrom-Json
    Initialize-NeuroticFileSafety
    if($journal.kind -ne 'neurotic-mutation' -or $journal.operation -cne $Kind -or $journal.root -ine $Root -or $journal.rootIdentity -ne [NeuRoticFileSafety]::Identity($Root) -or $journal.recovery -notmatch '^NeuRotic-backups\\transaction-[a-f0-9]{32}$' -or @($journal.files).Count -gt 10000){throw 'Invalid mutation recovery identity'}
    $seen=@{};$live=@{}
    foreach($file in $journal.files){
        if($seen.ContainsKey([string]$file.path) -or $file.snapshot -notmatch '^f\\[a-f0-9]{4}\.bin$' -or $file.before -notmatch '^(Absent|[a-fA-F0-9]{64})$' -or $file.after -notmatch '^(Absent|[a-fA-F0-9]{64})$'){throw 'Invalid recovery inventory'}
        $seen[[string]$file.path]=$true;$target=SafePath $Root $file.path
        if(-not(AllowedRestoreTarget $file.path) -and $file.path -ne 'NeuRotic\Installer\Current-Install.json' -and $file.path -notmatch '^NeuRotic-(test-)?backups\\[^\\]+\\INSTALL-MANIFEST.json$'){throw 'Invalid recovery target'}
        $hash=if(Test-Path -LiteralPath $target -PathType Leaf){HashFile $target}else{'Absent'};$live[[string]$file.path]=$hash
        if($hash -ne $file.before -and $hash -ne $file.after -and $hash -ne 'Absent'){throw ('Recovery preserves an outside edit: '+$file.path)}
        if($file.before -ne 'Absent' -and (HashFile (SafePath (SafePath $Root $journal.recovery) $file.snapshot)) -ne $file.before){throw 'Recovery preimage is missing or changed'}
    }
    foreach($file in @($journal.files)){
        $target=SafePath $Root $file.path;$hash=$live[[string]$file.path]
        if($hash -eq $file.before){continue}
        if($file.before -eq 'Absent'){if($hash -ne 'Absent'){Remove-NeuroticPath -LiteralPath $target -ExpectedHash $hash}}
        else{CopyVerified (SafePath (SafePath $Root $journal.recovery) $file.snapshot) $target $file.before $hash}
    }
    if($Kind -eq 'Install' -and $journal.record){
        $recordPath=Assert-PathUnderRoot $Root $journal.record
        if($recordPath.Substring($Root.Length+1) -notmatch '^NeuRotic-backups\\install-[a-f0-9]+\\INSTALL-MANIFEST.json$'){throw 'Invalid interrupted install record'}
        $record=Get-Content -LiteralPath $recordPath -Raw|ConvertFrom-Json;$record.status='failed-rolled-back';SaveRecord $recordPath $record
    }
    Complete-NeuroticMutation $Root $journal
    $script:RestoreRecordLedger=@{}
    Write-Output 'The interrupted transaction was restored to its verified pre-operation state.'
}
function Assert-RestoreTarget([string]$Relative,[string]$RecordPath,[hashtable]$Names) {
    if (-not (AllowedRestoreTarget $Relative)) {
        throw "Unrecognized restore target '$Relative' in '$RecordPath'. Keep this backup and use the installer that created it, or an updated NeuRotic App that recognizes this file."
    }
    if ($Names.ContainsKey($Relative)) {
        throw "Duplicate restore target '$Relative' in '$RecordPath'. Keep this backup for recovery; conflicting restore entries need review before Repair or Uninstall."
    }
    $Names[$Relative] = $true
}

function Assert-PathUnderRoot([string]$Root,[string]$Path) {
    $base = [IO.Path]::GetFullPath($Root).TrimEnd('\')
    $full = [IO.Path]::GetFullPath($Path).TrimEnd('\')
    if (-not $full.StartsWith($base + '\',[StringComparison]::OrdinalIgnoreCase)) {
        throw "Path leaves the selected game folder: $full"
    }
    $walk = $full
    while ($walk -and $walk.StartsWith($base,[StringComparison]::OrdinalIgnoreCase)) {
        if (Test-Path -LiteralPath $walk) {
            if ((Get-Item -Force -LiteralPath $walk).Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Linked installation records are not supported: $walk"
            }
        }
        if ($walk -eq $base) { break }
        $walk = Split-Path -Parent $walk
    }
    return $full
}

function Initialize-RestoreRecordLedger([string]$GameRoot,$State) {
    $script:RestoreRecordLedger=@{}
    $script:RestoreMetadataMissing=$false
    $ledger=@();if($State.PSObject.Properties.Name -contains 'restore_record_ledger'){$ledger=@($State.restore_record_ledger)}
    if($ledger.Count -gt 4096 -or @($State.restore_chain).Count -gt 4096){throw 'Installation ledger exceeds its bounded recovery limit.'}
    foreach($entry in $ledger){
        $path=Assert-PathUnderRoot $GameRoot ([string]$entry.path)
        if($path.Substring($GameRoot.Length+1) -notmatch '^NeuRotic-(test-)?backups\\[^\\]+\\INSTALL-MANIFEST\.json$'){throw 'Invalid installation ledger path.'}
        if($script:RestoreRecordLedger.ContainsKey($path)){throw 'Duplicate installation ledger record.'}
        $bytes=[Text.Encoding]::UTF8.GetBytes([string]$entry.record_json)
        if($bytes.Length -gt 4194304 -or (HashBytes $bytes) -ne $entry.sha256){throw 'Invalid installation ledger metadata.'}
        $script:RestoreRecordLedger[$path]=$bytes
    }
    $missing=@();$paths=@{};$seen=@{}
    foreach($entry in @($State.restore_chain)){
        $path=Assert-PathUnderRoot $GameRoot ([string]$entry)
        if($path.Substring($GameRoot.Length+1) -notmatch '^NeuRotic-(test-)?backups\\[^\\]+\\INSTALL-MANIFEST\.json$' -or $seen.ContainsKey($path)){throw 'Invalid or duplicate installation history path.'}
        $seen[$path]=$true
        if(-not(Test-Path -LiteralPath $path -PathType Leaf) -and -not $script:RestoreRecordLedger.ContainsKey($path)){$missing+=$path;continue}
        $record=Read-InstallRecord $path
        if(($record.PSObject.Properties.Name -contains 'recovery_metadata_missing') -and $record.recovery_metadata_missing){$script:RestoreMetadataMissing=$true}
        # Recovery inspection also sees partially restored records while the
        # durable uninstall journal still owns completion or rollback.
        if($record.status -notin @('installed-verified','restored') -or (Split-Path $record.game_executable) -ine $GameRoot -or $record.backup -ine (Split-Path $path)){throw 'Recovery metadata belongs to another installation.'}
        $names=@{};foreach($file in $record.files){Assert-RestoreTarget $file.path $path $names;[void](SafePath $GameRoot $file.path);$paths[[string]$file.path]=$true}
    }
    if(-not $missing.Count){return}
    $script:RestoreMetadataMissing=$true
    if($State.selected_proxy -notin $allowedProxies){throw 'Missing history has no recognized selected proxy.'}
    $paths[[string]$State.selected_proxy]=$true
    $config='OptiScaler.ini';if(($State.PSObject.Properties.Name -contains 'install_route') -and $State.install_route -eq 'prepared-x86'){$config='ReShade.ini'}
    $paths[$config]=$true
    if($paths.Count -gt 4096){throw 'Missing history recovery exceeds its bounded file limit.'}
    foreach($path in $missing){
        # Zero hashes are deliberately unknown ownership, never live hashes.
        # Historical originals cannot be inferred from current game contents.
        $files=@($paths.Keys|Sort-Object|ForEach-Object{[pscustomobject]@{path=$_;existed=$false;previous_hash=$null;installed_exists=$true;installed_hash=('0'*64);original_unknown=$true}})
        $record=[ordered]@{kind='neurotic-customer-candidate-install';status='installed-verified';game_executable=[string]$State.game_executable;backup=(Split-Path $path);selected_proxy=[string]$State.selected_proxy;original_ini=@{name=$config};install_route=$(if($config -eq 'ReShade.ini'){'prepared-x86'}else{'native-x64'});files=$files;recovery_metadata_missing=$true;reconciled_paths=@($paths.Keys);owned_files=@();started_utc=$null;completed_utc=$null;restored_utc=$null}
        $script:RestoreRecordLedger[$path]=[Text.Encoding]::UTF8.GetBytes(($record|ConvertTo-Json -Depth 12))
    }
    Write-Warning ('Recovered maintenance with '+$missing.Count+' missing installation record(s). Original files described only by those records are unknown; recovery cannot recreate them.')
}
function Assert-NeuroticHistoryCapacity([string[]]$Chain){
    if($Chain.Count -ge 512){throw 'Installation history is at capacity. Compact verified history before another update; Restore and Uninstall remain available. No game files were changed.'}
}
function Get-InstallRecordBytes([string]$RecordPath) {
    if(Test-Path -LiteralPath $RecordPath -PathType Leaf){
        $bytes=[IO.File]::ReadAllBytes($RecordPath)
        if((Get-Variable -Name RestoreRecordLedger -Scope Script -ErrorAction SilentlyContinue) -and $script:RestoreRecordLedger.ContainsKey($RecordPath) -and
            (HashBytes $bytes) -ne (HashBytes $script:RestoreRecordLedger[$RecordPath])){throw "Installation record differs from its committed ledger; preserved for recovery: $RecordPath"}
        return ,$bytes
    }
    if((Get-Variable -Name RestoreRecordLedger -Scope Script -ErrorAction SilentlyContinue) -and $script:RestoreRecordLedger.ContainsKey($RecordPath)){
        return ,$script:RestoreRecordLedger[$RecordPath]
    }
    throw "Installation record was not found and has no recovery ledger: $RecordPath"
}
function Read-InstallRecord([string]$RecordPath) {
    $record = [Text.Encoding]::UTF8.GetString((Get-InstallRecordBytes $RecordPath)).TrimStart([char]0xFEFF) | ConvertFrom-Json
    if ($record.kind -ne 'neurotic-customer-candidate-install' -or $record.files.Count -lt 2) {
        throw "This is not a recognized NeuRotic installation record: $RecordPath"
    }
    return $record
}

# Never recursively remove a product directory: it can contain user-added files.
function Remove-OwnedFile([string]$Root,[string]$Relative,[string]$ExpectedHash) {
    $path = SafePath $Root $Relative
    if (Test-Path -LiteralPath $path -PathType Leaf) {
        if ((HashFile $path) -ne $ExpectedHash) { Write-Host "Preserved changed file: $path"; return }
        Remove-NeuroticPath -LiteralPath $path -Force -ExpectedHash $ExpectedHash
    }
    $parent = Split-Path -Parent $path
    while ($parent -and $parent -ine $Root) {
        [void](Assert-PathUnderRoot $Root $parent)
        if (-not (Test-Path -LiteralPath $parent -PathType Container)) { $parent = Split-Path -Parent $parent; continue }
        if (@(Get-ChildItem -LiteralPath $parent -Force).Count) { break }
        Remove-NeuroticPath -LiteralPath $parent -Force
        $parent = Split-Path -Parent $parent
    }
}

function Test-RestoreChain([string]$GameRoot,[string[]]$Chain,[switch]$ReportChanges,[switch]$ReportLiveFiles,[switch]$MetadataOnly,[hashtable]$LiveHashes=$null) {
    if ($LiveHashes -and -not $MetadataOnly) { throw 'Observed live hashes are only valid for the current metadata inspection.' }
    $hashes = @{}; $seen = @{}; $reconciled = @{}
    $changes = New-Object System.Collections.Generic.List[object]
    $observed = New-Object System.Collections.Generic.List[object]
    $records = New-Object Collections.Generic.List[object]
    $total = 0; $done = 0
    $stage = $(if ($MetadataOnly) {'Checking restore records'} else {'Verifying required restore files'})
    Show-SetupProgress 'Reading restore records' 0 $Chain.Count
    foreach ($path in $Chain) {
        [void](Assert-PathUnderRoot $GameRoot $path)
        if ($path.Substring($GameRoot.Length+1) -notmatch '^NeuRotic-(test-)?backups\\[^\\]+\\INSTALL-MANIFEST\.json$') {
            throw 'Restore records must be in a NeuRotic installation backup folder.'
        }
        if ($seen.ContainsKey($path)) { throw 'Duplicate restore record.' }; $seen[$path] = $true
        $r = Read-InstallRecord $path
        if ($r.status -ne 'installed-verified' -or (Split-Path -Parent $r.game_executable) -ine $GameRoot -or
            $r.backup -ine (Split-Path -Parent $path)) { throw 'Restore record identity or status is invalid.' }
        $records.Add($r); $total += $r.files.Count
        Show-SetupProgress 'Reading restore records' $records.Count $Chain.Count
    }
    Show-SetupProgress $stage 0 $total
    foreach ($r in $records) {
        $names = @{}
        foreach ($f in $r.files) {
            Assert-RestoreTarget $f.path (Join-Path $r.backup 'INSTALL-MANIFEST.json') $names
            $target = SafePath $GameRoot $f.path
            $live = -not $hashes.ContainsKey($f.path)
            if ($live) {
                $exists = Test-Path -LiteralPath $target -PathType Leaf
                if ((Test-Path -LiteralPath $target) -and -not $exists) { throw "Directory occupies restore target: $target" }
                # Hub already hashed this file for this inspection's revision.
                # Reuse that same observation, never a prior request's cache.
                # Restore execution never accepts these hashes for backup bytes.
                if (-not $exists) {
                    $hashes[$f.path] = $null
                } elseif ($LiveHashes -and $LiveHashes.ContainsKey($f.path)) {
                    if ($LiveHashes[$f.path] -notmatch '^[a-fA-F0-9]{64}$') { throw 'Invalid live inventory hash.' }
                    $hashes[$f.path] = $LiveHashes[$f.path]
                } else {
                    $hashes[$f.path] = HashFile $target
                }
            }
            $expected = -not ($f.PSObject.Properties.Name -contains 'installed_exists') -or [bool]$f.installed_exists
            $mismatch = ($expected -and $null -ne $hashes[$f.path] -and $hashes[$f.path] -ne $f.installed_hash) -or
                (-not $expected -and $null -ne $hashes[$f.path])
            if ($live) { $observed.Add([pscustomobject]@{path=$f.path;existed=($null -ne $hashes[$f.path]);sha256=$hashes[$f.path];changed=(-not (Test-SettingsTarget $f.path) -and $mismatch);owned=($expected -and $null -ne $hashes[$f.path] -and $hashes[$f.path] -eq $f.installed_hash -and -not (($f.PSObject.Properties.Name -contains 'ownership_unproven') -and $f.ownership_unproven))}) }
            if (-not (Test-SettingsTarget $f.path) -and $mismatch) {
                if ($live) {
                    $changes.Add([pscustomobject]@{path=$f.path;existed=($null -ne $hashes[$f.path]);sha256=$hashes[$f.path]})
                } elseif (-not $reconciled[$f.path] -and -not (($r.PSObject.Properties.Name -contains 'recovery_metadata_missing') -and $r.recovery_metadata_missing)) {
                    throw "Restore history does not connect at: $($f.path)"
                }
            }
            if ($f.existed -and -not (($f.PSObject.Properties.Name -contains 'unchanged_owned') -and $f.unchanged_owned)) {
                $previous = SafePath (Join-Path $r.backup 'previous') $f.path
                if(-not $MetadataOnly -and ($f.PSObject.Properties.Name -contains 'temporary_modified_preimage') -and $f.temporary_modified_preimage -and -not(Test-Path -LiteralPath $previous -PathType Leaf)){
                    throw ('Replace discarded this modified-file preimage after successful installation: '+$f.path+'. Individual transaction restore is unavailable for it. Uninstall still uses the original recorded baseline where available.')
                }
                if (-not $MetadataOnly -and (HashFile $previous) -ne $f.previous_hash) { throw "Backup verification failed: $previous" }
            }
            $restoreExists = $f.existed -and -not (($f.PSObject.Properties.Name -contains 'restore_exists') -and -not $f.restore_exists)
            $hashes[$f.path] = $(if ($restoreExists) { $f.previous_hash } else { $null })
            $reconciled[$f.path] = ($r.PSObject.Properties.Name -contains 'reconciled_paths') -and ($r.reconciled_paths -contains $f.path)
            $done++; Show-SetupProgress $stage $done $total $f.path
        }
    }
    if ($ReportChanges) { return $changes.ToArray() }
    if ($ReportLiveFiles) { return $observed.ToArray() }
}

function New-UninstallTransaction([string]$GameRoot,[string[]]$Chain,[string[]]$AdditionalPaths=@()) {
    $journalPath = SafePath $GameRoot 'NeuRotic\Installer\Uninstall-Transaction.json'
    if (Test-Path -LiteralPath $journalPath) { throw 'An unfinished uninstall requires recovery first.' }
    $recoveryRelative = 'NeuRotic-uninstall-recovery\transaction-' + [Guid]::NewGuid().ToString('N')
    $recoveryRoot = SafePath $GameRoot $recoveryRelative
    $settingsName='OptiScaler.ini'
    if($Chain.Count){$settingsName=Get-RecordConfigName (Read-InstallRecord $Chain[0])}
    if($settingsName -notin @('OptiScaler.ini','ReShade.ini')){throw 'Unexpected uninstall configuration identity'}
    $paths = @($settingsName,('NeuRotic\UserData\'+$settingsName),'NeuRotic\Installer\Current-Install.json','Uninstall NeuRotic.cmd') + $AdditionalPaths
    if($settingsName -eq 'ReShade.ini'){$paths+='NeuRotic\UserData\PreparedGpuHost.ini'}
    foreach ($recordPath in $Chain) {
        $paths += $recordPath.Substring($GameRoot.Length+1)
        $paths += @((Read-InstallRecord $recordPath).files | ForEach-Object { [string]$_.path })
    }
    $snapshotIndex = 0
    $entries = @($paths | Select-Object -Unique | ForEach-Object {
        $target = SafePath $GameRoot $_
        $exists = Test-Path -LiteralPath $target -PathType Leaf
        if ((Test-Path -LiteralPath $target) -and -not $exists) { throw "Directory occupies transaction target: $target" }
        $hash = $(if ($exists) { HashFile $target } else { $null })
        $snapshot = 'f\' + ('{0:x4}.bin' -f $snapshotIndex)
        $snapshotIndex++
        if ($exists) { CopyVerified $target (SafePath $recoveryRoot $snapshot) $hash }
        [pscustomobject]@{path=$_;snapshot=$snapshot;existed=$exists;sha256=$hash}
    })
    $journal = [pscustomobject]@{kind='neurotic-uninstall-transaction';game_directory=$GameRoot;recovery=$recoveryRelative;files=$entries}
    SaveRecord $journalPath $journal
    return $journal
}

# The chain is newest first. Each older row replaces the proposed final state
# for that target, so uninstall consumes only its original effective baseline.
function Get-UninstallBaseline([string]$GameRoot,$Records,[switch]$Verify,[switch]$AllowUnavailable) {
    $baseline=@{}
    foreach($record in $Records){
        $names=@{}
        foreach($file in $record.files){
            Assert-RestoreTarget $file.path (Join-Path $record.backup 'INSTALL-MANIFEST.json') $names
            [void](SafePath $GameRoot $file.path)
            if(($file.PSObject.Properties.Name -contains 'unchanged_owned') -and $file.unchanged_owned){continue}
            if($file.existed -isnot [bool] -or (($file.PSObject.Properties.Name -contains 'restore_exists') -and $file.restore_exists -isnot [bool])){throw 'Invalid original-file existence metadata.'}
            $exists=$file.existed -and -not (($file.PSObject.Properties.Name -contains 'restore_exists') -and -not $file.restore_exists)
            $source=$null;$hash=$null
            if($exists){$source=SafePath (Join-Path $record.backup 'previous') $file.path;$hash=[string]$file.previous_hash;if($hash -notmatch '^[a-fA-F0-9]{64}$'){throw 'Invalid original backup hash.'}}
            $unknown=($file.PSObject.Properties.Name -contains 'original_unknown') -and $file.original_unknown
            $baseline[[string]$file.path]=[pscustomobject]@{path=[string]$file.path;existed=[bool]$exists;sha256=$hash;source=$source;available=(-not $unknown);original_unknown=[bool]$unknown;reason=$(if($unknown){'The original file state is unknown because installation metadata was lost.'}else{$null})}
        }
    }
    $result=@($baseline.Values|Sort-Object path)
    if($Verify){foreach($file in $result){if($file.existed){
        $valid=(Test-Path -LiteralPath $file.source -PathType Leaf) -and (HashFile $file.source) -eq $file.sha256
        if(-not $valid){
            if(-not $AllowUnavailable){throw "Backup verification failed: $($file.source)"}
            $file.available=$false;$file.reason='The original backup is missing or does not match its recorded hash.'
        }
    }}}
    return $result
}
function Invoke-UninstallBoundary([string]$Stage){
    if($env:NEUROTIC_HUB_FIXTURE_ROOT -and $env:NEUROTIC_UNINSTALL_TEST_STOP -ceq $Stage){throw ('Uninstall fixture interruption: '+$Stage)}
    if($env:NEUROTIC_HUB_FIXTURE_ROOT -and $env:NEUROTIC_UNINSTALL_TEST_STOP -ceq ('crash-'+$Stage)){
        $failure=New-Object InvalidOperationException('Uninstall fixture crash: '+$Stage);$failure.Data['NeuRoticFixtureCrash']=$true;throw $failure
    }
}
function Invoke-RestoreBaseline([string]$GameRoot,$Baseline,$Journal){
    $before=@{};foreach($entry in $Journal.files){$before[[string]$entry.path]=$entry}
    $done=0
    Show-SetupProgress 'Restoring original files once' 0 @($Baseline).Count
    foreach($file in $Baseline){
        $original=$before[$file.path];if(-not $original){throw 'Original-file recovery snapshot is missing.'}
        $target=SafePath $GameRoot $file.path;$expected=$(if($original.existed){$original.sha256}else{'Absent'})
        if($file.existed){
            if(-not $original.existed -or $original.sha256 -ne $file.sha256){CopyVerified $file.source $target $file.sha256 $expected}
        }elseif($original.existed){Remove-NeuroticPath -LiteralPath $target -Force -ExpectedHash $expected}
        $exists=Test-Path -LiteralPath $target -PathType Leaf
        if($exists -ne $file.existed -or ($exists -and (HashFile $target) -ne $file.sha256)){throw "Original-file restore did not verify: $target"}
        $done++;Show-SetupProgress 'Restoring original files once' $done @($Baseline).Count $file.path
        Invoke-UninstallBoundary 'during-baseline'
    }
}

function Restore-UninstallTransaction([string]$GameRoot,$Journal) {
    if ($Journal.kind -ne 'neurotic-uninstall-transaction' -or $Journal.game_directory -ine $GameRoot -or
        $Journal.recovery -notmatch '^NeuRotic-uninstall-recovery\\transaction-[a-f0-9]{32}$') { throw 'Invalid uninstall recovery journal.' }
    $recoveryRoot = SafePath $GameRoot $Journal.recovery
    $observedRecovery=@{}
    foreach ($f in $Journal.files) {
        if (-not (AllowedRestoreTarget $f.path) -and $f.path -notin @('NeuRotic\UserData\OptiScaler.ini','NeuRotic\UserData\ReShade.ini','NeuRotic\UserData\PreparedGpuHost.ini','NeuRotic\Installer\Current-Install.json','Uninstall NeuRotic.cmd') -and
            $f.path -notmatch '^NeuRotic-(test-)?backups\\[^\\]+\\INSTALL-MANIFEST.json$') { throw 'Invalid recovery target.' }
        [void](SafePath $GameRoot $f.path)
        $snapshot = Get-UninstallSnapshotRelative $f
        if ($f.existed -and (HashFile (SafePath $recoveryRoot $snapshot)) -ne $f.sha256) { throw 'Uninstall recovery snapshot failed verification.' }
        $live=SafePath $GameRoot $f.path
        $observedRecovery[$f.path]=$(if(Test-Path -LiteralPath $live -PathType Leaf){HashFile $live}else{'Absent'})
        if(($Journal.PSObject.Properties.Name -contains 'guarded_writes') -and $Journal.guarded_writes){
            $before=$(if($f.existed){$f.sha256}else{'Absent'});$after=$before
            if($f.PSObject.Properties.Name -contains 'after_exists'){$after=$(if($f.after_exists){$f.after_sha256}else{'Absent'})}
            if($observedRecovery[$f.path] -ne $before -and $observedRecovery[$f.path] -ne $after){throw "Recovery target changed outside this uninstall; preserved: $($f.path)"}
        }
    }
    foreach ($f in $Journal.files) {
        $target = SafePath $GameRoot $f.path
        if ($f.existed) {
            if($observedRecovery[$f.path] -eq $f.sha256){continue}
            CopyVerified (SafePath $recoveryRoot (Get-UninstallSnapshotRelative $f)) $target $f.sha256 $observedRecovery[$f.path]
        } elseif ($observedRecovery[$f.path] -ne 'Absent') { Remove-NeuroticPath -LiteralPath $target -Force -ExpectedHash $observedRecovery[$f.path] }
    }
    Remove-NeuroticPath -LiteralPath (SafePath $GameRoot 'NeuRotic\Installer\Uninstall-Transaction.json') -Force
    Write-Host 'The pre-uninstall files and settings were recovered. You can retry uninstall.'
}

function Remove-UninstallSnapshots([string]$GameRoot,$Journal) {
    foreach ($f in $Journal.files) {
        if ($f.existed) { Remove-OwnedFile $GameRoot ($Journal.recovery + '\' + (Get-UninstallSnapshotRelative $f)) $f.sha256 }
    }
}

function Get-OwnedHistoryForCleanup([string]$GameRoot,[string[]]$ActiveChain){
    $paths=New-Object Collections.Generic.List[string];$seen=@{}
    foreach($path in $ActiveChain){$paths.Add($path);$seen[$path]=$true}
    foreach($relativeRoot in @('NeuRotic-backups','NeuRotic-test-backups')){
        $root=SafePath $GameRoot $relativeRoot
        if(-not(Test-Path -LiteralPath $root -PathType Container)){continue}
        foreach($folder in Get-ChildItem -LiteralPath $root -Directory){
            try{
                $path=SafePath $folder.FullName 'INSTALL-MANIFEST.json'
                if($seen.ContainsKey($path) -or -not(Test-Path -LiteralPath $path -PathType Leaf)){continue}
                $record=Read-InstallRecord $path
                if($record.status -notin @('installed-verified','restored') -or $record.backup -ine $folder.FullName -or (Split-Path $record.game_executable) -ine $GameRoot){continue}
                $names=@{}
                foreach($file in $record.files){Assert-RestoreTarget $file.path $path $names;[void](SafePath (Join-Path $record.backup 'previous') $file.path);if($file.existed -and $file.previous_hash -notmatch '^[a-fA-F0-9]{64}$'){throw 'Invalid owned backup hash.'}}
                if($record.PSObject.Properties.Name -contains 'owned_files'){foreach($file in $record.owned_files){[void](SafePath $record.backup $file.path);if($file.sha256 -notmatch '^[a-fA-F0-9]{64}$'){throw 'Invalid owned file hash.'}}}
                $paths.Add($path);$seen[$path]=$true
            }catch{Write-Host ('Preserved unrecognized or changed history folder: '+$folder.FullName)}
        }
    }
    return $paths.ToArray()
}

function New-UninstallCleanup([string]$GameRoot,[string[]]$Chain,$State,$Journal,[string]$Mode) {
    $entries = @()
    if ($Mode -eq 'Full' -or $RemoveBackupRecords) {
    foreach ($recordPath in @(Get-OwnedHistoryForCleanup $GameRoot $Chain)) {
        $r = Read-InstallRecord $recordPath
        $folder = Assert-PathUnderRoot $GameRoot (Split-Path -Parent $recordPath)
        $prefix = $folder.Substring($GameRoot.Length+1) + '\'
        foreach ($f in $r.files) {
            if ($f.existed) { $entries += [pscustomobject]@{path=($prefix + 'previous\' + $f.path);sha256=$f.previous_hash} }
        }
        if ($r.PSObject.Properties.Name -contains 'owned_files') {
            foreach ($f in $r.owned_files) { $entries += [pscustomobject]@{path=($prefix + $f.path);sha256=$f.sha256} }
        }
        $entries += [pscustomobject]@{path=($prefix + 'INSTALL-MANIFEST.json');sha256=(HashFile $recordPath)}
    }
    }
    foreach ($f in $Journal.files) {
        if ($f.existed) { $entries += [pscustomobject]@{path=($Journal.recovery + '\' + (Get-UninstallSnapshotRelative $f));sha256=$f.sha256} }
    }
    if (($Mode -eq 'Full' -or $RemoveBackupRecords) -and $State) {
        if ($State.PSObject.Properties.Name -contains 'manager_files') { $entries += @($State.manager_files) }
        $entries += [pscustomobject]@{path='NeuRotic\Installer\Current-Install.json';sha256=(HashFile (SafePath $GameRoot 'NeuRotic\Installer\Current-Install.json'))}
    }
    $plan = [pscustomobject]@{kind='neurotic-uninstall-cleanup';game_directory=$GameRoot;files=$entries}
    SaveRecord (SafePath $GameRoot 'NeuRotic\Installer\Cleanup-Pending.json') $plan
    return $plan
}

function Complete-UninstallCleanup([string]$GameRoot,$Plan) {
    if ($Plan.kind -ne 'neurotic-uninstall-cleanup' -or $Plan.game_directory -ine $GameRoot) { throw 'Invalid cleanup receipt.' }
    foreach ($f in $Plan.files) {
        if ($f.path -notmatch '^NeuRotic-(test-)?backups\\[^\\]+\\' -and
            $f.path -notmatch '^NeuRotic-uninstall-recovery\\transaction-[a-f0-9]{32}\\' -and
            $f.path -notin @('NeuRotic\Installer\Current-Install.json','NeuRotic\Installer\NeuRotic-Setup-Engine.ps1')) { throw 'Unexpected cleanup target.' }
        [void](SafePath $GameRoot $f.path)
        if ($f.sha256 -notmatch '^[a-fA-F0-9]{64}$') { throw 'Invalid cleanup hash.' }
    }
    foreach ($f in $Plan.files) { Remove-OwnedFile $GameRoot $f.path $f.sha256 }
    $receipt = SafePath $GameRoot 'NeuRotic\Installer\Cleanup-Pending.json'
    Remove-OwnedFile $GameRoot 'NeuRotic\Installer\Cleanup-Pending.json' (HashFile $receipt)
}

function Invoke-RestoreRecord([string]$RecordPath,[switch]$AllowRestored,[switch]$AllowChangedFiles,$ExpectedFiles=@()) {
    $record=Read-InstallRecord $RecordPath;$root=Split-Path $record.game_executable
    CheckGameClosed $record.game_executable
    $pending=Get-NeuroticPendingTransaction $root
    if($pending -and $pending -ine $RecordPath){throw ('Finish pending recovery before an individual restore: '+$pending)}
    if($record.status -eq 'restored' -and $AllowRestored){return $record}
    if($record.status -notin @('installed-verified','backed-up','failed-rollback-incomplete')){throw 'This installation record is not active'}
    $statePath=SafePath $root 'NeuRotic\Installer\Current-Install.json';$state=$null
    if(Test-Path -LiteralPath $statePath -PathType Leaf){
        $state=Get-Content -LiteralPath $statePath -Raw|ConvertFrom-Json
        Initialize-RestoreRecordLedger $root $state
        if($record.status -eq 'installed-verified' -and @($state.restore_chain).Count -and $state.restore_chain[-1] -ine $RecordPath){throw 'Restore the newest active transaction first to preserve a coherent installation.'}
    }
    $writes=@();$sources=@{};$before=@{};Assert-LiveFiles $root $ExpectedFiles
    foreach($file in $record.files){
        if(($file.PSObject.Properties.Name -contains 'unchanged_owned') -and $file.unchanged_owned){continue}
        if(-not (AllowedRestoreTarget $file.path)){throw 'Unrecognized restore target'}
        $target=SafePath $root $file.path;$hash=if(Test-Path -LiteralPath $target -PathType Leaf){HashFile $target}else{'Absent'}
        if($hash -ne 'Absent' -and $hash -ne $file.installed_hash -and $hash -ne $file.previous_hash -and -not $AllowChangedFiles -and -not(Test-SettingsTarget $file.path)){throw ('Outside modification preserved: '+$file.path)}
        $restoreExists=$file.existed -and -not(($file.PSObject.Properties.Name -contains 'restore_exists') -and -not $file.restore_exists)
        $after=if($restoreExists){[string]$file.previous_hash}else{'Absent'}
        if($restoreExists){$source=SafePath (Join-Path $record.backup 'previous') $file.path;if((HashFile $source) -ne $after){throw 'Restore preimage failed verification'};$sources[[string]$file.path]=$source}
        $writes += [pscustomobject]@{path=$file.path;after=$after};$before[[string]$file.path]=$hash
    }
    $record.status='restored';$record|Add-Member -NotePropertyName restored_utc -NotePropertyValue ([DateTime]::UtcNow.ToString('o')) -Force
    $recordBytes=[Text.Encoding]::UTF8.GetBytes(($record|ConvertTo-Json -Depth 20));$relative=$RecordPath.Substring($root.Length+1)
    $writes += [pscustomobject]@{path=$relative;after=(HashBytes $recordBytes)};$before[$relative]=HashFile $RecordPath
    $metadata=@{};$metadata[$relative]=$recordBytes
    if($state){
        $state.restore_chain=@($state.restore_chain|Where-Object{$_ -ine $RecordPath})
        $state.restore_record_ledger=@($state.restore_record_ledger|Where-Object{$_.path -ine $RecordPath})
        if($state.PSObject.Properties.Name -contains 'install_history'){$state.install_history=@($state.install_history|Where-Object{$_.record -ine $RecordPath})}
        if($state.restore_chain.Count){
            $previous=Read-InstallRecord $state.restore_chain[-1]
            $state.selected_proxy=$previous.selected_proxy
            foreach($name in @('commit','install_route','target_bit_size','proxy_source')){if($previous.PSObject.Properties.Name -contains $name){$state|Add-Member -NotePropertyName $name -NotePropertyValue $previous.$name -Force}}
            $state.last_install_utc=$previous.completed_utc
        }else{$state.status='uninstalled';$state.uninstalled_utc=[DateTime]::UtcNow.ToString('o')}
        $bytes=[Text.Encoding]::UTF8.GetBytes(($state|ConvertTo-Json -Depth 20));$relative='NeuRotic\Installer\Current-Install.json'
        $writes += [pscustomobject]@{path=$relative;after=(HashBytes $bytes)};$before[$relative]=HashFile $statePath;$metadata[$relative]=$bytes
    }
    if($CheckOnly){return}
    $journal=New-NeuroticMutationJournal $root 'Restore' $writes $RecordPath
    Invoke-NeuroticInstallBoundary $root 'restore-after-journal'
    foreach($write in $writes){
        $target=SafePath $root $write.path;$expected=$before[[string]$write.path]
        if($metadata.ContainsKey([string]$write.path)){if(-not(WriteVerifiedBytes $metadata[$write.path] $target $write.after $expected)){throw 'Restore metadata changed; recovery required'}}
        elseif($write.after -eq 'Absent'){if($expected -ne 'Absent'){Remove-NeuroticPath -LiteralPath $target -ExpectedHash $expected}}
        else{CopyVerified $sources[$write.path] $target $write.after $expected}
        Invoke-NeuroticInstallBoundary $root ('restore-after-payload:'+ $write.path)
    }
    Invoke-NeuroticInstallBoundary $root 'restore-after-state'
    Complete-NeuroticMutation $root $journal
    $script:RestoreRecordLedger=@{}
    Write-Host 'The selected transaction was restored and the active installation history was rewound.'
    return $record
}
function Select-UninstallMode {
    Write-Host ''
    Write-Host 'What should happen to your NeuRotic settings?'
    Write-Host '1. Keep my current settings for reinstalling (recommended)'
    Write-Host '2. Remove NeuRotic settings and restore the exact pre-install state'
    Write-Host '3. Full cleanup: remove settings plus installer history and backups'
    Write-Host '0. Cancel'
    $choice = (Read-Host 'Choose 0, 1, 2, or 3').Trim()
    return $(switch ($choice) { '1' {'KeepSettings'} '2' {'RemoveSettings'} '3' {'Full'} default {$null} })
}

function Test-RecordAgainstHashes($Record,$Hashes) {
    foreach ($file in $Record.files) {
        if ($file.path -eq 'OptiScaler.ini') { continue }
        $expectsInstalled = -not ($file.PSObject.Properties.Name -contains 'installed_exists') -or [bool]$file.installed_exists
        if ($expectsInstalled -and (-not $Hashes.ContainsKey([string]$file.path) -or $Hashes[[string]$file.path] -ne [string]$file.installed_hash)) { return $false }
        if (-not $expectsInstalled -and $Hashes.ContainsKey([string]$file.path)) { return $false }
    }
    return $true
}

function Get-LegacyRestoreChain([string]$GameRoot,[string]$SelectedManifest) {
    $paths = @()
    if ($SelectedManifest) {
        $paths = @((Assert-PathUnderRoot $GameRoot (Resolve-Path -LiteralPath $SelectedManifest).Path))
    } else {
        foreach ($relativeRoot in @('NeuRotic-test-backups','NeuRotic-backups')) {
            $root = SafePath $GameRoot $relativeRoot
            if (Test-Path -LiteralPath $root -PathType Container) {
                foreach ($folder in Get-ChildItem -LiteralPath $root -Directory) {
                    [void](Assert-PathUnderRoot $GameRoot $folder.FullName)
                    $candidate = SafePath $folder.FullName 'INSTALL-MANIFEST.json'
                    if (Test-Path -LiteralPath $candidate -PathType Leaf) { $paths += $candidate }
                }
            }
        }
    }
    $records = @($paths | ForEach-Object {
        try {
            $r = Read-InstallRecord $_
            if ($r.status -eq 'installed-verified' -and (Split-Path -Parent $r.game_executable) -eq $GameRoot) {
                [pscustomobject]@{Path=$_;Record=$r}
            }
        } catch {}
    })
    if (-not $records.Count) { return @() }
    $hashes = @{}
    foreach ($name in $allowedProxies + @('ReShade64.dll','nvngx.dll_dlssnr.dll','OptiScaler.ini','NeuRotic-LICENSE.txt','NeuRotic-PreparedGuides.addon64','WinPixEventRuntime.dll')) {
        $candidate = SafePath $GameRoot $name
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { $hashes[$name] = HashFile $candidate }
    }
    foreach ($dir in @('OptiScaler','Licenses')) {
        $candidateRoot = SafePath $GameRoot $dir
        if (Test-Path -LiteralPath $candidateRoot -PathType Container) {
            foreach ($file in Get-ChildItem -LiteralPath $candidateRoot -File -Recurse) {
                $relative = $file.FullName.Substring($GameRoot.Length + 1)
                [void](SafePath $GameRoot $relative)
                $hashes[$relative] = HashFile $file.FullName
            }
        }
    }
    $chain = New-Object System.Collections.Generic.List[string]
    $linkedPrevious = @{}
    foreach ($entry in $records) {
        if ($entry.Record.PSObject.Properties.Name -contains 'previous_record' -and $entry.Record.previous_record) {
            $linkedPrevious[[string]$entry.Record.previous_record] = $true
        }
    }
    $heads = @($records | Where-Object { -not $linkedPrevious.ContainsKey($_.Path) })
    $nextRecord = $null
    if ($SelectedManifest) { $nextRecord = $records[0] }
    elseif ($heads.Count -eq 1) { $nextRecord = $heads[0] }
    while ($true) {
        $matches = @(if ($nextRecord) { $nextRecord } else { $records | Where-Object { Test-RecordAgainstHashes $_.Record $hashes } })
        $nextRecord = $null
        if (-not $matches.Count) { break }
        if ($matches.Count -gt 1) { throw 'More than one legacy installation record matches the current files. Select the exact game or use the installed uninstaller.' }
        $match = $matches[0]
        $chain.Add($match.Path)
        foreach ($file in $match.Record.files) {
            $restoreExists = $file.existed -and -not (($file.PSObject.Properties.Name -contains 'restore_exists') -and -not $file.restore_exists)
            if ($restoreExists) { $hashes[[string]$file.path] = [string]$file.previous_hash }
            else { [void]$hashes.Remove([string]$file.path) }
        }
        $records = @($records | Where-Object { $_.Path -ne $match.Path })
        if ($match.Record.PSObject.Properties.Name -contains 'previous_record' -and $match.Record.previous_record) {
            $previousPath = Assert-PathUnderRoot $GameRoot ([string]$match.Record.previous_record)
            if ($chain.Contains($previousPath)) { throw 'Cyclic legacy restore history.' }
            $previous = Read-InstallRecord $previousPath
            if ($previous.status -ne 'installed-verified') { throw 'The previous legacy installation record is not active.' }
            $nextRecord = [pscustomobject]@{Path=$previousPath;Record=$previous}
        }
    }
    return @($chain)
}

function Get-GameForUninstall {
    if ($GameDirectory) {
        $root = [IO.Path]::GetFullPath($GameDirectory).TrimEnd('\')
        if (-not (Test-Path -LiteralPath $root -PathType Container)) { throw "Game folder was not found: $root" }
        [void](SafePath $root 'NeuRotic\Installer')
        return [pscustomobject]@{Root=$root;Executable=$null}
    }
    if (-not $GameExecutable) {
        Add-Type -AssemblyName System.Windows.Forms
        $picker = New-Object System.Windows.Forms.OpenFileDialog
        $picker.Title = 'NeuRotic Manual Uninstall - select your game executable'
        $picker.Filter = 'Game executable (*.exe)|*.exe'
        try {
            if ($picker.ShowDialog() -ne [Windows.Forms.DialogResult]::OK) { return $null }
            $script:GameExecutable = $picker.FileName
        } finally { $picker.Dispose() }
    }
    $exe = (Resolve-Path -LiteralPath $GameExecutable).Path
    if ([IO.Path]::GetExtension($exe) -ine '.exe') { throw 'Select the game executable.' }
    return [pscustomobject]@{Root=(Split-Path -Parent $exe);Executable=$exe}
}

function Invoke-ManualUninstall([string]$GameRoot,[string]$Mode) {
    $packageManifestPath = Join-Path $PSScriptRoot 'PACKAGE-MANIFEST.json'
    $knownHash = $null
    $payload = @()
    if (Test-Path -LiteralPath $packageManifestPath -PathType Leaf) {
        $packageManifest = Get-Content -Raw -Encoding UTF8 -LiteralPath $packageManifestPath | ConvertFrom-Json
        $payload = @($packageManifest.files | Where-Object { $_.path.StartsWith('payload\',[StringComparison]::OrdinalIgnoreCase) })
        $runtime = @($payload | Where-Object { $_.path -eq 'payload\OptiScaler.dll' })
        if ($runtime.Count -eq 1) { $knownHash = [string]$runtime[0].sha256 }
    }
    $detected = @($allowedProxies | Where-Object {
        $path = Join-Path $GameRoot $_
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { return $false }
        if ($knownHash -and (HashFile $path) -eq $knownHash) { return $true }
        return (Get-Item -LiteralPath $path).VersionInfo.OriginalFilename -ieq 'OptiScaler.dll'
    })
    $proxy = $ManualProxyName
    if (-not $proxy -and $detected.Count -eq 1) { $proxy = $detected[0] }
    if (-not $proxy) {
        Write-Host ''
        if ($detected.Count) { Write-Host ('Possible NeuRotic proxy files: ' + ($detected -join ', ')) }
        Write-Host 'No trustworthy installation record was found. Choose the filename you installed NeuRotic as.'
        $proxy = SelectProxyName
    }
    if (-not $proxy) { Write-Output 'Uninstall cancelled. No files were changed.'; return }
    $proxyPath = SafePath $GameRoot $proxy
    if (-not (Test-Path -LiteralPath $proxyPath -PathType Leaf)) { throw "The selected proxy does not exist: $proxyPath" }
    $managed = New-Object System.Collections.Generic.List[string]
    $managed.Add($proxy)
    foreach ($file in $payload) {
        $relative = [string]$file.path.Substring(8)
        if ($relative -eq 'OptiScaler.dll' -or $relative -eq 'OptiScaler.ini') { continue }
        if (-not (AllowedTarget $relative)) { continue }
        $target = SafePath $GameRoot $relative
        if ((Test-Path -LiteralPath $target -PathType Leaf) -and (HashFile $target) -eq [string]$file.sha256) { $managed.Add($relative) }
    }
    Write-Output ''
    Write-Output 'NeuRotic uninstall - manual recovery mode'
    Write-Output "Game folder: $GameRoot"
    Write-Output "Selected proxy: $proxy"
    Write-Output ('Verified files to remove: ' + (($managed | Select-Object -Unique) -join ', '))
    Write-Output 'No original proxy can be restored because no installation record was found.'
    if ($CheckOnly) { Write-Output 'PASS: manual uninstall preview; no files changed.'; return }
    if (-not $ConfirmUninstall -and (Read-Host 'Type UNINSTALL to continue') -cne 'UNINSTALL') { Write-Output 'Uninstall cancelled. No files were changed.'; return }
    $recovery = SafePath $GameRoot ('NeuRotic-uninstall-recovery\manual-' + [Guid]::NewGuid().ToString('N').Substring(0,12))
    Ensure-NeuroticDirectory $recovery
    foreach ($relative in @($managed | Select-Object -Unique)) {
        $target = SafePath $GameRoot $relative
        CopyVerified $target (SafePath $recovery $relative) (HashFile $target)
    }
    $journal = New-UninstallTransaction $GameRoot @() @($managed)
    try {
    foreach ($relative in @($managed | Select-Object -Unique)) {
        $entry=@($journal.files|Where-Object{$_.path -eq $relative})[0]
        Remove-NeuroticPath -LiteralPath (SafePath $GameRoot $relative) -Force -ExpectedHash $entry.sha256
    }
    if ($Mode -ne 'KeepSettings') {
        $iniPath = SafePath $GameRoot 'OptiScaler.ini'
        if (Test-Path -LiteralPath $iniPath -PathType Leaf) { CopyVerified $iniPath (Join-Path $recovery 'OptiScaler.ini') (HashFile $iniPath); Remove-NeuroticPath -LiteralPath $iniPath -Force }
    }
    Remove-NeuroticPath -LiteralPath (SafePath $GameRoot 'NeuRotic\Installer\Uninstall-Transaction.json') -Force
    } catch { Restore-UninstallTransaction $GameRoot $journal; throw }
    Remove-UninstallSnapshots $GameRoot $journal
    if ($RemovePrivateModel -and ($ConfirmUninstall -or (Read-Host 'Type DELETE MODEL to remove the user-supplied NVIDIA model') -ceq 'DELETE MODEL')) {
        $model = SafePath $GameRoot 'nvngx_dlssnr.dll'
        if (Test-Path -LiteralPath $model -PathType Leaf) { Remove-NeuroticPath -LiteralPath $model -Force }
    }
    $script:UninstallCompleted = $true
    if($HubOperationResultPath){SaveRecord $HubOperationResultPath ([ordered]@{status='Succeeded';operation='Uninstall';unavailableOriginals=@();preservedForeignFiles=@();limitations=@()})}
    Write-Output "PASS: manually selected NeuRotic files removed. Recovery: $recovery"
}

function Invoke-PublicUninstall {
    $lockStart=Get-NeuroticLockCount
    try {
    $game = Get-GameForUninstall
    if (-not $game) { Write-Output 'Uninstall cancelled. No files were changed.'; return }
    $gameRoot = $game.Root
    [void](Enter-NeuroticTargetLock $gameRoot)
    $pending=Get-NeuroticPendingTransaction $gameRoot
    if($pending -in @('Install-Transaction.json','Restore-Transaction.json')){
        if($CheckOnly){throw 'Recovery is required before uninstall'}
        if(-not $ConfirmUninstall -and (Read-Host 'Type RECOVER to restore the interrupted transaction') -cne 'RECOVER'){return}
        Restore-NeuroticMutation $gameRoot ($pending.Replace('-Transaction.json',''));return
    }
    if($pending -and [IO.Path]::IsPathRooted($pending)){throw ('Restore the interrupted installation before uninstalling: '+$pending)}
    if(Test-Path -LiteralPath (SafePath $gameRoot 'NeuRotic/Installer/History-Transaction.json')){throw 'Finish interrupted history cleanup with Restore in the NeuRotic App before uninstalling.'}
    $statePath = SafePath $gameRoot 'NeuRotic\Installer\Current-Install.json'
    $journalPath = SafePath $gameRoot 'NeuRotic\Installer\Uninstall-Transaction.json'
    if (Test-Path -LiteralPath $journalPath -PathType Leaf) {
        Write-Host 'A previous uninstall was interrupted. Its verified snapshot can recover the original files.'
        if ($CheckOnly) { throw 'Recovery is required before uninstall can proceed.' }
        if (-not $ConfirmUninstall -and (Read-Host 'Type RECOVER to restore the pre-uninstall state') -cne 'RECOVER') { return }
        $journal = Get-Content -Raw -LiteralPath $journalPath | ConvertFrom-Json
        foreach ($exe in Get-ChildItem -LiteralPath $gameRoot -Filter '*.exe' -File) { CheckGameClosed $exe.FullName }
        Restore-UninstallTransaction $gameRoot $journal
        $staleCleanup = SafePath $gameRoot 'NeuRotic\Installer\Cleanup-Pending.json'
        if (Test-Path -LiteralPath $staleCleanup -PathType Leaf) { Remove-NeuroticPath -LiteralPath $staleCleanup -Force }
        Remove-UninstallSnapshots $gameRoot $journal
        return
    }
    $cleanupPath = SafePath $gameRoot 'NeuRotic\Installer\Cleanup-Pending.json'
    if (Test-Path -LiteralPath $cleanupPath -PathType Leaf) {
        Write-Host 'The game files are already restored. Backup cleanup remains to be completed.'
        if ($CheckOnly) { Write-Output 'Cleanup is pending; preview made no changes.'; return }
        if (-not $ConfirmUninstall -and (Read-Host 'Type CLEANUP to finish removing recorded backup files') -cne 'CLEANUP') { return }
        $cleanupLimitations=@()
        if(Test-Path -LiteralPath $statePath -PathType Leaf){$cleanupState=Get-Content -Raw -LiteralPath $statePath|ConvertFrom-Json;if($cleanupState.PSObject.Properties.Name -contains 'uninstall_limitations'){$cleanupLimitations=@($cleanupState.uninstall_limitations)}}
        Complete-UninstallCleanup $gameRoot (Get-Content -Raw -LiteralPath $cleanupPath | ConvertFrom-Json)
        $script:UninstallCompleted = $true
        Write-Output 'PASS: remaining uninstall cleanup completed.'
        if($HubOperationResultPath){SaveRecord $HubOperationResultPath ([ordered]@{status='Succeeded';operation='Uninstall';limitations=$cleanupLimitations;unavailableOriginals=@();preservedForeignFiles=@()})}
        return
    }
    $state = $null
    $stateObservedHash = $null
    $chain = @()
    if (Test-Path -LiteralPath $statePath -PathType Leaf) {
        $stateBytes=[IO.File]::ReadAllBytes($statePath);$stateObservedHash=HashBytes $stateBytes
        $state = [Text.Encoding]::UTF8.GetString($stateBytes).TrimStart([char]0xFEFF) | ConvertFrom-Json
        if ($state.kind -ne 'neurotic-public-install' -or $state.schema_version -ne 2 -or $state.status -ne 'installed-verified') {
            throw 'The managed NeuRotic installation record is invalid or not active.'
        }
        if ([IO.Path]::GetFullPath([string]$state.game_directory).TrimEnd('\') -ne $gameRoot) { throw 'The managed installation record belongs to another game folder.' }
        Initialize-RestoreRecordLedger $gameRoot $state
        if ($state.game_executable) { $game.Executable = [string]$state.game_executable }
        foreach ($item in @($state.restore_chain)) {
            $candidate = Assert-PathUnderRoot $gameRoot ([string]$item)
            $chain += $candidate
        }
        [array]::Reverse($chain)
    } else {
        $chain = @(Get-LegacyRestoreChain $gameRoot $LegacyManifest)
    }
    if ($game.Executable) { CheckGameClosed $game.Executable }
    else { foreach ($exe in Get-ChildItem -LiteralPath $gameRoot -Filter '*.exe' -File) { CheckGameClosed $exe.FullName } }
    $mode = $UninstallMode
    if (-not $mode) { $mode = Select-UninstallMode }
    if (-not $mode) { Write-Output 'Uninstall cancelled. No files were changed.'; return }
    if (-not $chain.Count) { Invoke-ManualUninstall $gameRoot $mode; return }
    $recordSnapshots=@{}
    $validatedRecords = @($chain | ForEach-Object {
        $recordPath = Assert-PathUnderRoot $gameRoot $_
        $recordBytes=Get-InstallRecordBytes $recordPath;$record=[Text.Encoding]::UTF8.GetString($recordBytes).TrimStart([char]0xFEFF)|ConvertFrom-Json
        if($record.kind -ne 'neurotic-customer-candidate-install' -or $record.files.Count -lt 2){throw "Unrecognized installation record: $recordPath"}
        $recordSnapshots[$recordPath]=[pscustomobject]@{bytes=$recordBytes;sha256=(HashBytes $recordBytes)}
        if($record.PSObject.Properties.Name -contains 'created_directories'){foreach($directory in @($record.created_directories)){[void](SafePath $gameRoot $directory)}}
        $recordGameRoot = [IO.Path]::GetFullPath((Split-Path -Parent ([string]$record.game_executable))).TrimEnd('\')
        $recordBackup = [IO.Path]::GetFullPath([string]$record.backup).TrimEnd('\')
        $expectedBackup = [IO.Path]::GetFullPath((Split-Path -Parent $recordPath)).TrimEnd('\')
        if (-not $recordGameRoot.Equals($gameRoot,[StringComparison]::OrdinalIgnoreCase) -or
            -not $recordBackup.Equals($expectedBackup,[StringComparison]::OrdinalIgnoreCase)) {
            throw "An installation record does not belong to the selected game folder: $recordPath"
        }
        $record
    })
    $newest = $validatedRecords[0]
    $uninstallConfigName=Get-RecordConfigName $newest
    $savedConfigRelative='NeuRotic\UserData\'+$uninstallConfigName
    $observedFiles = @(Test-RestoreChain $gameRoot $chain -ReportLiveFiles -MetadataOnly)
    foreach($recordPath in $chain){if((HashBytes (Get-InstallRecordBytes $recordPath)) -ne $recordSnapshots[$recordPath].sha256){throw 'Installation history changed during uninstall inspection.'}}
    $baseline=@(Get-UninstallBaseline $gameRoot $validatedRecords -Verify -AllowUnavailable)
    # A deposited private model remains user data when it had no original
    # pre-install counterpart. Its separately selected cleanup owns removal.
    if(-not $RemovePrivateModel){$baseline=@($baseline|Where-Object{$_.existed -or [IO.Path]::GetFileName($_.path) -ine 'nvngx_dlssnr.dll'})}
    $changedFiles = @($observedFiles | Where-Object { $_.changed })
    $unavailableOriginals=@($baseline|Where-Object{-not $_.available}|ForEach-Object{[pscustomobject]@{path=$_.path;reason=$_.reason}})
    $preservedForeignFiles=@($changedFiles|ForEach-Object{[string]$_.path})
    foreach($entry in $baseline){
        if($entry.original_unknown -and (Test-SettingsTarget $entry.path)){
            $observed=@($observedFiles|Where-Object{$_.path -eq $entry.path})
            if($observed.Count -and $observed[0].existed -and -not $observed[0].owned){$preservedForeignFiles+=$entry.path}
        }
    }
    $uninstallLimitations=@($unavailableOriginals)+@($changedFiles|ForEach-Object{[pscustomobject]@{path=$_.path;reason='A file changed outside NeuRotic and was preserved in place.'}})
    if($script:RestoreMetadataMissing){$uninstallLimitations+=[pscustomobject]@{path='installation history';reason='Installation metadata was lost. Files described only by missing records may remain; unrecorded files were preserved.'}}
    $baseline=@($baseline|Where-Object{$_.path -notin $preservedForeignFiles}|ForEach-Object{
        if($_.available){$_}else{[pscustomobject]@{path=$_.path;existed=$false;sha256=$null;source=$null}}
    })
    $relocatedProxies = @(Get-RelocatedProxies $gameRoot $chain)
    [void](SafePath $gameRoot $savedConfigRelative)
    if($RemovePrivateModel){[void](SafePath $gameRoot 'nvngx_dlssnr.dll')}
    if ($mode -eq 'Full' -or $RemoveBackupRecords) {
        foreach ($cleanupRecord in $validatedRecords) {
            if ($cleanupRecord.PSObject.Properties.Name -contains 'owned_files') {
                foreach ($f in $cleanupRecord.owned_files) { [void](SafePath $cleanupRecord.backup $f.path) }
            }
        }
        if ($state -and $state.PSObject.Properties.Name -contains 'manager_files') {
            foreach ($f in $state.manager_files) {
                if ($f.path -ne 'NeuRotic\Installer\NeuRotic-Setup-Engine.ps1') { throw 'Unexpected installer-owned file.' }
                [void](SafePath $gameRoot $f.path)
            }
        }
    }
    Write-Output ''
    Write-Output 'NeuRotic uninstall plan'
    Write-Output "Game: $gameRoot"
    Write-Output "Installed as: $($newest.selected_proxy)"
    if ($state) {
        Write-Output 'Installed application: NeuRotic'
        Write-Output "Last installed: $($state.last_install_utc)"
        Write-Output "Installation history entries: $($chain.Count)"
    } else { Write-Output "Legacy installation records: $($chain.Count)" }
    Write-Output "Settings choice: $mode"
    if ($changedFiles.Count) { Write-Output ('Changed files: preserve in place: ' + ($changedFiles.path -join ', ')) }
    foreach($limitation in $unavailableOriginals){Write-Warning ($limitation.path+': '+$limitation.reason)}
    if ($relocatedProxies.Count) { Write-Output ('Matching relocated loaders: preserve a separate copy, then remove: ' + ($relocatedProxies.path -join ', ')) }
    Write-Output 'Private NVIDIA NR model: keep (user-owned)'
    if ($RemovePrivateModel) { Write-Output 'Private NVIDIA NR model: REMOVE (separately requested)' }
    if ($HubPlanPreviewPath) {
        if (-not $CheckOnly) { throw 'Structured preview requires CheckOnly' }
        $limitationPaths=@($uninstallLimitations|ForEach-Object{$_.path})
        $previewChanges = @($observedFiles | ForEach-Object { [ordered]@{path=$_.path;operation=$(if($_.path -in $preservedForeignFiles){'preserve externally changed file in place'}elseif($_.path -in $limitationPaths){'remove known NeuRotic file; original restoration unavailable'}else{'restore original recorded state'});previous_hash=$_.sha256} })
        $previewChanges += @($relocatedProxies | ForEach-Object { [ordered]@{path=$_.path;operation='preserve then remove relocated loader'} })
        $previewChanges += [ordered]@{path=$uninstallConfigName;operation=$(if($mode -eq 'KeepSettings'){'retain settings in NeuRotic/UserData'}else{'remove managed settings; restore pre-install INI'})}
        $previewChanges += [ordered]@{path='NeuRotic/Installer';operation=$(if($mode -eq 'Full' -or $RemoveBackupRecords){'remove verified owned manager files and all recognized same-game backup records, including older archives; preserve unknown and changed files'}else{'retain installation history; mark uninstalled'})}
        SaveRecord $HubPlanPreviewPath ([ordered]@{changes=$previewChanges;mode=$mode;preserved=@('private neural model','unknown files','externally changed files');unavailableOriginals=$unavailableOriginals;preservedForeignFiles=$preservedForeignFiles})
    }
    if ($CheckOnly) { Write-Output 'PASS: uninstall preview; no files changed.'; return }
    if (-not $ConfirmUninstall -and (Read-Host 'Type UNINSTALL to continue') -cne 'UNINSTALL') { Write-Output 'Uninstall cancelled. No files were changed.'; return }
    Show-SetupProgress 'Backing up files'
    $journal = New-UninstallTransaction $gameRoot $chain @($relocatedProxies | ForEach-Object { [string]$_.path })
    $activeWritesStarted = $false
    try {
    # Declare every permitted post-write value before changing any live file.
    # Recovery refuses external edits instead of overwriting them with snapshots.
    $expectedRestore=@{};foreach($entry in $journal.files){$expectedRestore[[string]$entry.path]=$entry}
    foreach($observed in $observedFiles){
        $entry=$expectedRestore[[string]$observed.path]
        if(-not(Test-SettingsTarget $observed.path) -and $entry.existed -and $entry.sha256 -ne $observed.sha256 -and $observed.path -notin $preservedForeignFiles){
            $preservedForeignFiles+=$observed.path
            $uninstallLimitations+=[pscustomobject]@{path=$observed.path;reason='A file changed during uninstall preparation and was preserved in place.'}
        }
    }
    $baseline=@($baseline|Where-Object{$_.path -notin $preservedForeignFiles})
    foreach($recordPath in $chain){$relative=$recordPath.Substring($gameRoot.Length+1);if($expectedRestore[$relative].existed -and $expectedRestore[$relative].sha256 -ne $recordSnapshots[$recordPath].sha256){throw 'Installation record changed before uninstall backup.'}}
    if($state -and $expectedRestore['NeuRotic\Installer\Current-Install.json'].sha256 -ne $stateObservedHash){throw 'Installation state changed before uninstall backup.'}
    $intended=@{};foreach($file in $baseline){$intended[$file.path]=$file}
    $restoredUtc=[DateTime]::UtcNow.ToString('o');$metadataWrites=@{}
    foreach($recordPath in $chain){
        $record=[Text.Encoding]::UTF8.GetString($recordSnapshots[$recordPath].bytes).TrimStart([char]0xFEFF)|ConvertFrom-Json
        $record.status='restored';$record|Add-Member -NotePropertyName restored_utc -NotePropertyValue $restoredUtc -Force
        $bytes=[Text.Encoding]::UTF8.GetBytes(($record|ConvertTo-Json -Depth 20));$relative=$recordPath.Substring($gameRoot.Length+1)
        $metadataWrites[$relative]=$bytes;$intended[$relative]=[pscustomobject]@{existed=$true;sha256=(HashBytes $bytes)}
    }
    foreach($file in $relocatedProxies){if(-not $file.recorded){$intended[$file.path]=[pscustomobject]@{existed=$false;sha256=$null}}}
    $settingsEntry=$expectedRestore[$uninstallConfigName];$savedRelative=$savedConfigRelative
    $intended[$savedRelative]=[pscustomobject]@{existed=($mode -eq 'KeepSettings' -and $settingsEntry.existed);sha256=$(if($mode -eq 'KeepSettings' -and $settingsEntry.existed){$settingsEntry.sha256}else{$null})}
    if($uninstallConfigName -eq 'ReShade.ini'){
        $helperEntry=$expectedRestore['NeuRotic\Prepared\NeuRotic.GpuHost\OptiScaler.ini']
        $keepHelper=$mode -eq 'KeepSettings' -and $helperEntry -and $helperEntry.existed
        $intended['NeuRotic\UserData\PreparedGpuHost.ini']=[pscustomobject]@{existed=[bool]$keepHelper;sha256=$(if($keepHelper){$helperEntry.sha256}else{$null})}
    }

    if($state){
        $state.status='uninstalled';$state.uninstalled_utc=$restoredUtc;$state.uninstall_mode=$mode
        $state|Add-Member -NotePropertyName restore_record_ledger -NotePropertyValue @($chain|ForEach-Object{
            $relative=$_.Substring($gameRoot.Length+1);$recordBytes=$metadataWrites[$relative]
            [pscustomobject]@{path=$_;sha256=(HashBytes $recordBytes);record_json=[Text.Encoding]::UTF8.GetString($recordBytes)}
        }) -Force
        $state|Add-Member -NotePropertyName uninstall_limitations -NotePropertyValue @($uninstallLimitations) -Force
        $originalIni=@($baseline|Where-Object{$_.path -eq $uninstallConfigName})
        $state|Add-Member -NotePropertyName restored_ini_sha256 -NotePropertyValue $(if($originalIni.Count -and $originalIni[0].existed){$originalIni[0].sha256}else{$null}) -Force
        $bytes=[Text.Encoding]::UTF8.GetBytes(($state|ConvertTo-Json -Depth 20));$metadataWrites['NeuRotic\Installer\Current-Install.json']=$bytes
        $intended['NeuRotic\Installer\Current-Install.json']=[pscustomobject]@{existed=$true;sha256=(HashBytes $bytes)}
    }
    foreach($entry in $journal.files){if($intended.ContainsKey($entry.path)){$entry|Add-Member -NotePropertyName after_exists -NotePropertyValue ([bool]$intended[$entry.path].existed) -Force;$entry|Add-Member -NotePropertyName after_sha256 -NotePropertyValue $intended[$entry.path].sha256 -Force}}
    $journal|Add-Member -NotePropertyName guarded_writes -NotePropertyValue $true -Force;SaveRecord $journalPath $journal
    # Preserve the verified transaction bytes, including edits arriving after preview.
    # Full cleanup cannot discard their only copy when a user edits during backup.
    $preserve = @($relocatedProxies)
    foreach ($observed in $observedFiles) {
        if (Test-SettingsTarget $observed.path) { continue }
        $entry = @($journal.files | Where-Object { $_.path -eq $observed.path })[0]
        if ($entry.existed -and ($observed.changed -or $entry.sha256 -ne $observed.sha256)) { $preserve += $entry }
    }
    Save-ChangedFiles $gameRoot $preserve $journal
    # Confirm the saved preimages still describe every live target before writes.
    foreach ($entry in $journal.files) {
        $path = SafePath $gameRoot $entry.path
        $exists = Test-Path -LiteralPath $path -PathType Leaf
        if ($exists -ne $entry.existed -or ($exists -and (HashFile $path) -ne $entry.sha256)) { throw "File changed during uninstall backup: $path" }
    }
    if ($game.Executable) { CheckGameClosed $game.Executable }
    else { foreach ($exe in Get-ChildItem -LiteralPath $gameRoot -Filter '*.exe' -File) { CheckGameClosed $exe.FullName } }
    $savedSettingsPath = SafePath $gameRoot $savedConfigRelative
    $settingsEntry = @($journal.files | Where-Object { $_.path -eq $uninstallConfigName })[0]
    $activeWritesStarted = $true
    if ($mode -eq 'KeepSettings' -and $settingsEntry.existed) {
        # The verified transaction preimage includes edits made after preview.
        # Preview-time bytes must never replace the user's latest settings.
        $settingsSnapshot = SafePath (SafePath $gameRoot $journal.recovery) (Get-UninstallSnapshotRelative $settingsEntry)
        $currentIniBytes = [IO.File]::ReadAllBytes($settingsSnapshot)
        if ((HashBytes $currentIniBytes) -ne $settingsEntry.sha256) { throw 'Settings snapshot failed verification.' }
        $savedBefore=@($journal.files|Where-Object{$_.path -eq $savedConfigRelative})[0]
        $written=WriteVerifiedBytes $currentIniBytes $savedSettingsPath $settingsEntry.sha256 $(if($savedBefore.existed){$savedBefore.sha256}else{'Absent'})
        if(-not $written){throw 'Saved settings changed at write boundary; outside bytes preserved'}
        Write-Output "Current NeuRotic settings saved for reinstalling: $savedSettingsPath"
    } elseif (Test-Path -LiteralPath $savedSettingsPath -PathType Leaf) {
        $savedBefore=$expectedRestore[$savedConfigRelative]
        if(-not $savedBefore.existed){throw 'Saved settings appeared during uninstall; preserved.'}
        Remove-NeuroticPath -LiteralPath $savedSettingsPath -Force -ExpectedHash $savedBefore.sha256
    }
    if($uninstallConfigName -eq 'ReShade.ini'){
        $helperRelative='NeuRotic\Prepared\NeuRotic.GpuHost\OptiScaler.ini'
        $savedHelperRelative='NeuRotic\UserData\PreparedGpuHost.ini'
        $helperEntry=@($journal.files|Where-Object{$_.path -eq $helperRelative})
        $savedHelperPath=SafePath $gameRoot $savedHelperRelative
        if($mode -eq 'KeepSettings' -and $helperEntry.Count -eq 1 -and $helperEntry[0].existed){
            $snapshot=SafePath (SafePath $gameRoot $journal.recovery) (Get-UninstallSnapshotRelative $helperEntry[0])
            $before=@($journal.files|Where-Object{$_.path -eq $savedHelperRelative})[0]
            CopyVerified $snapshot $savedHelperPath $helperEntry[0].sha256 $(if($before.existed){$before.sha256}else{'Absent'})
        }elseif(Test-Path -LiteralPath $savedHelperPath -PathType Leaf){
            $before=$expectedRestore[$savedHelperRelative]
            if(-not $before.existed){throw 'Saved helper settings appeared during uninstall; preserved.'}
            Remove-NeuroticPath -LiteralPath $savedHelperPath -Force -ExpectedHash $before.sha256
        }
    }
    Invoke-RestoreBaseline $gameRoot $baseline $journal
    foreach($directory in @($validatedRecords | ForEach-Object {if($_.PSObject.Properties.Name -contains 'created_directories'){$_.created_directories}} | Sort-Object -Unique | Sort-Object Length -Descending)){
        $createdPath=SafePath $gameRoot $directory
        if((Test-Path -LiteralPath $createdPath -PathType Container) -and @(Get-ChildItem -LiteralPath $createdPath -Force).Count -eq 0){Remove-NeuroticPath -LiteralPath $createdPath -Force}
    }
    foreach ($file in $relocatedProxies) {
        if ($file.recorded) { continue } # The restore chain already returned this name to its original state.
        $path = SafePath $gameRoot $file.path
        if ((HashFile $path) -ne $file.sha256) { throw "Relocated loader changed during uninstall: $path" }
        Remove-NeuroticPath -LiteralPath $path -Force -ExpectedHash $file.sha256
    }
    $installedLauncher = SafePath $gameRoot 'Uninstall NeuRotic.cmd'

    foreach($relative in $metadataWrites.Keys){
        $before=$expectedRestore[$relative];$bytes=$metadataWrites[$relative]
        if(-not (WriteVerifiedBytes $bytes (SafePath $gameRoot $relative) (HashBytes $bytes) $(if($before.existed){$before.sha256}else{'Absent'}))){throw "Installation metadata changed during uninstall; preserved: $relative"}
        Invoke-UninstallBoundary 'during-metadata'
    }
    $script:RestoreRecordLedger=@{}
    # Keep the original evidence whenever restoration was incomplete, including
    # Full cleanup; a future original backup recovery must remain possible.
    $cleanupMode=$mode
    if($uninstallLimitations.Count){$cleanupMode='KeepSettings';$RemoveBackupRecords=$false}
    $cleanup = New-UninstallCleanup $gameRoot $chain $state $journal $cleanupMode
    Remove-NeuroticPath -LiteralPath $journalPath -Force
    } catch {
        $failure = $_
        if(Get-NeuroticFailureProperty $_.Exception 'NeuRoticFixtureCrash'){
            $_.Exception.Data['NeuRoticMutationStatus']='RecoveryRequired';throw
        }
        if(Get-NeuroticFailureProperty $_.Exception 'NeuRoticWriteStarted'){$activeWritesStarted=$true}
        if (-not $activeWritesStarted) {
            # No active target has been written; never roll back over a newer user edit.
            Remove-NeuroticPath -LiteralPath $journalPath -Force
            Remove-UninstallSnapshots $gameRoot $journal
            throw $failure
        }
        try {
            Restore-UninstallTransaction $gameRoot $journal
            if (Test-Path -LiteralPath $cleanupPath -PathType Leaf) { Remove-NeuroticPath -LiteralPath $cleanupPath -Force }
            Remove-UninstallSnapshots $gameRoot $journal
        }
        catch { throw "Uninstall stopped; recovery is pending. Rerun the downloaded uninstaller after closing the game. $($_.Exception.Message)" }
        throw $failure
    }
    if ($RemovePrivateModel) {
        $deleteModel = $true
        if (-not $ConfirmUninstall) {
            if ((Read-Host 'The private NVIDIA model was not installed by NeuRotic. Type DELETE MODEL to remove it') -cne 'DELETE MODEL') {
                $deleteModel = $false
                Write-Output 'Private-model removal was not confirmed. The model was preserved.'
            }
        }
        $model = SafePath $gameRoot 'nvngx_dlssnr.dll'
        if ($deleteModel -and (Test-Path -LiteralPath $model -PathType Leaf)) { Remove-NeuroticPath -LiteralPath $model -Force }
    }
    Show-SetupProgress 'Cleaning up installer files'
    Complete-UninstallCleanup $gameRoot $cleanup
    Complete-SetupProgress
    $script:UninstallCompleted = $true
    if($uninstallLimitations.Count){Write-Warning ('NeuRotic owned files were removed where safe. Original restoration has '+$uninstallLimitations.Count+' limitation(s); affected files and recovery records were retained.');foreach($entry in $uninstallLimitations){Write-Output ($entry.path+': '+$entry.reason)}}
    else{Write-Output 'PASS: NeuRotic was uninstalled and the verified pre-install files were restored.'}
    if($HubOperationResultPath){SaveRecord $HubOperationResultPath ([ordered]@{status='Succeeded';operation='Uninstall';unavailableOriginals=$unavailableOriginals;preservedForeignFiles=$preservedForeignFiles;limitations=$uninstallLimitations})}
    }finally{Release-NeuroticTargetLocks $lockStart}
}

$lockScopeStart=Get-NeuroticLockCount
try {
if ($HubNonInteractive) {
    # Strict typed callers must never inherit a newly introduced console prompt.
    function Read-Host { throw 'NeedsDecision: interactive input is unavailable in Hub mode.' }
}
if ($HubRequestPath) {
    if (-not $HubResultPath) { throw 'Hub result path is required.' }
    . (Join-Path $PSScriptRoot 'NeuRotic-HubProtocol.ps1')
    . (Join-Path $PSScriptRoot 'NeuRotic-HubSettings.ps1')
    Invoke-HubProtocol $HubRequestPath $HubResultPath
    return
}
if ($Restore) {
    $restoreRecordPath = Join-Path $PSScriptRoot 'INSTALL-MANIFEST.json'
    $restoreRecord = Read-InstallRecord $restoreRecordPath
    $restoreRoot = Split-Path -Parent $restoreRecord.game_executable
    [void](Enter-NeuroticTargetLock $restoreRoot)
    $pending=Get-NeuroticPendingTransaction $restoreRoot
    if($pending -in @('Install-Transaction.json','Restore-Transaction.json')){Restore-NeuroticMutation $restoreRoot ($pending.Replace('-Transaction.json',''));return}
    if($pending -and $pending -ine $restoreRecordPath){throw ('Finish pending recovery first: '+$pending)}
    if($restoreRecord.status -in @('backed-up','failed-rollback-incomplete')){[void](Invoke-RestoreRecord $restoreRecordPath);return}
    if(Test-Path -LiteralPath (SafePath $restoreRoot 'NeuRotic/Installer/History-Transaction.json')){throw 'Finish interrupted history cleanup with Restore in the NeuRotic App before restoring an individual backup.'}
    $observed = @(Test-RestoreChain $restoreRoot @($restoreRecordPath) -ReportLiveFiles)
    $changes = @($observed | Where-Object { $_.changed })
    if (-not $CheckOnly) { Save-ChangedFiles $restoreRoot $changes }
    [void](Invoke-RestoreRecord $restoreRecordPath -AllowChangedFiles -ExpectedFiles $observed)
    return
}
if ($Uninstall) {
    Invoke-PublicUninstall
    if ($Installed -and $script:UninstallCompleted) { exit 10 }
    return
}

$packageRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot)).TrimEnd('\')
if ($HubPackageRoot) { $packageRoot = [IO.Path]::GetFullPath($HubPackageRoot).TrimEnd('\') }
$packageSupport = Join-Path $packageRoot 'support'
$manifestPath = Join-Path $packageSupport 'PACKAGE-MANIFEST.json'
$manifest = Get-Content -Raw -Encoding UTF8 -LiteralPath $manifestPath | ConvertFrom-Json
if ($manifest.kind -ne 'neurotic-customer-candidate' -or $manifest.commit -notmatch '^[a-f0-9]{40}$') { throw 'Unexpected package identity.' }
$seen = @{}
Show-SetupProgress 'Verifying package' 0 $manifest.files.Count
foreach ($file in $manifest.files) {
    if ($seen.ContainsKey($file.path)) { throw 'Duplicate package path.' }; $seen[$file.path] = $true
    $source = SafePath $packageRoot $file.path
    if ($file.sha256 -notmatch '^[A-Fa-f0-9]{64}$' -or
        ((-not $CheckOnly -or -not $file.path.StartsWith('payload\',[StringComparison]::OrdinalIgnoreCase)) -and (HashFile $source) -ne $file.sha256)) {
        throw "Package verification failed: $($file.path)"
    }
    Show-SetupProgress 'Verifying package' $seen.Count $manifest.files.Count $file.path
}
Complete-SetupProgress
$build = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $packageSupport 'BUILD-MANIFEST.json') | ConvertFrom-Json
$preparedX86=($build.PSObject.Properties.Name -contains 'install_route') -and $build.install_route -eq 'prepared-x86'
$targetBits=64;$proxySource='OptiScaler.dll';$configName='OptiScaler.ini'
$requiredPayload=@('payload\OptiScaler.dll','payload\nvngx.dll_dlssnr.dll','payload\OptiScaler.ini')
if($preparedX86){
    if($build.target_bit_size -ne 32 -or $build.bit_size -ne 64){throw 'Prepared x86 build identity must distinguish 32-bit target from 64-bit helper source'}
    $targetBits=32;$proxySource='ReShade32.dll';$configName='ReShade.ini'
    $requiredPayload=@('payload\ReShade32.dll','payload\ReShade.ini','payload\NeuRotic\Prepared\NeuRotic-PreparedGuides.addon32','payload\NeuRotic\Prepared\NeuRotic.GpuHost\NeuRotic.PreparedGpuHost.exe','payload\NeuRotic\Prepared\NeuRotic.GpuHost\OptiScaler.dll','payload\NeuRotic\Prepared\NeuRotic.GpuHost\OptiScaler.ini','payload\NeuRotic\Prepared\NeuRotic.GpuHost\WinPixEventRuntime.dll','payload\NeuRotic\Prepared\NeuRotic.GpuHost\nvngx.dll_dlssnr.dll','payload\NeuRotic\Prepared\Start-PreparedGame.ps1')
}
foreach ($required in @($requiredPayload)+@('support\NeuRotic-Setup-Engine.ps1','support\NeuRotic-Prerequisites.ps1','support\BUILD-MANIFEST.json')) {
    if (-not $seen.ContainsKey($required)) { throw "Missing manifest entry: $required" }
}
# Launchers may live at the release root. They are not installed payloads;
# required engine, payload and build entries above remain mandatory and verified.
$build = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $packageSupport 'BUILD-MANIFEST.json') | ConvertFrom-Json
if ($build.commit -ne $manifest.commit -or $build.status -ne 'built' -or $build.build.exit_code -ne 0) { throw 'Build identity mismatch.' }
if((Get-SetupPEBits (SafePath $packageRoot ('payload\'+$proxySource))) -ne $targetBits){throw 'Package loader architecture does not match installation route'}
if($preparedX86){
    foreach($file in $manifest.files){
        if(-not $file.path.StartsWith('payload\',[StringComparison]::OrdinalIgnoreCase)){continue}
        $relative=$file.path.Substring(8)
        if($relative -notin @('ReShade32.dll','ReShade.ini','NeuRotic-LICENSE.txt') -and $relative -notmatch '^(NeuRotic\\Prepared|Licenses)\\[^:]+$'){throw "Unexpected prepared x86 payload target: $relative"}
        if([IO.Path]::GetExtension($relative) -in @('.dll','.exe','.addon32','.addon64')){
            $expectedBits=$(if($relative.StartsWith('NeuRotic\Prepared\NeuRotic.GpuHost\',[StringComparison]::OrdinalIgnoreCase)){64}else{32})
            if((Get-SetupPEBits (SafePath $packageRoot $file.path)) -ne $expectedBits){throw "Prepared payload architecture mismatch: $relative"}
        }
    }
}
. (Join-Path $PSScriptRoot 'NeuRotic-Prerequisites.ps1')
$runtimeMinimum = [version]'14.44.35207.0'
if ($build.PSObject.Properties.Name -contains 'runtime_prerequisites') {
    $runtimeMinimum = [version]$build.runtime_prerequisites.visual_cpp_x64_minimum
}
if (-not $GameExecutable) {
    Add-Type -AssemblyName System.Windows.Forms
    $picker = New-Object System.Windows.Forms.OpenFileDialog
    $picker.Title = 'NeuRotic Manual Setup - select your game executable'
    $picker.Filter = 'Game executable (*.exe)|*.exe'
    try {
        if ($picker.ShowDialog() -ne [Windows.Forms.DialogResult]::OK) { Write-Output 'Installation cancelled.'; return }
        $GameExecutable = $picker.FileName
    } finally { $picker.Dispose() }
}
$GameExecutable = (Resolve-Path -LiteralPath $GameExecutable).Path
if ([IO.Path]::GetExtension($GameExecutable) -ine '.exe') { throw 'Select a game executable.' }
if((Get-SetupPEBits $GameExecutable) -ne $targetBits){throw "Target executable architecture does not match this $targetBits-bit installation package."}
$gameDir = Split-Path -Parent $GameExecutable
[void](Enter-NeuroticTargetLock $gameDir)
if ($gameDir -eq $packageRoot -or $gameDir.StartsWith($packageRoot + '\',[StringComparison]::OrdinalIgnoreCase) -or
    $packageRoot.StartsWith($gameDir + '\',[StringComparison]::OrdinalIgnoreCase)) {
    throw 'Keep the complete installer folder outside the game folder.'
}
CheckGameClosed $GameExecutable
$managerRoot = SafePath $gameDir 'NeuRotic\Installer'
$currentStatePath = Join-Path $managerRoot 'Current-Install.json'
$pending=Get-NeuroticPendingTransaction $gameDir
if($pending){throw ('RecoveryRequired: finish the interrupted transaction before installing: '+$pending)}
if(Test-Path -LiteralPath (SafePath $gameDir 'NeuRotic/Installer/History-Transaction.json')){throw 'Finish interrupted history cleanup with Restore in the NeuRotic App before installing.'}
if (Test-Path -LiteralPath (SafePath $gameDir 'NeuRotic\Installer\Uninstall-Transaction.json')) {
    throw 'An interrupted uninstall must be recovered using NeuRotic-Manual-Uninstall.cmd before Setup can continue.'
}
if (Test-Path -LiteralPath (SafePath $gameDir 'NeuRotic\Installer\Cleanup-Pending.json')) {
    throw 'Finish the pending cleanup using NeuRotic-Manual-Uninstall.cmd before running Setup.'
}
$priorState = $null
$retainedState = $null
$adoptedModifiedInstall = $false
$adoptedPriorRelease = $null
$changingFromProxy = $null
$changedFiles = @()
$observedFiles = @()
$relocatedProxies = @()
$reconciledPaths = @()
if (Test-Path -LiteralPath $currentStatePath -PathType Leaf) {
    $priorState = Get-Content -Raw -Encoding UTF8 -LiteralPath $currentStatePath | ConvertFrom-Json
    if ($priorState.kind -ne 'neurotic-public-install' -or $priorState.schema_version -ne 2 -or
        [IO.Path]::GetFullPath([string]$priorState.game_directory).TrimEnd('\') -ne $gameDir) {
        throw 'An invalid managed NeuRotic installation record is present. No files were changed.'
    }
    if ($priorState.status -eq 'uninstalled') {
        $retainedState = $priorState
        $priorState = $null
    } elseif ($priorState.status -ne 'installed-verified') {
        throw 'The managed NeuRotic installation record is incomplete. No files were changed.'
    }
    if($priorState){Initialize-RestoreRecordLedger $gameDir $priorState}
}
if (-not $priorState -and -not $retainedState) {
    # Recover trusted record lineage before choosing/reconciling proxy filenames.
    $legacyChain = @(Get-LegacyRestoreChain $gameDir $null)
    if ($legacyChain.Count) {
        # The common managed path below validates this recovered lineage once.
        $latestLegacy = Read-InstallRecord $legacyChain[0]
        $oldestLegacy = Read-InstallRecord $legacyChain[$legacyChain.Count-1]
        $ascendingChain = @($legacyChain); [array]::Reverse($ascendingChain)
        $priorState = [pscustomobject]@{kind='neurotic-public-install';schema_version=2;status='installed-verified';
            game_directory=$gameDir;game_executable=$GameExecutable;selected_proxy=$latestLegacy.selected_proxy;
            release_name='Recovered NeuRotic installation';first_install_utc=$oldestLegacy.started_utc;
            last_install_utc=$latestLegacy.completed_utc;restore_chain=$ascendingChain;install_history=@()}
    }
}
if ($priorState) {
    $verifyChain = @($priorState.restore_chain | ForEach-Object { [string]$_ }); [array]::Reverse($verifyChain)
    # Append-only install/update/repair uses current-file preimages, not older
    # backup payloads. Check lineage here; validate old bytes only on a restore
    # or the specific original-proxy copy required by ChangeProxy below.
    $observedFiles = @(Test-RestoreChain $gameDir $verifyChain -ReportLiveFiles -MetadataOnly)
    $changedFiles = @($observedFiles | Where-Object { $_.changed })
    $relocatedProxies = @(Get-RelocatedProxies $gameDir $verifyChain)
    $reconciledPaths = @($changedFiles | ForEach-Object { [string]$_.path })
    if ($AdoptModifiedInstall -and $changedFiles.Count) {
        # Explicit legacy adoption still means the user selected a new baseline.
        $adoptedModifiedInstall = $true
        $adoptedPriorRelease = [string]$priorState.release_name
        Write-Host 'The current game files will be preserved as the recovery baseline for this update. Earlier backup records will remain untouched.'
        $priorState = $null
        $relocatedProxies = @()
    }
    if ($priorState) {
    if ($ExistingInstallAction -eq 'Cancel') { Write-Output 'Setup cancelled. No files were changed.'; return }
    if ($ExistingInstallAction -eq 'Uninstall') { $script:GameDirectory = $gameDir; Invoke-PublicUninstall; return }
    if (-not $ProxyName) {
        $choice = $ExistingInstallAction
        if (-not $choice) {
            Write-Host ''
            Write-Host 'NeuRotic is already installed in this game.'
            Write-Host 'Installed application: NeuRotic'
            Write-Host ("Installed as: {0}" -f $priorState.selected_proxy)
            Write-Host ("Last installed: {0}" -f $priorState.last_install_utc)
            Write-Host '1. Update or repair using the same filename'
            Write-Host '2. Change the proxy filename'
            Write-Host '3. Uninstall NeuRotic'
            Write-Host '0. Cancel'
            $answer = (Read-Host 'Choose 0, 1, 2, or 3').Trim()
            $choice = $(switch ($answer) { '1' {'Update'} '2' {'ChangeProxy'} '3' {'Uninstall'} default {'Cancel'} })
        }
        if ($choice -eq 'Cancel') { Write-Output 'Setup cancelled. No files were changed.'; return }
        if ($choice -eq 'Uninstall') {
            $script:GameDirectory = $gameDir
            Invoke-PublicUninstall
            return
        }
        if ($choice -eq 'ChangeProxy') {
            $changingFromProxy = [string]$priorState.selected_proxy
            $ProxyName = SelectProxyName
            if (-not $ProxyName) { Write-Output 'Setup cancelled. No files were changed.'; return }
            if ($ProxyName -ieq $changingFromProxy) { $changingFromProxy = $null; $ExistingProxyAction = 'Replace' }
        } else {
            $ProxyName = [string]$priorState.selected_proxy
            if (-not $ExistingProxyAction -and (Test-Path -LiteralPath (SafePath $gameDir $ProxyName) -PathType Leaf)) { $ExistingProxyAction = 'Replace' }
        }
    } elseif ($ProxyName -ine [string]$priorState.selected_proxy) {
        if ($ExistingInstallAction -ne 'ChangeProxy') {
            throw "NeuRotic is managed as $($priorState.selected_proxy). Choose ChangeProxy or keep the recorded filename."
        }
        $changingFromProxy = [string]$priorState.selected_proxy
    } elseif (-not $ExistingProxyAction -and (Test-Path -LiteralPath (SafePath $gameDir $ProxyName) -PathType Leaf)) {
        $ExistingProxyAction = 'Replace'
    }
}
}
# Existing-install Uninstall/Cancel actions have returned above. Only admission
# to installation/update/repair receives the optional runtime warning.
$runtimeStatus = Get-NeuroticVcRuntimeStatus -MinimumVersion $runtimeMinimum
if (-not $runtimeStatus.Ready) {
    Write-NeuroticVcRuntimeRequirement $runtimeStatus
    if (-not $runtimeStatus.Is64BitOs) { exit 2 }
    if ($CheckOnly) {
        Write-Output 'WARNING: runtime check is advisory; installation may continue at your choice.'
    } else {
        if (-not $ContinueWithoutRuntime) {
            Write-Output '1. Continue installation anyway'
            Write-Output '0. Cancel'
            $runtimeChoice = ([string](Read-Host 'Choose 1 to continue or 0 to cancel')).Trim()
            if ($runtimeChoice -ne '1') {
                Write-Output 'Setup cancelled. No game files have been changed.'
                return
            }
        }
        Write-Output 'Continuing installation anyway. NeuRotic may fail to load or display errors without the runtime.'
    }
} else {
    Write-Output 'PASS: Microsoft Visual C++ x64 runtime prerequisite verified.'
}
$action = 'None'
$scriptedAction = $ExistingProxyAction
$selectedProxy = $ProxyName
if ($ExistingDxgiAction) {
    if ($ExistingProxyAction) { throw 'Use ExistingProxyAction or the legacy ExistingDxgiAction, not both.' }
    if (-not $selectedProxy) { $selectedProxy = 'dxgi.dll' }
    if ($selectedProxy -ine 'dxgi.dll') { throw 'The legacy ExistingDxgiAction parameter applies only to dxgi.dll.' }
    $scriptedAction = $(switch ($ExistingDxgiAction) { 'Rename' {'RenameReShade'} 'Delete' {'Replace'} default {'Cancel'} })
}
while ($true) {
    if (-not $selectedProxy) {
        $selectedProxy = SelectProxyName
        if (-not $selectedProxy) { Write-Output 'Installation cancelled. No game files were changed.'; return }
    }
    $proxyPath = SafePath $gameDir $selectedProxy
    $proxyExists = Test-Path -LiteralPath $proxyPath -PathType Leaf
    if ($proxyExists -and -not $scriptedAction -and @($relocatedProxies | Where-Object { $_.path -ieq $selectedProxy }).Count) { $scriptedAction = 'Replace' }
    if ((Test-Path -LiteralPath $proxyPath) -and -not $proxyExists) { throw "A directory named $selectedProxy occupies the installation target." }
    if (-not $proxyExists) {
        if ($scriptedAction) { throw "ExistingProxyAction was supplied, but no $selectedProxy exists in the selected game folder." }
        break
    }
    # The per-file decision stage below owns unknown/modified destination consent.
    $action = $(if ($scriptedAction) { $scriptedAction } else { 'Replace' })
    if ($action -eq 'Cancel') { Write-Output 'Installation cancelled. No game files were changed.'; return }
    if ($action -eq 'ChooseAnother') {
        if ($scriptedAction) { throw 'ChooseAnother requires interactive filename selection.' }
        $selectedProxy = $null; $action = 'None'; continue
    }
    if ($action -eq 'RenameReShade' -and $selectedProxy -ine 'dxgi.dll') {
        throw 'RenameReShade is available only when the selected filename is dxgi.dll.'
    }
    if ($action -eq 'RenameReShade') {
        if($preparedX86){throw 'Prepared x86 installs replace the selected proxy with recorded recovery; ReShade64 rename is unavailable.'}
        $reshadePath = SafePath $gameDir 'ReShade64.dll'
        if (Test-Path -LiteralPath $reshadePath) { throw 'ReShade64.dll already exists. Setup will not overwrite it. No game files were changed.' }
    }
    break
}
$ProxyName = $selectedProxy
if($preparedX86 -and $ProxyName -ine 'dxgi.dll'){throw 'Prepared x86 installation requires the dxgi.dll ReShade proxy.'}

# Exact recorded-content matches are safely retired. Unknown second loaders still
# require an ownership decision rather than being removed by filename alone.
$retiredObsoleteProxy = $null
$relocatedNames = @($relocatedProxies | ForEach-Object { [string]$_.path })
$otherOptiScaler = @($allowedProxies | Where-Object { $_ -ine $ProxyName -and $_ -ine $changingFromProxy -and $_ -notin $relocatedNames } | Where-Object {
    $candidate = SafePath $gameDir $_
    (Test-Path -LiteralPath $candidate -PathType Leaf) -and
        (Get-Item -LiteralPath $candidate).VersionInfo.OriginalFilename -ieq 'OptiScaler.dll'
})
if ($otherOptiScaler.Count) {
    $canRetire = $adoptedModifiedInstall -and $otherOptiScaler.Count -eq 1 -and $otherOptiScaler[0] -ieq 'OptiScaler.dll'
    $retire = $RetireObsoleteOptiScalerProxy
    if ($canRetire -and -not $retire -and -not $ExistingInstallAction) {
        Write-Host ''
        Write-Host 'An older bare OptiScaler.dll is also present. Setup did not create a record for it.'
        Write-Host '1. Preserve it in this update''s recovery backup and retire it before continuing'
        Write-Host '0. Cancel'
        $retire = ((Read-Host 'Choose 0 or 1').Trim() -eq '1')
    }
    if (-not $canRetire -or -not $retire) {
        throw "NeuRotic/OptiScaler is already installed under $($otherOptiScaler -join ', '). Restore that installation before installing another proxy."
    }
    $obsoletePath = SafePath $gameDir 'OptiScaler.dll'
    $retiredObsoleteProxy = [pscustomobject]@{path='OptiScaler.dll';target=$obsoletePath;sha256=(HashFile $obsoletePath)}
    Write-Host 'The older bare OptiScaler.dll will be preserved in this update''s recovery backup and restored if this update is uninstalled.'
}

$activeReshadeRename=$null
if($priorState){$activeReshadeRename=Get-ManagedReShadeRename @($priorState.restore_chain)}
$files = New-Object System.Collections.Generic.List[object]
$installInspector=$IncludeInspector -or @($observedFiles|Where-Object{$_.path -like '*OptiScaler\CharacterInspector\*'}).Count -gt 0
$iniPlan = $null
$iniOriginalBytes = $null
foreach ($file in $manifest.files) {
    if (-not $file.path.StartsWith('payload\',[StringComparison]::OrdinalIgnoreCase)) { continue }
    $relative = $file.path.Substring(8)
    if(-not $installInspector -and $relative -like '*OptiScaler\CharacterInspector\*'){continue}
    if ($relative -eq $proxySource) { $relative = $ProxyName }
    if($relative -ieq 'ReShade64.dll' -and $action -eq 'RenameReShade'){continue}
    if (-not (AllowedTarget $relative)) { throw "Unexpected payload target: $relative" }
    $target = SafePath $gameDir $relative
    $exists = Test-Path -LiteralPath $target -PathType Leaf
    if ((Test-Path -LiteralPath $target) -and -not $exists) { throw "A directory occupies a file destination: $target" }
    $previousHash = $(if ($exists) { HashFile $target } else { $null })
    $operation = 'copy-package'
    $installedHash = $file.sha256
    if ($relative -eq $configName) {
        $savedSettings = SafePath $gameDir ('NeuRotic\UserData\'+$configName)
        $unchangedRestoredIni = $exists -and $retainedState -and
            ($retainedState.PSObject.Properties.Name -contains 'restored_ini_sha256') -and $previousHash -eq $retainedState.restored_ini_sha256
        $reuseSavedSettings = (-not $exists -or $unchangedRestoredIni) -and (Test-Path -LiteralPath $savedSettings -PathType Leaf)
        $baseBytes = $(if ($reuseSavedSettings) { [IO.File]::ReadAllBytes($savedSettings) } elseif ($exists) { [IO.File]::ReadAllBytes($target) } else { [IO.File]::ReadAllBytes((SafePath $packageRoot $file.path)) })
        if ($exists) { $iniOriginalBytes = [IO.File]::ReadAllBytes($target) }
        if ($preparedX86) {
            $iniPlan=Plan-PreparedReShadeEdit $baseBytes ([IO.File]::ReadAllBytes((SafePath $packageRoot $file.path)))
            $installedHash=HashBytes $iniPlan.Bytes;$operation='write-prepared-reshade-routing'
        } elseif ($action -eq 'RenameReShade') {
            $iniPlan = PlanLoadReshadeEdit $baseBytes
            $installedHash = HashBytes $iniPlan.Bytes
            $operation = 'write-loadreshade-true'
        } elseif ($exists -and -not $reuseSavedSettings) {
            $installedHash = $previousHash
            $operation = 'preserve-existing'
        } else {
            $iniPlan = [pscustomobject]@{Bytes=$baseBytes;Encoding='package';HadBom=$false}
            if ($reuseSavedSettings) { $installedHash = HashBytes $baseBytes; $operation = 'reuse-saved-settings' }
        }
    } elseif($relative -ieq 'ReShade64.dll' -and $activeReshadeRename -and $exists){
        $installedHash=$previousHash;$operation='preserve-existing'
    } elseif($preparedX86 -and $relative -eq 'NeuRotic\Prepared\NeuRotic.GpuHost\OptiScaler.ini'){
        $savedHelper=SafePath $gameDir 'NeuRotic\UserData\PreparedGpuHost.ini'
        if($exists){$installedHash=$previousHash;$operation='preserve-existing'}
        elseif(Test-Path -LiteralPath $savedHelper -PathType Leaf){$file=[pscustomobject]@{path=$savedHelper;sha256=(HashFile $savedHelper)};$installedHash=$file.sha256;$operation='copy-user-dependency'}
    }
    $entry = [pscustomobject][ordered]@{path=$relative;source=$file.path;operation=$operation;existed=$exists;
        previous_hash=$previousHash;installed_hash=$installedHash}
    if($script:RestoreMetadataMissing){
        $entry|Add-Member -NotePropertyName original_unknown -NotePropertyValue $true
        if($operation -eq 'preserve-existing' -and (Test-SettingsTarget $relative)){$entry|Add-Member -NotePropertyName ownership_unproven -NotePropertyValue $true}
    }
    if ($relocatedProxies.Count -and $relocatedProxies.path -contains $relative) { $entry | Add-Member -NotePropertyName restore_exists -NotePropertyValue $false }
    $files.Add($entry)
}
if ($action -eq 'RenameReShade') {
    $files.Add([pscustomobject][ordered]@{path='ReShade64.dll';source=$null;operation='rename-existing-proxy';existed=$false;
        previous_hash=$null;installed_hash=(HashFile $proxyPath)})
}
if ($retiredObsoleteProxy) {
    $files.Add([pscustomobject][ordered]@{path=$retiredObsoleteProxy.path;source=$null;operation='retire-obsolete-optiscaler-proxy';existed=$true;
        previous_hash=$retiredObsoleteProxy.sha256;installed_exists=$false;installed_hash=$null})
}
foreach ($file in $relocatedProxies) {
    if ($file.path -ieq $ProxyName) { continue }
    $files.Add([pscustomobject][ordered]@{path=$file.path;source=$null;operation='retire-relocated-managed-proxy';existed=$true;
        previous_hash=$file.sha256;installed_exists=$false;installed_hash=$null;restore_exists=$false})
}
# Generated executable support is ordinary owned content, reviewed before any write.
$runtimeHelper=Join-Path $PSScriptRoot 'NeuRotic-RuntimeFiles.ps1'
if(Test-Path -LiteralPath $runtimeHelper -PathType Leaf){
    . $runtimeHelper
    foreach($dependency in @(Get-NeuroticRuntimeProjection (Split-Path -Parent $PSScriptRoot) $gameDir $preparedX86)){
        if(@($files|Where-Object{$_.path -ieq $dependency.path}).Count){throw 'Runtime deposit conflicts with packaged payload'}
        [void](SafePath $gameDir $dependency.path);$files.Add($dependency)
    }
}
$runtimeProjectionIdentity=Get-NeuroticRuntimeProjectionIdentity $files.ToArray()
if($HubExpectedRuntimeIdentity -and $runtimeProjectionIdentity -ne $HubExpectedRuntimeIdentity){throw 'Runtime deposit changed since the reviewed plan; refresh and review again. No game files were changed.'}
$launcherBytes=Get-NeuroticUninstallLauncherBytes
foreach($generated in @([pscustomobject]@{path='Uninstall NeuRotic.cmd';bytes=$launcherBytes;source=$null},[pscustomobject]@{path='NeuRotic\Installer\NeuRotic-Setup-Engine.ps1';bytes=$null;source=(Join-Path $PSScriptRoot 'NeuRotic-Setup-Engine.ps1')})){
    $target=SafePath $gameDir $generated.path;$exists=Test-Path -LiteralPath $target -PathType Leaf
    $hash=if($generated.bytes){HashBytes $generated.bytes}else{HashFile $generated.source}
    $files.Add([pscustomobject]@{path=$generated.path;source=$generated.source;bytes=$generated.bytes;operation='write-generated';existed=$exists;previous_hash=$(if($exists){HashFile $target}else{$null});installed_hash=$hash})
}
# Reconcile the complete previous effective set, including files omitted by this release.
foreach($old in $observedFiles){
    if(-not $old.owned -or (Test-SettingsTarget $old.path) -or $old.path -like '*\nvngx_dlssnr.dll' -or $old.path -ieq 'nvngx_dlssnr.dll' -or $old.path -like '*\streamline\*' -or @($files|Where-Object{$_.path -ieq $old.path}).Count -or $old.path -ieq $changingFromProxy){continue}
    $files.Add([pscustomobject]@{path=$old.path;source=$null;operation='retire-obsolete-component';existed=$true;previous_hash=$old.sha256;installed_exists=$false;installed_hash=$null})
}
$connectionPlan=$null
if($ConnectionWrapper -or $ConnectionDependencyManifest) {
 if(-not $ConnectionWrapper -or -not $ConnectionDependencyManifest){throw 'Wrapper and dependency manifest must be selected together'}
 . (Join-Path $PSScriptRoot 'NeuRotic-ConnectionDependencies.ps1')
 $connectionPlan=Plan-ConnectionInstall $GameExecutable $ConnectionWrapper $ConnectionDependencyManifest $ProxyName
 foreach($entry in $connectionPlan.files){if(@($files|Where-Object{$_.path -ieq $entry.path}).Count){throw 'Dependency conflicts with managed payload'};$files.Add($entry)}
 Write-Output ("Optional translator: {0} {1}; restart required. Rendering remains unqualified until observed." -f $connectionPlan.identity,$connectionPlan.version)
}
$oldProxyTransition = $null
if ($changingFromProxy) {
    if (-not $priorState.restore_chain.Count) { throw 'The managed installation has no restore history for a proxy change.' }
    # A bundled ReShade64.dll is ordinary payload. Only a recorded rename ties
    # it to the original dxgi.dll and requires restoring that pair together.
    if ($changingFromProxy -ieq 'dxgi.dll' -and $activeReshadeRename) {
        throw 'A managed ReShade proxy move is active. Uninstall first so dxgi.dll and ReShade64.dll can be restored together, then choose the new proxy.'
    }
    $latestRecord = Read-InstallRecord ([string]$priorState.restore_chain[$priorState.restore_chain.Count-1])
    $latestEntry = @($latestRecord.files | Where-Object { $_.path -ieq $changingFromProxy })
    if ($latestEntry.Count -ne 1) { throw 'The active proxy is not represented exactly once in the latest installation record.' }
    $oldProxyPath = SafePath $gameDir $changingFromProxy
    $oldProxyExists = Test-Path -LiteralPath $oldProxyPath -PathType Leaf
    if ((Test-Path -LiteralPath $oldProxyPath) -and -not $oldProxyExists) { throw "Directory occupies old proxy: $oldProxyPath" }
    $oldestRecordPath = $null
    foreach ($historyPath in $priorState.restore_chain) {
        [void](Assert-PathUnderRoot $gameDir $historyPath)
        if (@((Read-InstallRecord $historyPath).files | Where-Object { $_.path -ieq $changingFromProxy }).Count) {
            $oldestRecordPath = [string]$historyPath; break
        }
    }
    if (-not $oldestRecordPath) { throw 'No original state was recorded for this proxy.' }
    $oldestRecord = Read-InstallRecord $oldestRecordPath
    $baselineEntry = @($oldestRecord.files | Where-Object { $_.path -ieq $changingFromProxy })
    if ($baselineEntry.Count -ne 1) { throw 'The original proxy baseline is ambiguous.' }
    $baselineExists = [bool]$baselineEntry[0].existed -and -not (($baselineEntry[0].PSObject.Properties.Name -contains 'restore_exists') -and -not $baselineEntry[0].restore_exists)
    $baselineHash = $(if ($baselineExists) {[string]$baselineEntry[0].previous_hash} else {$null})
    $baselineSource = $(if ($baselineExists) { SafePath (Join-Path (Split-Path -Parent $oldestRecordPath) 'previous') $changingFromProxy } else {$null})
    if ($baselineExists -and (HashFile $baselineSource) -ne $baselineHash) { throw 'The original proxy baseline backup failed verification.' }
    $oldProxyTransition = [pscustomobject]@{path=$changingFromProxy;target=$oldProxyPath;final_exists=$baselineExists;final_hash=$baselineHash;source=$baselineSource}
    $files.Add([pscustomobject][ordered]@{path=$changingFromProxy;source=$null;operation='restore-old-managed-proxy';existed=$oldProxyExists;
        previous_hash=$(if ($oldProxyExists) { HashFile $oldProxyPath } else { $null });installed_exists=$baselineExists;installed_hash=$baselineHash})
}
$preserved = @()
# The private model is outside installer ownership. Observe presence only;
# model validation belongs to its runtime consumer, and external edits are valid.
$modelPath = Join-Path $gameDir 'nvngx_dlssnr.dll'
if (Test-Path -LiteralPath $modelPath -PathType Leaf) { $preserved += @{path='nvngx_dlssnr.dll';ownership='user';operation='leave untouched'} }

$resolution=Resolve-InstallFileDecisions $files @($observedFiles+$relocatedProxies) $FileDecisionsJson -NonInteractive:($CheckOnly -or $HubNonInteractive) -LegacyProxy $ProxyName -LegacyAction $explicitProxyAction -Review:$CheckOnly
$fileConflicts=@($resolution.conflicts)
$skippedFiles=@($resolution.decisions.Keys|Where-Object{$resolution.decisions[$_] -eq 'Skip'})
$cancelled=@($resolution.decisions.Values|Where-Object{$_ -eq 'Cancel'}).Count -gt 0
$changedFiles=@($files|Where-Object{$resolution.decisions[[string]$_.path] -eq 'SaveAndReplace'}|ForEach-Object{[pscustomobject]@{path=$_.path;existed=$_.existed;sha256=$_.previous_hash}})
$discardModifiedPreimages=@($files|Where-Object{
    $resolution.decisions[[string]$_.path] -eq 'Replace' -and
    @($observedFiles|Where-Object{$_.changed}|ForEach-Object{$_.path}) -contains $_.path
})
foreach($entry in $discardModifiedPreimages){$entry|Add-Member -NotePropertyName temporary_modified_preimage -NotePropertyValue $true}
# Ordinary unchanged managed payload needs neither another preimage nor a write.
# Proxy/config rows retain the transaction identity expected by older restorers.
$ownedNames=@{};foreach($entry in $observedFiles){if($entry.owned){$ownedNames[[string]$entry.path]=$true}}
$unchangedRequired=@()
foreach($entry in @($files.ToArray())){
    if($entry.existed -and $entry.previous_hash -eq $entry.installed_hash){
        if($entry.path -ine $ProxyName -and $entry.path -ine $configName -and $ownedNames.ContainsKey([string]$entry.path)){$unchangedRequired+=$entry;$files.Remove($entry)|Out-Null}
        else{
            if($entry.operation -eq 'copy-package'){$entry.operation='preserve-existing'}
            if($ownedNames.ContainsKey([string]$entry.path)){$entry|Add-Member -NotePropertyName unchanged_owned -NotePropertyValue $true}
        }
    }
}
if(@($files|Where-Object{$_.path -ieq $ProxyName -and $_.operation -eq 'preserve-existing'}).Count -and $action -eq 'Replace'){$action='None'}

Write-Output ''
Write-Output 'NeuRotic Manual Setup'
Write-Output "Game: $GameExecutable"
Write-Output "Install target: $ProxyName"
if ($changedFiles.Count) { Write-Output ('Changed files: preserve a separate copy and retain original uninstall history: ' + ($changedFiles.path -join ', ')) }
if ($relocatedProxies.Count) { Write-Output ('Matching relocated loaders: preserve a separate copy, then retire: ' + ($relocatedProxies.path -join ', ')) }
if ($oldProxyTransition) { Write-Output "Proxy change: restore/remove managed $changingFromProxy, then install as $ProxyName in one rollback transaction." }
if ($retiredObsoleteProxy) { Write-Output 'Older bare OptiScaler.dll: preserve in recovery backup, then retire it before installing the selected proxy.' }
if ($action -eq 'RenameReShade') {
    Write-Output 'Existing dxgi.dll: rename to ReShade64.dll and enable LoadReshade.'
} elseif ($action -eq 'Replace') {
    Write-Output "Existing ${ProxyName}: preserve in recovery backup, then delete and replace it."
} else {
    Write-Output "No existing ${ProxyName}: install immediately."
}
Write-Output "Setup creates a recovery folder before changing files. Restore returns $ProxyName and $configName to their exact original state."
Write-Output 'Your private NR files are kept across updates. NeuRotic verifies the selected model when it is used.'
if (-not (Test-Path -LiteralPath $modelPath -PathType Leaf)) {
 Write-Output 'This game has no private NR file beside its executable. Existing game-local models and persistent AppData files are always preserved.'
}
$priorChain = @()
if ($priorState) { $priorChain = @($priorState.restore_chain | ForEach-Object { [string]$_ }) }
elseif (-not $adoptedModifiedInstall -and -not $retainedState) { $priorChain = @(Get-LegacyRestoreChain $gameDir $null); [array]::Reverse($priorChain) }
Assert-NeuroticHistoryCapacity $priorChain
if ($HubPlanPreviewPath) {
    if (-not $CheckOnly) { throw 'Structured preview requires CheckOnly' }
    $previewChanges = @($files.ToArray())
    if(Get-Variable RuntimeDeploymentWarnings -Scope Script -ErrorAction SilentlyContinue){foreach($warning in $script:RuntimeDeploymentWarnings){$previewChanges += [ordered]@{path=$configName;operation=$warning}}}
    $previewChanges += [ordered]@{path='NeuRotic-backups/install-<transaction>';operation='save verified preimages and restore manifest'}
    $previewChanges += [ordered]@{path='NeuRotic/Installer';operation='write installation receipt and trusted uninstaller'}
    $previewChanges += [ordered]@{path='Uninstall NeuRotic.cmd';operation='write uninstall launcher'}
    SaveRecord $HubPlanPreviewPath ([ordered]@{changes=$previewChanges;preserved=$preserved;runtimeReady=$runtimeStatus.Ready;runtimeDependencyIdentity=$runtimeProjectionIdentity;fileConflicts=$fileConflicts;skippedFiles=$skippedFiles;skipped=($skippedFiles.Count -gt 0);cancelled=$cancelled})
}
if ($CheckOnly) {
    if($fileConflicts.Count){Write-Output 'Per-file decisions are required; preview made no game changes.'}
    elseif($cancelled -or $skippedFiles.Count){Write-Output 'Installation cancelled or skipped; preview made no game changes.'}
    else{Write-Output 'PASS: installer preview; payload bytes will be verified during execution. No game files changed.'}
    return
}
if($cancelled){
    Write-Output 'Installation cancelled. No game files were changed.'
    if($HubOperationResultPath){SaveRecord $HubOperationResultPath ([ordered]@{status='Cancelled';operation='Install';skippedFiles=@()})}
    return
}
if($skippedFiles.Count){throw ('Installation skipped to preserve a consistent existing installation. No game files were changed. Skipped: '+($skippedFiles -join ', '))}
if($fileConflicts.Count){throw ('Per-file decisions required before installation: '+($fileConflicts.path -join ', '))}
if($connectionPlan){Test-ConnectionPreimage $connectionPlan}
Assert-LiveFiles $gameDir @($observedFiles + $relocatedProxies)

$run = [Guid]::NewGuid().ToString('N').Substring(0,12)
$backup = SafePath $gameDir ("NeuRotic-backups\install-$run")
$verifyChain = @($priorChain); [array]::Reverse($verifyChain)
if ($verifyChain.Count -and -not $priorState) {
    $legacyObserved = @(Test-RestoreChain $gameDir $verifyChain -ReportLiveFiles -MetadataOnly)
    if (-not $priorState) {
        $changedFiles = @($legacyObserved | Where-Object { $_.changed })
        $reconciledPaths = @($changedFiles | ForEach-Object { [string]$_.path })
        Assert-LiveFiles $gameDir @($files | ForEach-Object { [pscustomobject]@{path=$_.path;existed=$_.existed;sha256=$_.previous_hash} })
    }
}
$createdDirectories = @()
foreach ($file in $files) {
    $parent = Split-Path -Parent (SafePath $gameDir $file.path)
    while ($parent -ine $gameDir) {
        if (-not (Test-Path -LiteralPath $parent)) { $createdDirectories += $parent.Substring($gameDir.Length+1) }
        $parent = Split-Path -Parent $parent
    }
}
foreach ($file in $files) {
    $restorePath = SafePath (Join-Path $backup 'restore-00000000') $file.path
    if ($restorePath.Length -ge 248) { throw 'The game folder path is too long for reliable Windows PowerShell backup/restore. No game files were changed.' }
}
Ensure-NeuroticDirectory $backup
if($connectionPlan){
 foreach($entry in $connectionPlan.files){
  if($entry.operation -eq 'copy-user-dependency'){
   $staged=SafePath $backup ('dependency-staging\'+$entry.path)
   CopyVerified $entry.source $staged $entry.installed_hash 'Absent'
   $entry.source=$staged
  }
 }
 Test-ConnectionPreimage $connectionPlan
}
Save-ChangedFiles $gameDir @($changedFiles + $relocatedProxies)
Show-SetupProgress 'Backing up files' 0 $files.Count
$backupCount = 0
foreach ($file in $files) {
    if ($file.existed -and -not (($file.PSObject.Properties.Name -contains 'unchanged_owned') -and $file.unchanged_owned)) { CopyVerified (SafePath $gameDir $file.path) (SafePath (Join-Path $backup 'previous') $file.path) $file.previous_hash }
    $backupCount++; Show-SetupProgress 'Backing up files' $backupCount $files.Count $file.path
}
$managerWasPresent = Test-Path -LiteralPath $managerRoot -PathType Container
$managerSnapshot = Join-Path $backup 'manager-previous'
if ($managerWasPresent) {
    foreach ($name in @('NeuRotic-Setup-Engine.ps1','Current-Install.json')) {
        $managerFile = SafePath $managerRoot $name
        if (Test-Path -LiteralPath $managerFile -PathType Leaf) { CopyVerified $managerFile (SafePath $managerSnapshot $name) (HashFile $managerFile) }
    }
}
$priorInstalledLauncher = Join-Path $gameDir 'Uninstall NeuRotic.cmd'
$launcherSnapshot = Join-Path $backup 'Uninstall NeuRotic.cmd.previous'
if (Test-Path -LiteralPath $priorInstalledLauncher -PathType Leaf) { CopyVerified $priorInstalledLauncher $launcherSnapshot (HashFile $priorInstalledLauncher) }
$ownerSource=Join-Path $PSScriptRoot 'NeuRotic-Setup-Engine.ps1'
CopyVerified $ownerSource (Join-Path $backup 'NeuRotic-Setup-Engine.ps1') (HashFile $ownerSource)
$restoreBytes=[Text.Encoding]::ASCII.GetBytes("@echo off`r`nsetlocal`r`ntitle NeuRotic Restore`r`nset `"PSModulePath=`"`r`n`"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe`" -NoProfile -ExecutionPolicy Bypass -File `"%~dp0NeuRotic-Setup-Engine.ps1`" -Restore %*`r`nset `"result=%ERRORLEVEL%`"`r`nif not `"%result%`"==`"0`" echo Restore did not complete. Read the message above.`r`npause`r`nexit /b %result%`r`n")
WriteVerifiedBytes $restoreBytes (Join-Path $backup 'Restore.cmd') (HashBytes $restoreBytes)
$iniEntry = @($files | Where-Object { $_.path -eq $configName })[0]
$record = [ordered]@{kind='neurotic-customer-candidate-install';status='backed-up';commit=$manifest.commit;
    package_manifest_sha256=(HashFile $manifestPath);game_executable=$GameExecutable;backup=$backup;
    selected_proxy=$ProxyName;existing_proxy_action=$action;files=$files;preserved=$preserved;
    original_proxy=[ordered]@{name=$ProxyName;existed=$proxyExists;sha256=(@($files | Where-Object { $_.path -eq $ProxyName })[0].previous_hash);backup_path=$(if ($proxyExists) {"previous\$ProxyName"} else {$null})};
    original_ini=[ordered]@{name=$configName;existed=$iniEntry.existed;sha256=$iniEntry.previous_hash;
        bytes_base64=$(if ($iniEntry.existed) {[Convert]::ToBase64String($iniOriginalBytes)} else {$null});backup_path=$(if ($iniEntry.existed) {'previous\'+$configName} else {$null})};
    install_route=$(if($preparedX86){'prepared-x86'}else{'native-x64'});target_bit_size=$targetBits;proxy_source=$proxySource;
    reshade_operation=$(if ($action -eq 'RenameReShade') {[ordered]@{original_name='dxgi.dll';new_name='ReShade64.dll';sha256=(HashFile $proxyPath);loadreshade=$true;encoding=$iniPlan.Encoding;bom_preserved=$iniPlan.HadBom}} else {$null});
    ini_disposition=$(if ($action -eq 'RenameReShade') {'LoadReshade=true targeted encoding-preserving edit'} elseif ($iniEntry.operation -eq 'reuse-saved-settings') {'reuse-saved-settings'} elseif ($iniEntry.existed) {'preserve-live'} else {'reviewed-default'});
    runtime='Inconclusive';started_utc=[DateTime]::UtcNow.ToString('o');completed_utc=$null;restored_utc=$null;
    error=$null;rollback_error=$null;owned_files=@();created_directories=@($createdDirectories | Select-Object -Unique);
    previous_record=$(if ($priorChain.Count) { $priorChain[$priorChain.Count-1] } else { $null });reconciled_paths=$reconciledPaths}
foreach ($ownedFile in @(Get-ChildItem -LiteralPath $backup -Recurse -File)) {
    $relative = $ownedFile.FullName.Substring($backup.Length+1)
    if (-not $relative.StartsWith('previous\')) { $record.owned_files += [pscustomobject]@{path=$relative;sha256=(HashFile $ownedFile.FullName)} }
}
$recordPath = Join-Path $backup 'INSTALL-MANIFEST.json'
SaveRecord $recordPath $record
$mutationWrites=@($files|ForEach-Object{[pscustomobject]@{path=$_.path;after=$(if(($_.PSObject.Properties.Name -contains 'installed_exists') -and -not $_.installed_exists){'Absent'}else{$_.installed_hash})}})
$mutationWrites += [pscustomobject]@{path='NeuRotic\Installer\Current-Install.json';after=$(if(Test-Path -LiteralPath $currentStatePath -PathType Leaf){HashFile $currentStatePath}else{'Absent'})}
$installJournal=New-NeuroticMutationJournal $gameDir 'Install' $mutationWrites $recordPath
Invoke-NeuroticInstallBoundary $gameDir 'after-journal'
$touched = New-Object System.Collections.Generic.List[string]
$managerTouched=@{}
function Touch([string]$Path) { if (-not $touched.Contains($Path)) { $touched.Add($Path) } }
try {
    CheckGameClosed $GameExecutable
    if ($proxyExists -and (HashFile $proxyPath) -ne $record.original_proxy.sha256) { throw "$ProxyName changed before installation." }
    if ($action -eq 'RenameReShade') {
        $reshadePath = SafePath $gameDir 'ReShade64.dll'
        if (Test-Path -LiteralPath $reshadePath) { throw 'ReShade64.dll appeared before installation.' }
        Touch $ProxyName; Touch 'ReShade64.dll'
        Move-Item -LiteralPath $proxyPath -Destination $reshadePath
        if ((Test-Path -LiteralPath $proxyPath) -or (HashFile $reshadePath) -ne $record.original_proxy.sha256) { throw 'Existing dxgi.dll rename verification failed.' }
    } elseif ($action -eq 'Replace') {
        Touch $ProxyName; Remove-NeuroticPath -LiteralPath $proxyPath -Force -ExpectedHash $record.original_proxy.sha256
        if (Test-Path -LiteralPath $proxyPath) { throw "Existing $ProxyName could not be removed." }
        Invoke-NeuroticInstallBoundary $gameDir 'after-proxy-removal'
    }
    if ($oldProxyTransition) {
        $preimage = @($files | Where-Object { $_.path -eq $oldProxyTransition.path })[0]
        Assert-LiveFiles $gameDir @([pscustomobject]@{path=$preimage.path;existed=$preimage.existed;sha256=$preimage.previous_hash})
        Touch $oldProxyTransition.path
        if ($oldProxyTransition.final_exists) {
            CopyVerified $oldProxyTransition.source $oldProxyTransition.target $oldProxyTransition.final_hash $(if($preimage.existed){$preimage.previous_hash}else{'Absent'})
        } elseif (Test-Path -LiteralPath $oldProxyTransition.target -PathType Leaf) {
            Remove-NeuroticPath -LiteralPath $oldProxyTransition.target -Force -ExpectedHash $preimage.previous_hash
        }
    }
    if ($retiredObsoleteProxy) {
        Touch $retiredObsoleteProxy.path
        if ((HashFile $retiredObsoleteProxy.target) -ne $retiredObsoleteProxy.sha256) { throw 'The older bare OptiScaler.dll changed before installation.' }
        Remove-NeuroticPath -LiteralPath $retiredObsoleteProxy.target -Force -ExpectedHash $retiredObsoleteProxy.sha256
        if (Test-Path -LiteralPath $retiredObsoleteProxy.target) { throw 'The older bare OptiScaler.dll could not be retired.' }
    }
    foreach ($file in $relocatedProxies) {
        if ($file.path -ieq $ProxyName) { continue }
        $target = SafePath $gameDir $file.path
        if ((HashFile $target) -ne $file.sha256) { throw "Relocated loader changed before repair: $target" }
        Touch $file.path; Remove-NeuroticPath -LiteralPath $target -Force -ExpectedHash $file.sha256
    }
    $installCount = 0
    Show-SetupProgress 'Installing files' 0 $files.Count
    foreach ($file in $files) {
        Show-SetupProgress 'Installing files' $installCount $files.Count $file.path; $installCount++
        if ($file.operation -in @('rename-existing-proxy','restore-old-managed-proxy','retire-obsolete-optiscaler-proxy','retire-relocated-managed-proxy')) { continue }
        $target = SafePath $gameDir $file.path
        if($file.operation -eq 'retire-obsolete-component'){
            Touch $file.path;Remove-NeuroticPath -LiteralPath $target -ExpectedHash $file.previous_hash;continue
        }
        if ($file.operation -eq 'preserve-existing') {
            if ((HashFile $target) -ne $file.previous_hash) { throw "Preserved configuration changed before installation: $($file.path)" }
            continue
        }
        if ($file.path -ne $ProxyName) {
            if ($file.existed) {
                if ((HashFile $target) -ne $file.previous_hash) { throw "Destination changed during installation: $($file.path)" }
            } elseif (Test-Path -LiteralPath $target) { throw "A new destination appeared during installation: $($file.path)" }
        } elseif (Test-Path -LiteralPath $target) { throw "$ProxyName unexpectedly exists before installation." }
        Touch $file.path
        $expectedDestination=$(if($file.path -eq $ProxyName -or -not $file.existed){'Absent'}else{$file.previous_hash})
        if ($file.path -eq $configName) {
            $written=WriteVerifiedBytes $iniPlan.Bytes $target $file.installed_hash $expectedDestination
            if(-not $written){$failure=New-Object IOException("Destination changed at write boundary: $target");$failure.Data['NeuRoticRefusedPath']=$target;throw $failure}
        } elseif($file.operation -eq 'write-generated') {
            if($file.bytes){if(-not (WriteVerifiedBytes $file.bytes $target $file.installed_hash $expectedDestination)){throw 'Generated file changed at publication boundary'}}
            else{CopyVerified $file.source $target $file.installed_hash $expectedDestination}
        } elseif($file.operation -eq 'copy-user-dependency') {
            CopyVerified $file.source $target $file.installed_hash $expectedDestination
        } else {
            CopyVerified (SafePath $packageRoot $file.source) $target $file.installed_hash $expectedDestination
        }
        Invoke-NeuroticInstallBoundary $gameDir ('after-payload:'+ $file.path)
    }
    Show-SetupProgress 'Installing files' $files.Count $files.Count
    $verifyCount = 0
    $requiredVerification=@($files.ToArray())+@($unchangedRequired)
    Show-SetupProgress 'Verifying installation' 0 $requiredVerification.Count
    foreach ($file in $requiredVerification) {
        $target = SafePath $gameDir $file.path
        $expectsInstalled = -not ($file.PSObject.Properties.Name -contains 'installed_exists') -or [bool]$file.installed_exists
        if ($expectsInstalled) {
            if (-not (Test-Path -LiteralPath $target -PathType Leaf) -or (HashFile $target) -ne $file.installed_hash) { throw "Installed verification failed: $($file.path)" }
        } elseif (Test-Path -LiteralPath $target) { throw "Removed proxy still exists after the managed proxy change: $($file.path)" }
        $verifyCount++; Show-SetupProgress 'Verifying installation' $verifyCount $requiredVerification.Count $file.path
    }
    $record.status='installed-verified'; $record.completed_utc=[DateTime]::UtcNow.ToString('o'); SaveRecord $recordPath $record
    Invoke-NeuroticInstallBoundary $gameDir 'after-record'

    $installedEngine=Join-Path $managerRoot 'NeuRotic-Setup-Engine.ps1'
    $chain = @($priorChain + $recordPath)
    $history = @()
    if ($priorState -and $priorState.install_history) { $history += @($priorState.install_history) }
    $history += [pscustomobject][ordered]@{install_id=$run;release_name=$(if ($manifest.name) {[string]$manifest.name} else {'NeuRotic'});
        commit=[string]$manifest.commit;installed_utc=$record.completed_utc;selected_proxy=$ProxyName;record=$recordPath}
    # The three recent restore transactions are retained separately from the
    # original-baseline checkpoint by post-commit maintenance below.
    $history = @($history | Select-Object -Last 3)
    $state = [ordered]@{kind='neurotic-public-install';schema_version=2;status='installed-verified';product='NeuRotic';
        release_name=$(if ($manifest.name) {[string]$manifest.name} else {'NeuRotic'});commit=[string]$manifest.commit;
        game_executable=$GameExecutable;game_directory=$gameDir;selected_proxy=$ProxyName;
        install_route=$(if($preparedX86){'prepared-x86'}else{'native-x64'});target_bit_size=$targetBits;proxy_source=$proxySource;
        first_install_utc=$(if ($priorState) {[string]$priorState.first_install_utc} else {$record.completed_utc});last_install_utc=$record.completed_utc;
        install_history=$history;restore_chain=$chain;installed_uninstaller='Uninstall NeuRotic.cmd';
        restore_record_ledger=@($chain|ForEach-Object{$recordBytes=Get-InstallRecordBytes $_;[pscustomobject]@{path=$_;sha256=(HashBytes $recordBytes);record_json=[Text.Encoding]::UTF8.GetString($recordBytes)}});
        preserved=$preserved;uninstalled_utc=$null;uninstall_mode=$null;
        history_retention=[ordered]@{keepRecent=3;baselineSeparate=$true;status='Deferred';reason='Automatic history maintenance has not completed.';recordCount=$chain.Count};
        manager_files=@([pscustomobject]@{path='NeuRotic\Installer\NeuRotic-Setup-Engine.ps1';sha256=(HashFile $installedEngine)})}
    if ($adoptedModifiedInstall) {
        $state.adopted_modified_install = $true
        $state.adopted_previous_release = $adoptedPriorRelease
        $state.adopted_utc = $record.completed_utc
    }
    $stateBytes=[Text.Encoding]::UTF8.GetBytes(($state|ConvertTo-Json -Depth 20))
    foreach($entry in $installJournal.files){if($entry.path -eq 'NeuRotic\Installer\Current-Install.json'){$entry.after=HashBytes $stateBytes}}
    SaveRecord (SafePath $gameDir 'NeuRotic\Installer\Install-Transaction.json') $installJournal
    SaveRecord $currentStatePath $state
    Invoke-NeuroticInstallBoundary $gameDir 'after-state'
    $managerTouched['Current-Install.json']=HashFile $currentStatePath
    Complete-NeuroticMutation $gameDir $installJournal
} catch {
    $record.error=$_.Exception.Message
    $refusedPath=Get-NeuroticFailureProperty $_.Exception 'NeuRoticRefusedPath'
    if($refusedPath){$relative=$refusedPath.Substring($gameDir.Length+1);[void]$touched.Remove($relative)}
    try {
        for ($i=$touched.Count-1; $i -ge 0; --$i) {
            $path=$touched[$i]
            $file=@($files | Where-Object { $_.path -eq $path })[0]
            $target=SafePath $gameDir $path
            $rollbackExpected='Absent'
            if(Test-Path -LiteralPath $target -PathType Leaf){
                $liveHash=HashFile $target
                $rollbackExpected=$liveHash
                if($file.existed -and $liveHash -eq $file.previous_hash){continue}
                if($liveHash -ne $file.installed_hash){throw "File changed before rollback; preserved for review: $target"}
            }
            if ($file.existed) {
                if ((Test-Path -LiteralPath $target -PathType Leaf) -and (HashFile $target) -eq $file.previous_hash) { continue }
                CopyVerified (SafePath (Join-Path $backup 'previous') $path) $target $file.previous_hash $rollbackExpected
            } elseif ($rollbackExpected -ne 'Absent') { Remove-NeuroticPath -LiteralPath $target -Force -ExpectedHash $rollbackExpected }
        }
        foreach ($name in @('NeuRotic-Setup-Engine.ps1','Current-Install.json')) {
            if(-not $managerTouched.ContainsKey($name)){continue}
            $target = SafePath $managerRoot $name
            $saved = SafePath $managerSnapshot $name
            if(Test-Path -LiteralPath $target -PathType Leaf){
                $liveHash=HashFile $target
                if((Test-Path -LiteralPath $saved -PathType Leaf) -and $liveHash -eq (HashFile $saved)){continue}
                if($liveHash -ne $managerTouched[$name]){throw "Manager file changed during rollback; preserved: $target"}
                $rollbackExpected=$liveHash
            }else{$rollbackExpected='Absent'}
            if (Test-Path -LiteralPath $saved -PathType Leaf) { CopyVerified $saved $target (HashFile $saved) $rollbackExpected }
            elseif ($rollbackExpected -ne 'Absent') { Remove-NeuroticPath -LiteralPath $target -Force -ExpectedHash $rollbackExpected }
        }
        $installedLauncher = Join-Path $gameDir 'Uninstall NeuRotic.cmd'
        if($managerTouched.ContainsKey('launcher')){
            if (Test-Path -LiteralPath $installedLauncher -PathType Leaf) { Remove-NeuroticPath -LiteralPath $installedLauncher -Force -ExpectedHash $managerTouched['launcher'] }
            if (Test-Path -LiteralPath $launcherSnapshot -PathType Leaf) { CopyVerified $launcherSnapshot $installedLauncher (HashFile $launcherSnapshot) 'Absent' }
        }
        $record.status='failed-rolled-back'
        Complete-NeuroticMutation $gameDir $installJournal
    } catch { $record.status='failed-rollback-incomplete';$record.rollback_error=$_.Exception.Message }
    SaveRecord $recordPath $record
    $failure = New-Object InvalidOperationException("Installation failed: $($record.status): $($record.error). See $recordPath")
    $failure.Data['NeuRoticMutationStatus'] = $(if($record.status -eq 'failed-rolled-back'){'FailedRolledBack'}else{'RecoveryRequired'})
    throw $failure
}
# Retention follows a completed installation and has its own durable journal.
# Never roll a successful installation back after old history has been pruned.
foreach($entry in $discardModifiedPreimages){
    $temporary=SafePath (Join-Path $backup 'previous') $entry.path
    try{Remove-OwnedFile (Join-Path $backup 'previous') $entry.path $entry.previous_hash}
    catch{Write-Warning ('Replacement succeeded; temporary rollback preimage could not be retired: '+$temporary)}
}
$historyHelper=Join-Path $PSScriptRoot 'NeuRotic-HubHistory.ps1'
if((Test-Path -LiteralPath $historyHelper -PathType Leaf) -and -not @($chain|Where-Object{-not(Test-Path -LiteralPath $_ -PathType Leaf)}).Count){
    . $historyHelper
    $retention=Invoke-AutomaticHistoryRetention ([pscustomobject]@{directory=$gameDir;executable=$GameExecutable})
    if($retention.status -eq 'Complete'){Write-Output 'History policy: keep the three newest restore records plus a separate original-baseline checkpoint when needed.'}
    else{Write-Warning ('Installation completed; automatic history cleanup was deferred. '+$retention.reason)}
}else{Write-Warning 'Installation completed; automatic history maintenance is unavailable or deferred for recovered records. Existing backups were preserved.'}
Write-Output "PASS: full installation verified. Recovery folder: $backup"
if($HubOperationResultPath){SaveRecord $HubOperationResultPath ([ordered]@{status='Succeeded';operation='Install';installedRecord=$recordPath;selectedProxy=$ProxyName;skippedFiles=@();unavailableOriginals=@();preservedForeignFiles=@()})}
Complete-SetupProgress
if($preparedX86){Write-Output 'Next: start this game through NeuRotic or NeuRotic/Prepared/Start-PreparedGame.ps1 so the prepared guide route is enabled for that process. If compatible in-game integration is unavailable, use NR Anything.'}
else{Write-Output 'Next: launch the game and press Insert to open NeuRotic, unless you saved a different shortcut. Keep the recovery folder until testing is complete.'}
Write-Output "To remove NeuRotic, close the game and run 'Uninstall NeuRotic.cmd' in the game folder. Setup also retains the exact recovery record at: $backup"
}finally{Release-NeuroticTargetLocks $lockScopeStart}
