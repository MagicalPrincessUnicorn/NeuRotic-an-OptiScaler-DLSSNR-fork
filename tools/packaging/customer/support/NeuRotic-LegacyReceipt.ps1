# Bounded metadata import. Never opens previous/, preserved/ or backup bytes.
function ConvertFrom-NeuroticLegacyReceipt($State,[string]$GameRoot){
 if($State.kind -ne 'neurotic-public-install' -or $State.schema_version -ne 2){throw 'Unrecognized legacy metadata'}
 if($State.status -eq 'uninstalled'){return [pscustomobject]@{receipt=$null;notes=@('Completed legacy uninstall records remain inert.');imported=$false}}
 $files=@();$notes=@();$records=@();$rename=$null
 $proxy=ConvertTo-NeuroticRelativePath ([string]$State.selected_proxy)
 if($proxy -notin @('dxgi.dll','winmm.dll','version.dll','dbghelp.dll','d3d12.dll','wininet.dll','winhttp.dll','OptiScaler.asi','OptiScaler.dll')){throw 'Unrecognized legacy proxy'}
 # Prefer embedded metadata. A missing chain is not an install prerequisite.
 foreach($entry in @($State.restore_record_ledger)|Select-Object -Last 32){
  try{if($entry.record_json -isnot [string] -or $entry.record_json.Length -gt 1048576){throw 'Oversize record'};$records+=,($entry.record_json|ConvertFrom-Json)}catch{$notes+=,'An invalid legacy metadata entry was left inert.'}
 }
 if(-not $records.Count){
  foreach($entry in @($State.restore_chain)|Select-Object -Last 32){
   try{
    $relative=[string]$entry
    if([IO.Path]::IsPathRooted($relative)){
     $origin=[IO.Path]::GetFullPath([string]$State.game_directory).TrimEnd('\','/')+'\'
     $absolute=[IO.Path]::GetFullPath($relative);if(-not $absolute.StartsWith($origin,[StringComparison]::OrdinalIgnoreCase)){throw 'Record outside original game'};$relative=$absolute.Substring($origin.Length)
    }
    $relative=ConvertTo-NeuroticRelativePath $relative
    if($relative -notmatch '^(?i:NeuRotic-(?:test-)?backups)/[^/]+/INSTALL-MANIFEST\.json$'){throw 'Unexpected legacy metadata path'}
    $path=Resolve-NeuroticOwnedPath $GameRoot $relative
    if([IO.File]::Exists($path)){
     if((Get-Item -LiteralPath $path).Length -gt 1048576){throw 'Oversize record'}
     $records+=,(Get-Content -LiteralPath $path -Raw|ConvertFrom-Json)
    }
   }catch{$notes+=,'Legacy history metadata was unavailable; backup folders were left inert.'}
  }
 }
 foreach($record in $records){
  if($record.kind -ne 'neurotic-customer-candidate-install'){continue}
  foreach($entry in @($record.files)|Select-Object -First 10000){
   try{
    $path=ConvertTo-NeuroticRelativePath ([string]$entry.path)
    if($entry.ownership_unproven -or $entry.operation -eq 'rename-existing-proxy' -or $entry.installed_exists -ceq $false){continue}
    if($path -match '^(?i:NeuRotic-(?:test-)?backups)/|^(?i:NeuRotic/UserData)/|^(?i:OptiScaler/CharacterInspector)/'){continue}
    if($entry.operation -notin @('copy-package','copy-user-dependency','copy-runtime-dependency','preserve-existing','reuse-saved-settings','write-loadreshade-true','write-prepared-reshade-routing','write-generated')){continue}
    [void](Resolve-NeuroticOwnedPath $GameRoot $path)
    $files+=,[pscustomobject]@{path=$path;removeOnUninstall=$true;role=$(if($path -in @('dxgi.dll','winmm.dll','version.dll','dbghelp.dll','d3d12.dll','wininet.dll','winhttp.dll','OptiScaler.asi','OptiScaler.dll')){'proxy'}else{'legacy'})}
   }catch{$notes+=,'Unsafe legacy ownership path was ignored.'}
  }
  if($record.reshade_operation.original_name -ieq 'dxgi.dll' -and $record.reshade_operation.new_name -ieq 'ReShade64.dll'){$rename=[pscustomobject]@{from='dxgi.dll';to='ReShade64.dll'}}
 }
 # Current metadata establishes the selected loader even without its history.
 $files+=,[pscustomobject]@{path=$proxy;removeOnUninstall=$true;role='proxy'}
 foreach($entry in @($State.manager_files)){
  try{$path=ConvertTo-NeuroticRelativePath ([string]$entry.path);if($path -notmatch '^(?i:NeuRotic/Installer)/[^/]+\.(?:ps1|cs|json)$'){continue};[void](Resolve-NeuroticOwnedPath $GameRoot $path);$files+=,[pscustomobject]@{path=$path;removeOnUninstall=$true;role='installer'}}catch{}
 }
 $exe=[IO.Path]::GetFileName([string]$State.game_executable);if(-not $exe){$exe='Unknown.exe'}
 $receipt=[pscustomobject]@{schemaVersion=3;kind='neurotic-game-install';status='Partial';selectedExecutable=$exe;architecture=$(if($State.target_bit_size -eq 32){'x86'}else{'x64'});packageId='legacy-import';proxy=$proxy;files=@(Merge-NeuroticOwnedFiles @() $files)}
 if($rename){$receipt|Add-Member -NotePropertyName reshadeRename -NotePropertyValue $rename}
 return [pscustomobject]@{receipt=(ConvertTo-NeuroticInstallReceipt $receipt);notes=@($notes)+@('Legacy ownership metadata imported. Historical backups, private data and old Inspector folders remain inert.');imported=$true}
}
function Get-NeuroticRecognizableLegacyReceipt([string]$GameRoot){
 $files=@();$notes=@()
 foreach($relative in @('dxgi.dll','winmm.dll','version.dll','dbghelp.dll','d3d12.dll','wininet.dll','winhttp.dll','OptiScaler.asi','OptiScaler.dll','NeuRotic/Prepared/NeuRotic.GpuHost/OptiScaler.dll')){
  try{$path=Resolve-NeuroticOwnedPath $GameRoot $relative;if(-not [IO.File]::Exists($path)){continue};if(Test-NeuroticLoader $path){$files+=,[pscustomobject]@{path=$relative;removeOnUninstall=$true;role='proxy'}}else{$notes+=,('Ambiguous file left alone: '+$relative)}}catch{$notes+=,('File could not be identified safely: '+$relative)}
 }
 $receipt=$null;if($files.Count){$receipt=[pscustomobject]@{schemaVersion=3;kind='neurotic-game-install';status='Partial';selectedExecutable='Unknown.exe';architecture='unknown';packageId='legacy-recognized';proxy=$files[0].path;files=$files}}
 return [pscustomobject]@{receipt=$receipt;notes=@($notes)}
}
