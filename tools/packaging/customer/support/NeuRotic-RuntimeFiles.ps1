function Get-NeuroticStreamlineCatalog {
 $catalog= @'
[
  {
    "files": [
      {
        "path": "sl.interposer.dll",
        "sha256": "27b2190057994c0b287c2c5716953bf1586f6499ac12fbbb2092b9aaf8396570",
        "bytes": 651392
      },
      {
        "path": "sl.common.dll",
        "sha256": "a4b2b5acbe49fbc6d44dd432cac19cd53218f698b2539dc7ed0fb268c72cfc8d",
        "bytes": 830592
      },
      {
        "path": "sl.dlss_g.dll",
        "sha256": "b8b5effd7debdb750abd216de43385fb653261712bc315d85eba68811fb3ee02",
        "bytes": 625792
      },
      {
        "path": "sl.reflex.dll",
        "sha256": "ecf12973cdcec2ffced2ea77b1c7e45f4d387e7c864ddb5531b66a6f947effb3",
        "bytes": 382080
      },
      {
        "path": "sl.pcl.dll",
        "sha256": "12aa4e76c28a27c735e4ecb3072f44d09428acb107b70ac38e4bd48ddb05f88d",
        "bytes": 360064
      },
      {
        "path": "nvngx_dlssg.dll",
        "sha256": "5d5cbf14d2727d47f93fd10bf77bd91708ae122482a6f86fd564971641ebd47b",
        "bytes": 7453808
      }
    ],
    "id": "canonical-20260926",
    "authority": "OptiScaler/dlssnr/CanonicalProviderPolicy.h",
    "architecture": 64
  },
  {
    "files": [
      {
        "path": "sl.interposer.dll",
        "sha256": "8c87c9499461da561edd529aa9bf7831d67d7b94ebb1c1a5ed54ef4934e1ea4c",
        "bytes": 652928
      },
      {
        "path": "sl.common.dll",
        "sha256": "82924a8954dd671e09351c5de0eb87ad0eb25b944cc9f9ab955ca1d9950de15d",
        "bytes": 843392
      },
      {
        "path": "sl.dlss_g.dll",
        "sha256": "f4a6b2b14dcc0b1485989e430d3b4e3a44ac1800b92ba1ad74f476e64fb2b09c",
        "bytes": 636032
      },
      {
        "path": "sl.reflex.dll",
        "sha256": "0ce9725e3e03ea9e7f81d008b57f33ee365973d2e349131c8b1c3e3378fe2db0",
        "bytes": 388736
      },
      {
        "path": "sl.pcl.dll",
        "sha256": "f13d51cfa05f4cd514df2026049e2db8adf359221713170ad386fd499915b582",
        "bytes": 360064
      },
      {
        "path": "nvngx_dlssg.dll",
        "sha256": "ff6e90eb78b827927dff5b4ecc6b1c870c2e9bca29ed9f48c7d348cc9e170b82",
        "bytes": 7460976
      }
    ],
    "id": "nvidia-sdk-2.14.1-release",
    "authority": "preserved official NVIDIA SDK 2.14.1 manifest",
    "architecture": 64
  }
]
'@ | ConvertFrom-Json
 foreach($entry in $catalog){$entry}
}
# One persistent, user-owned source for App, Anything and manual setup checks.
# Private NR bytes are never part of an application release or package manifest.
function Assert-NeuroticRuntimePath([string]$Path) {
 $full=[IO.Path]::GetFullPath($Path);$cursor=$full
 while($cursor){
  if(Test-Path -LiteralPath $cursor){$item=Get-Item -LiteralPath $cursor -Force -ErrorAction Stop;if($item.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked runtime paths are not supported'}}
  $parent=Split-Path -Parent $cursor;if($parent -eq $cursor){break};$cursor=$parent
 }
 return $full
}
function Get-NeuroticRuntimeRoot {
 $user=Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'NeuRotic/Hub'
 if($env:NEUROTIC_HUB_FIXTURE_ROOT){$user=$env:NEUROTIC_HUB_FIXTURE_ROOT}
 return Assert-NeuroticRuntimePath (Join-Path $user 'Runtime')
}
function Get-NeuroticModelFolder {
 $folder=Assert-NeuroticRuntimePath (Join-Path (Get-NeuroticRuntimeRoot) 'Place NVNGX DLSS NR File Here')
 [void](New-Item -ItemType Directory -Force -Path $folder)
 [void](Assert-NeuroticRuntimePath $folder)
 $readme=Assert-NeuroticRuntimePath (Join-Path $folder 'README.txt')
 if(-not (Test-Path -LiteralPath $readme)){
  $file=[IO.File]::Open($readme,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
  try{$bytes=[Text.Encoding]::UTF8.GetBytes("Place your compatible nvngx_dlssnr.dll directly in this folder.`r`nNeuRotic verifies it automatically before Neural Rendering can start.`r`nKeep the file here; nested folders and linked paths are not supported.`r`nNeuRotic does not distribute this private model file.`r`n");$file.Write($bytes,0,$bytes.Length);$file.Flush()}finally{$file.Dispose()}
 }elseif(-not [IO.File]::Exists($readme)){throw 'Model folder instructions must be an ordinary file'}
 return $folder
}
function Save-NeuroticModelPreference([string]$Preference,[string]$ModelPath) {
 $temp=Assert-NeuroticRuntimePath (Join-Path (Split-Path -Parent $Preference) ([Guid]::NewGuid().ToString('N')+'.pending'))
 try{
  [IO.File]::WriteAllText($temp,(@{schemaVersion=1;modelPath=$ModelPath}|ConvertTo-Json),(New-Object Text.UTF8Encoding($false)))
  [void](Assert-NeuroticRuntimePath $Preference)
  if([IO.File]::Exists($Preference)){[IO.File]::Replace($temp,$Preference,[NullString]::Value)}else{[IO.File]::Move($temp,$Preference)}
 }finally{if([IO.File]::Exists($temp)){[IO.File]::Delete($temp)}}
}
function Find-NeuroticModelWorker([string]$AppRoot) {
 $root=[IO.Path]::GetFullPath($AppRoot)
 for($level=0;$level -lt 4;$level++){
  foreach($relative in @('Tools/WindowWorker/NeuRotic.WindowWorker.exe','window-worker/NeuRotic.WindowWorker.exe')){
   $candidate=Assert-NeuroticRuntimePath (Join-Path $root $relative)
   if([IO.File]::Exists($candidate)){return $candidate}
  }
  $parent=Split-Path -Parent $root;if(-not $parent -or $parent -eq $root){break};$root=$parent
 }
 return $null
}
function Invoke-NeuroticModelCheck([string]$Worker,[string]$Path,[string]$ImportRoot=$null) {
 if(-not $Worker){return [pscustomobject]@{ready=$false;path=$Path;reason='Model verification needs the complete NeuRotic application. Open NeuRotic and choose your NR file.'}}
 [void](Assert-NeuroticRuntimePath $Path)
 $quote={param($text) '"'+([regex]::Replace([regex]::Replace($text,'(\\*)"','$1$1\"'),'(\\+)$','$1$1'))+'"'}
 $start=New-Object Diagnostics.ProcessStartInfo;$start.FileName=$Worker;$start.UseShellExecute=$false;$start.CreateNoWindow=$true
 $start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
 $start.Arguments=if($ImportRoot){'--import-model '+(& $quote $Path)+' --models-root '+(& $quote $ImportRoot)}else{'--verify-model '+(& $quote $Path)}
 $child=[Diagnostics.Process]::Start($start)
 try {
  $output=$child.StandardOutput.ReadToEndAsync();$errors=$child.StandardError.ReadToEndAsync()
  if(-not $child.WaitForExit(30000)){$child.Kill();$child.WaitForExit();throw 'Model verification timed out; choose the file again.'}
  $event=$output.Result.Trim()|ConvertFrom-Json
  if($event.event -ne 'model' -or $event.model.ready -isnot [bool]){throw 'Model verification returned an invalid result'}
  if($child.ExitCode -ne 0 -and $event.model.ready){throw 'Model verification did not complete successfully'}
  return $event.model
 } finally {$child.Dispose()}
}
function Get-NeuroticRuntimeProjection([string]$AppRoot,[string]$GameRoot,[bool]$Prepared) {
 $script:RuntimeDeploymentWarnings=@()
 $components=Get-NeuroticRuntimeComponents $AppRoot
 $prefix=if($Prepared){'NeuRotic\Prepared\NeuRotic.GpuHost\'}else{''}
 $configuration=Join-Path $GameRoot ($prefix+'OptiScaler.ini')
 if([IO.File]::Exists($configuration)){
  $section=''
  foreach($line in [IO.File]::ReadAllLines($configuration)){
   if($line -match '^\s*\[([^]]+)\]'){$section=$Matches[1]}
   elseif($section -ieq 'Libraries' -and $line -match '^\s*OptiDllPath\s*=\s*([^;#]+)'){
    $value=$Matches[1].Trim()
    if($value -and $value -ine 'auto' -and $value -ine 'OptiScaler'){
     $reason='Automatic runtime deployment was skipped because this game uses custom OptiDllPath '+$value+'. Keep its compatible runtime at that configured location; setup does not write outside the selected game.'
     $script:RuntimeDeploymentWarnings+=,$reason;Write-Warning $reason
     return
    }
   }
  }
 }
 $sources=@()
 if($components.streamline.status -eq 'Present'){
  foreach($file in $components.streamline.files){$sources += [pscustomobject]@{path=($prefix+'OptiScaler\streamline\'+$file.path);source=$file.source;sha256=$file.sha256;generation=$components.streamline.generation}}
 }
 if($components.neuralModel.status -eq 'Present'){
  $sources += [pscustomobject]@{path=($prefix+'nvngx_dlssnr.dll');source=$components.neuralModel.path;sha256=$components.neuralModel.sha256;generation='canonical-neural-model'}
 }
 foreach($file in $sources){
  $target=Join-Path $GameRoot $file.path;$exists=[IO.File]::Exists($target)
  [pscustomobject]@{path=$file.path;source=$file.source;operation='copy-user-dependency';component_generation=$file.generation;existed=$exists;previous_hash=$(if($exists){(Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash}else{$null});installed_hash=$file.sha256}
 }
}
function Get-NeuroticRuntimeComponents([string]$AppRoot) {
 Initialize-NeuroticRuntimeStat
 $root=Get-NeuroticRuntimeRoot;[void](New-Item -ItemType Directory -Force -Path $root)
 $stream=Assert-NeuroticRuntimePath (Join-Path $root 'streamline');[void](New-Item -ItemType Directory -Force -Path $stream)
 $names=@('sl.interposer.dll','sl.common.dll','sl.dlss_g.dll','sl.reflex.dll','sl.pcl.dll','nvngx_dlssg.dll');$present=@();$missing=@();$problem=$null
 foreach($name in $names){try{
  $target=Assert-NeuroticRuntimePath (Join-Path $stream $name)
  # Never replace persistent user content with a release-local copy.
  if(-not (Test-Path -LiteralPath $target)){$legacy=Assert-NeuroticRuntimePath (Join-Path $AppRoot ('streamline/'+$name));if([IO.File]::Exists($legacy)){[IO.File]::Copy($legacy,$target,$false)}}
  if([IO.File]::Exists($target)){$present+=,$name}else{$missing+=,$name}
 }catch{$missing+=,$name;$problem=$_.Exception.Message}}
 $generation=$null;$verifiedFiles=@()
 if(-not $missing.Count){
  try{
   $observed=@{}
   foreach($name in $names){
    $path=Assert-NeuroticRuntimePath (Join-Path $stream $name);$input=[IO.File]::Open($path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
    try{
     $reader=New-Object IO.BinaryReader($input)
     if($input.Length -lt 256 -or $reader.ReadUInt16() -ne 0x5a4d){throw ('Invalid Streamline PE image: '+$name)}
     $input.Position=0x3c;$offset=$reader.ReadUInt32();if($offset -gt $input.Length-24){throw 'Invalid PE header offset'}
     $input.Position=$offset;if($reader.ReadUInt32() -ne 0x4550 -or $reader.ReadUInt16() -ne 0x8664){throw ('Streamline requires an x64 image: '+$name)}
     $input.Position=0;$sha=[Security.Cryptography.SHA256]::Create();try{$hash=([BitConverter]::ToString($sha.ComputeHash($input))).Replace('-','').ToLowerInvariant()}finally{$sha.Dispose()}
     $observed[$name]=[pscustomobject]@{path=$name;source=$path;bytes=$input.Length;sha256=$hash;fileIdentity=[NeuRoticSimpleFileIo]::Stat($path);lastWriteTicks=(Get-Item -LiteralPath $path).LastWriteTimeUtc.Ticks}
    }finally{$input.Dispose()}
   }
   foreach($cohort in @(Get-NeuroticStreamlineCatalog)){
    if(-not @($cohort.files|Where-Object{$observed[$_.path].sha256 -ne $_.sha256 -or $observed[$_.path].bytes -ne $_.bytes}).Count){$generation=$cohort.id;$verifiedFiles=@($names|ForEach-Object{$observed[$_]});break}
   }
   if(-not $generation){throw 'Streamline files do not match a supported complete pinned cohort. Keep one matching file set together.'}
  }catch{$problem=$_.Exception.Message}
 }
 $status=if($problem){'Unavailable'}elseif($generation){'Present'}elseif($present.Count){'Partial'}else{'Missing'}
 $streamline=[ordered]@{optional=$true;status=$status;path=$stream;present=$present;missing=$missing;reason=$problem;generation=$generation;files=$verifiedFiles}
 $modelPath=Join-Path $root 'Place NVNGX DLSS NR File Here/nvngx_dlssnr.dll';$modelStatus='Missing';$modelExists=$false;$reason=$null;$modelHash=$null;$modelAcceptance=$null
 try {
  $folder=Get-NeuroticModelFolder;$modelPath=Assert-NeuroticRuntimePath (Join-Path $folder 'nvngx_dlssnr.dll')
  # Presence is independent of admission; a rejected file is not missing.
  $modelExists=[IO.File]::Exists($modelPath)
  $preference=Assert-NeuroticRuntimePath (Join-Path $root 'anything.json');$worker=Find-NeuroticModelWorker $AppRoot
  $remembered=$null
  if([IO.File]::Exists($preference)){$remembered=[string](Get-Content -LiteralPath $preference -Raw|ConvertFrom-Json).modelPath}
  $checked=$null
  if(Test-Path -LiteralPath $modelPath){
   # A deposited file always wins. Rejecting it must not silently enable an old
   # remembered model or replace the user's new bytes with a legacy copy.
   $checked=Invoke-NeuroticModelCheck $worker $modelPath
  }elseif($remembered -ne $modelPath){
   $candidates=@()
   if($remembered){$candidates+=,$remembered}
   else {
    $old=Assert-NeuroticRuntimePath (Join-Path (Split-Path -Parent $root) 'Anything/anything.json')
    if([IO.File]::Exists($old)){$legacySaved=[string](Get-Content -LiteralPath $old -Raw|ConvertFrom-Json).modelPath;if($legacySaved){$candidates+=,$legacySaved}}
    $legacyFolder=Assert-NeuroticRuntimePath (Join-Path $root 'Models')
    $candidates+=,(Join-Path $legacyFolder 'nvngx_dlssnr.dll')
    if([IO.Directory]::Exists($legacyFolder)){foreach($child in Get-ChildItem -LiteralPath $legacyFolder -Directory -Force){[void](Assert-NeuroticRuntimePath $child.FullName);$candidates+=,(Join-Path $child.FullName 'nvngx_dlssnr.dll')}}
    $candidates+=,(Join-Path $AppRoot 'models/nvngx_dlssnr.dll')
   }
   foreach($candidate in $candidates){[void](Assert-NeuroticRuntimePath $candidate);if([IO.File]::Exists($candidate)){$checked=Invoke-NeuroticModelCheck $worker $candidate $folder;break}}
  }
  if($checked){
   $reason=$checked.reason
   if($checked.ready){
    if($checked.sha256 -notmatch '^[a-fA-F0-9]{64}$'){throw 'Model verifier returned no validated byte identity'}
    if(-not [string]::Equals([IO.Path]::GetFullPath($checked.path),$modelPath,[StringComparison]::OrdinalIgnoreCase)){throw 'Model verifier returned a path outside the deposit folder'}
    $modelStatus='Present';$modelExists=$true;$modelHash=[string]$checked.sha256
    # The verifier may import a legacy source. Bind the accepted target bytes
    # and stat under one read lease before persisting readiness.
    $input=[IO.File]::Open($modelPath,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
    try{
     $sha=[Security.Cryptography.SHA256]::Create();try{$actual=([BitConverter]::ToString($sha.ComputeHash($input))).Replace('-','')}finally{$sha.Dispose()}
     if($actual -ine $modelHash){throw 'Model changed during validation. Refresh runtime files.'}
     $modelAcceptance=[pscustomobject]@{fileIdentity=[NeuRoticSimpleFileIo]::Stat($modelPath);bytes=$input.Length;lastWriteTicks=(Get-Item -LiteralPath $modelPath).LastWriteTimeUtc.Ticks}
    }finally{$input.Dispose()}
    if($remembered -ne $modelPath){Save-NeuroticModelPreference $preference $modelPath}
   }else{$modelStatus='Unavailable'}
  }else{$reason='Place your compatible nvngx_dlssnr.dll directly in the model folder.'}
 }catch{$modelStatus='Unavailable';$reason=$_.Exception.Message}
 $components=[ordered]@{streamline=$streamline;neuralModel=[ordered]@{optional=$true;status=$modelStatus;path=$modelPath;sha256=$modelHash;present=$(if($modelExists){@('nvngx_dlssnr.dll')}else{@()});missing=$(if($modelExists){@()}else{@('nvngx_dlssnr.dll')});reason=$reason}}
 if($modelAcceptance){foreach($name in @('fileIdentity','bytes','lastWriteTicks')){$components.neuralModel[$name]=$modelAcceptance.$name}}
 Save-NeuroticRuntimeAcceptance $components
 return $components
}
function Initialize-NeuroticRuntimeStat {
 if(-not ('NeuRoticSimpleFileIo' -as [type])){Add-Type -Path (Join-Path $PSScriptRoot 'NeuRotic-SimpleFileIo.cs')}
}
function Add-NeuroticRuntimeAcceptanceStat($File,[string]$Path){
 $identity=[NeuRoticSimpleFileIo]::Stat((Assert-NeuroticRuntimePath $Path));if($identity -eq 'Absent'){throw 'Accepted runtime source is missing; refresh runtime files'}
 $prior=$(if($File -is [Collections.IDictionary]){$File['fileIdentity']}elseif($File.PSObject.Properties.Name -contains 'fileIdentity'){$File.fileIdentity}else{$null})
 if($prior -and $identity -ne $prior){throw 'Runtime source changed after acceptance. Refresh runtime files.'}
 $info=Get-Item -LiteralPath $Path -Force
 foreach($value in @(@('bytes',$info.Length),@('lastWriteTicks',$info.LastWriteTimeUtc.Ticks),@('fileIdentity',$identity))){
  if($File -is [Collections.IDictionary]){$File[$value[0]]=$value[1]}else{$File|Add-Member -NotePropertyName $value[0] -NotePropertyValue $value[1] -Force}
 }
}
function Save-NeuroticRuntimeAcceptance($Components){
 Initialize-NeuroticRuntimeStat
 if($Components.streamline.status -eq 'Present'){foreach($file in $Components.streamline.files){Add-NeuroticRuntimeAcceptanceStat $file $file.source}}
 if($Components.neuralModel.status -eq 'Present'){Add-NeuroticRuntimeAcceptanceStat $Components.neuralModel $Components.neuralModel.path}
 $path=Assert-NeuroticRuntimePath (Join-Path (Get-NeuroticRuntimeRoot) 'accepted-runtime.json')
 $temporary=Assert-NeuroticRuntimePath ($path+'.writing')
 try{
  [IO.File]::WriteAllText($temporary,([ordered]@{schemaVersion=1;components=$Components}|ConvertTo-Json -Depth 8),(New-Object Text.UTF8Encoding($false)))
  if([IO.File]::Exists($path)){[IO.File]::Replace($temporary,$path,[NullString]::Value)}else{[IO.File]::Move($temporary,$path)}
 }finally{if([IO.File]::Exists($temporary)){[IO.File]::Delete($temporary)}}
}
function Get-NeuroticAcceptedRuntimeComponents {
 Initialize-NeuroticRuntimeStat
 $empty=[pscustomobject]@{streamline=[pscustomobject]@{status='Missing';files=@();reason='Refresh runtime files.'};neuralModel=[pscustomobject]@{status='Missing';path='';reason='Refresh runtime files.'}}
 try{
  $path=Assert-NeuroticRuntimePath (Join-Path (Get-NeuroticRuntimeRoot) 'accepted-runtime.json')
  if(-not [IO.File]::Exists($path)){return $empty}
  if((Get-Item -LiteralPath $path).Length -gt 1MB){throw 'Runtime acceptance metadata is too large'}
  $cache=Get-Content -LiteralPath $path -Raw|ConvertFrom-Json;if($cache.schemaVersion -ne 1){return $empty}
  $components=$cache.components
  foreach($name in @('streamline','neuralModel')){
   $component=$components.$name;if($component.status -ne 'Present'){continue}
   try{
    $files=@(if($name -eq 'streamline'){$component.files}else{$component})
    if(-not $files.Count -or $files.Count -gt 16){throw 'Runtime acceptance has an invalid file list'}
    foreach($file in $files){$source=$(if($name -eq 'streamline'){$file.source}else{$file.path});$source=Assert-NeuroticRuntimePath $source
     if(-not $file.fileIdentity -or [NeuRoticSimpleFileIo]::Stat($source) -ne $file.fileIdentity){throw 'Runtime deposit changed. Refresh runtime files.'}
     $info=Get-Item -LiteralPath $source -Force;if($info.Length -ne $file.bytes -or $info.LastWriteTimeUtc.Ticks -ne $file.lastWriteTicks){throw 'Runtime deposit changed. Refresh runtime files.'}
    }
   }catch{$component.status='Unavailable';$component|Add-Member -NotePropertyName reason -NotePropertyValue 'Runtime deposit changed or needs acceptance. Refresh runtime files.' -Force}
  }
  return $components
 }catch{return $empty}
}
