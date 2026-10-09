# Offline warning evidence only. Research metadata never authorizes execution or bypass.
function Assert-HubAntiCheatPath([string]$Path){
 $item=Get-Item -LiteralPath ([IO.Path]::GetFullPath($Path)) -Force -ErrorAction Stop
 while($item){if($item.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked anti-cheat evidence path refused'};$item=if($item -is [IO.DirectoryInfo]){$item.Parent}else{$item.Directory}}
}
function Initialize-HubAntiCheatCollector {
 if(-not ('NeuRoticAntiCheatCollector' -as [type])){Add-Type -Path (Join-Path $PSScriptRoot 'NeuRotic-AntiCheatCollector.cs') -ReferencedAssemblies System.Web.Extensions}
}
function Read-HubAntiCheatBytes([string]$Path,[int]$Limit){Initialize-HubAntiCheatCollector;return ,[Convert]::FromBase64String([NeuRoticAntiCheatCollector]::ReadBounded($Path,$Limit))}
function Read-HubAntiCheatDataset([string]$RulesPath=(Join-Path $PSScriptRoot 'neurotic_anticheat_rules.json'),[string]$NamesPath=(Join-Path $PSScriptRoot 'exe_names.txt')){
 $result=[ordered]@{status='Unavailable';id=$null;reviewDate=$null;sha256=$null;namesSha256=$null;reason=$null;games=@()}
 try{
  if(-not ('NeuRoticHubJson' -as [type])){Add-Type -Path (Join-Path $PSScriptRoot 'NeuRotic-HubJson.cs')}
  $bytes=Read-HubAntiCheatBytes $RulesPath 1048576;$result.sha256=HashBytes $bytes
  $namesBytes=Read-HubAntiCheatBytes $NamesPath 65536;$result.namesSha256=HashBytes $namesBytes
  $utf8=New-Object Text.UTF8Encoding($false,$true);$text=$utf8.GetString($bytes);[NeuRoticHubJson]::Validate($text);$data=$text|ConvertFrom-Json
  if(($data.schema_version -isnot [int] -and $data.schema_version -isnot [long]) -or $data.schema_version -ne 1 -or $data.platform -cne 'Windows' -or $data.dataset_id -isnot [string] -or $data.dataset_id.Length -gt 128 -or $data.review_date -notmatch '^\d{4}-\d{2}-\d{2}$' -or $data.policy.action -cne 'warn_allow_explicit_override' -or $data.policy.no_match_means -cne 'unknown_not_safe'){throw 'Unsupported anti-cheat dataset identity or policy'}
  if($data.games -isnot [Array] -or -not $data.games.Count -or $data.games.Count -gt 512){throw 'Invalid anti-cheat game records'}
  foreach($key in @('game_family_records','game_alias_records','unique_basename_only_warning_aliases','unique_context_required_aliases','steam_app_identities')){if($data.counts.$key -isnot [int] -and $data.counts.$key -isnot [long]){throw 'Dataset counts must be integers'}}
  $ids=New-Object 'Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
  $exact=New-Object 'Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
  $context=New-Object 'Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
  $aliasCount=0;$identityCount=0
  foreach($game in $data.games){
   if($game.id -isnot [string] -or $game.id -notmatch '^[a-z0-9-]{1,128}$' -or -not $ids.Add($game.id) -or $game.title -isnot [string] -or -not $game.title.Length -or $game.title.Length -gt 256 -or $game.title -match '[\x00-\x1f]' -or $game.identity_match_enabled -isnot [bool] -or $game.runtime_verified -isnot [bool] -or $game.runtime_verified -or $game.action -cne 'warn_allow_override'){throw 'Invalid anti-cheat game identity or claim'}
   if($game.steam_app_ids -isnot [Array] -or $game.steam_app_ids.Count -gt 16 -or $game.executables -isnot [Array] -or $game.executables.Count -gt 64 -or $game.anti_cheat_reported -isnot [Array] -or $game.anti_cheat_reported.Count -gt 16){throw 'Invalid anti-cheat game arrays'}
   foreach($app in $game.steam_app_ids){if($app -isnot [string] -or $app -notmatch '^[1-9][0-9]{0,9}$'){throw 'Invalid anti-cheat Steam identity'};$identityCount++}
   foreach($provider in $game.anti_cheat_reported){if($provider -isnot [string] -or $provider.Length -gt 256 -or $provider -match '[\x00-\x1f]'){throw 'Invalid reported provider metadata'}}
   foreach($alias in $game.executables){
    $aliasCount++
    if($alias.basename -isnot [string] -or $alias.basename.Length -gt 256 -or $alias.basename -match '[\x00-\x1f\\/:*?"<>|]' -or $alias.basename -notmatch '(?i)\.exe$' -or $alias.active_warning_rule -isnot [bool] -or $alias.match_mode -cnotin @('exact_basename','requires_game_identity')){throw 'Invalid anti-cheat executable warning rule'}
    if($alias.active_warning_rule){if($alias.match_mode -ceq 'exact_basename'){[void]$exact.Add($alias.basename)}else{[void]$context.Add($alias.basename)}}
   }
  }
  $flat=New-Object 'Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
  foreach($name in @($utf8.GetString($namesBytes) -split '\r?\n' | Where-Object {$_ -ne ''})){if($name.Length -gt 256 -or $name -match '[\\/:*?"<>|]' -or -not $exact.Contains($name) -or -not $flat.Add($name)){throw 'Flat executable reference differs from structured exact rules'}}
  if($flat.Count -ne $exact.Count -or $data.counts.game_family_records -ne $data.games.Count -or $data.counts.game_alias_records -ne $aliasCount -or $data.counts.unique_basename_only_warning_aliases -ne $exact.Count -or $data.counts.unique_context_required_aliases -ne $context.Count -or $data.counts.steam_app_identities -ne $identityCount){throw 'Anti-cheat dataset counts or flat reference are inconsistent'}
  $result.status='Available';$result.id=$data.dataset_id;$result.reviewDate=$data.review_date;$result.games=@($data.games)
 }catch{$result.reason=$_.Exception.Message}
 return [pscustomobject]$result
}
function Get-HubAntiCheatIdentity($Request,$Target){
 $result=[ordered]@{status='NotProvided';appId=$null;manifest=$null;manifestSha256=$null;gameRoot=$null;reason=$null}
 $app=Get-HubProperty $Request 'steamAppId';$steam=Get-HubProperty $Request 'steamRoot';$root=Get-HubProperty $Request 'gameRoot'
 # An unverified gameRoot hint alone does not establish platform identity or widen scanning.
 if(-not $app -and -not $steam){return [pscustomobject]$result}
 $result.status='Unverified'
 try{
  if(-not $app -or $app -notmatch '^[1-9][0-9]{0,9}$'){throw 'Steam app identity requires an independently verified owning manifest'}
  $steamapps=$null
  if($root){
   $rootPath=[IO.Path]::GetFullPath($root).TrimEnd('\');$common=Split-Path -Parent $rootPath;$apps=Split-Path -Parent $common
   if([IO.Path]::GetFileName($common) -ieq 'common' -and [IO.Path]::GetFileName($apps) -ieq 'steamapps'){Assert-HubAntiCheatPath $rootPath;$steamapps=$apps}
  }
  if(-not $steamapps){
   if(-not $steam){throw 'Steam app identity requires an independently verified owning manifest'}
   $steamPath=[IO.Path]::GetFullPath($steam).TrimEnd('\');Assert-HubAntiCheatPath $steamPath
   $steamapps=if([IO.Path]::GetFileName($steamPath) -ieq 'steamapps'){$steamPath}else{Join-Path $steamPath 'steamapps'}
  }
  $manifest=Join-Path $steamapps ('appmanifest_'+$app+'.acf');$bytes=Read-HubAntiCheatBytes $manifest 65536
  $result.manifest=$manifest;$result.manifestSha256=HashBytes $bytes
  $identity=[NeuRoticHubJson]::ReadSteamIdentity((New-Object Text.UTF8Encoding($false,$true)).GetString($bytes))
  if(-not [StringComparer]::Ordinal.Equals($identity['appid'],$app) -or $identity['installdir'] -match '[\x00-\x1f\\/:*?"<>|]|[. ]$' -or $identity['installdir'] -in @('','.','..')){throw 'Steam manifest app ID or installation directory is invalid'}
  $owned=[IO.Path]::GetFullPath((Join-Path (Join-Path $steamapps 'common') $identity['installdir'])).TrimEnd('\');Assert-HubAntiCheatPath $owned
  if(-not $Target.executable.StartsWith($owned+'\',[StringComparison]::OrdinalIgnoreCase) -or ($root -and [IO.Path]::GetFullPath($root).TrimEnd('\') -ine $owned)){throw 'Steam manifest does not own the selected executable and game root'}
  $result.status='Verified';$result.appId=$app;$result.gameRoot=$owned
 }catch{$result.reason=$_.Exception.Message}
 return [pscustomobject]$result
}
. (Join-Path $PSScriptRoot 'NeuRotic-AntiCheatPolicy.ps1')
function Read-HubProviderRules {
 try{
  $bytes=Read-HubAntiCheatBytes (Join-Path $PSScriptRoot 'NeuRotic-AntiCheatProviders.json') 131072
  if((HashBytes $bytes).ToLowerInvariant() -cne '8f66be037644936b2ee51258bdccdc032c8b4a3a2358d8655f6d0b3018c5261c'){throw 'Bundled provider rules digest mismatch'}
  if(-not ('NeuRoticHubJson' -as [type])){Add-Type -Path (Join-Path $PSScriptRoot 'NeuRotic-HubJson.cs')}
  $text=(New-Object Text.UTF8Encoding($false,$true)).GetString($bytes);[NeuRoticHubJson]::Validate($text);$rules=$text|ConvertFrom-Json;Assert-AcRules $rules;return $rules
 }catch{return $null}
}
function Get-HubAntiCheat($Request,$Target){
 $rules=Read-HubProviderRules;$data=Read-HubAntiCheatDataset;$identity=Get-HubAntiCheatIdentity $Request $Target
 $root=if($identity.status -eq 'Verified'){$identity.gameRoot}else{$Target.directory}
 $names=@();if($rules){foreach($s in $rules.file_signals){$names+=@($s.basenames)}}
 Initialize-HubAntiCheatCollector
 $collected=[NeuRoticAntiCheatCollector]::Collect($root,$Target.executable,[string[]]$names)|ConvertFrom-Json
 $snapshot=$collected.snapshot
 $cancelled=$snapshot.scan_status -ceq 'cancelled'
 if($identity.status -ne 'Verified'){if(-not $cancelled){$snapshot.scan_status='incomplete'};$snapshot.issues+=,'trusted_install_root_unresolved'}
 if($data.status -ne 'Available'){if(-not $cancelled){$snapshot.scan_status='incomplete'};$snapshot.issues+=,'catalog_unavailable'}
 $games=@();$catalog=@();$mapping=@{'EA Javelin Anticheat'='ea_javelin';'EA AntiCheat'='ea_javelin';'BattlEye'='battleye';'Valve Anti-Cheat (VAC)'='vac';'VAC'='vac';'Valve Anti-Cheat'='vac';'Easy Anti-Cheat'='eac_eos';'Easy Anti-Cheat (EAC)'='eac_eos';'Riot Vanguard'='riot_vanguard';'Vanguard'='riot_vanguard'}
 # Curated associations require a locally re-read owning manifest. Executable aliases remain reference data only.
 if($identity.status -eq 'Verified'){
  foreach($g in $data.games){if(-not $g.identity_match_enabled -or $identity.appId -cnotin $g.steam_app_ids){continue};$games+=,[ordered]@{id=$g.id;title=$g.title;providers=@($g.anti_cheat_reported);association='curated_storefront_record';appId=$identity.appId}
   foreach($label in $g.anti_cheat_reported){if($mapping.ContainsKey($label)){$catalog+=,[pscustomobject]@{provider=$mapping[$label];source_id=$data.id;match='storefront_id';record_id=$g.id;app_id=$identity.appId}}}
  }
 }
 $snapshot.catalog=@($catalog)
 $report=Invoke-HubAntiCheatPolicy $rules $snapshot
 if(-not $cancelled -and $games.Count -and $report.game_state -eq 'not_found'){$report.game_state='documented';$report.headline='documented';$report.requires_ack=$true}
 # Cancellation is the operation outcome even if rule validation is unavailable.
 # Local/curated context may remain visible, but cannot request or grant approval.
 if($cancelled){$report.scan_status='cancelled';$report.headline='cancelled';$report.requires_ack=$false;$report.evidence_digest=Get-AcHash @{policy_evidence=$report.evidence_digest;scan_status='cancelled'}}
 if($games.Count){$report.evidence_digest=Get-AcHash @{policy_evidence=$report.evidence_digest;curated_game_associations=@($games)}}
 $reason=switch ($report.headline){
  'detected' {'Anti-cheat components detected in this installation. Component layout does not establish authenticated publisher identity or active protection. Installing NeuRotic may cause launch failures, removal from online sessions, or an account ban.'}
  'possible' {'Possible anti-cheat components found in this installation, including ambiguous or leftover files. This does not establish active protection. Installing NeuRotic may cause launch failures, removal from online sessions, or an account ban.'}
  'documented' {'Known anti-cheat association from the curated storefront catalog. This is a reported game association; local protection has not been established. Installing NeuRotic may cause launch failures, removal from online sessions, or an account ban.'}
  'cancelled' {'Anti-cheat scan cancelled. No changes have been made.'}
  'incomplete' {'Anti-cheat inspection could not complete. Some checks are unavailable. A negative or incomplete check cannot establish account safety or permission to use NeuRotic.'}
  default {'No recognized anti-cheat components found in this bounded check. This does not establish account safety or permission to use NeuRotic.'}
 }
 $status=if($cancelled){'Cancelled'}elseif($report.findings.Count -or $games.Count){'WarningCandidate'}else{'Unknown'}
 $result=[ordered]@{status=$status;reason=$reason;acknowledgementRequired=[bool]$report.requires_ack;games=@($games);evidence=@($report.findings|ForEach-Object {$_.evidence});dataset=[ordered]@{status=$data.status;id=$data.id;reviewDate=$data.reviewDate;sha256=$data.sha256;reason=$data.reason};scan=[ordered]@{root=$root;status=$(if($cancelled){'Cancelled'}elseif($report.scan_status -eq 'complete'){'Complete'}else{'Incomplete'});reason=($report.issues -join ', ');entries=$collected.counters.entries;counters=$collected.counters};identity=$identity;target=$Target.executable;targetSha256=$Target.sha256;operation=(Get-HubProperty $Request 'operation')}
 foreach($p in $report.PSObject.Properties){$result[$p.Name]=$p.Value}
 $result.fingerprint=Get-AcHash @{encoding='hub-ac-binding-v2';evidence=$report.evidence_digest;targetFileIdentity=(Get-HubProperty $Target 'fileIdentity' $Target.sha256);operation=$result.operation;identity=$identity;datasetDigest=$data.sha256;catalogGames=@($games)}
 return [pscustomobject]$result
}
function New-HubAntiCheatDecision($Target,$Info){return [ordered]@{status='NeedsDecision';decisionKind='AntiCheatRisk';target=$Target.executable;reason=$Info.reason;antiCheat=$Info}}
