# Retain English for CMD and bind the App error to a stable catalog identity.
function New-NeuroticCodedError([string]$Code,[string]$Message){
 $error=[InvalidOperationException]::new($Message);$error.Data['NeuroticReasonCode']=$Code;return $error
}
# Selected-target metadata only. Never walk the game tree or infer identity from a filename.
function Get-NeuroticField($Object,[string]$Name,$Default=$null){
 if($Object -is [Collections.IDictionary]){if($Object.Contains($Name)){return $Object[$Name]};return $Default}
 if($null -ne $Object -and $Object.PSObject.Properties.Name -contains $Name){return $Object.$Name};return $Default
}
function Get-NeuroticExecutableArchitecture([string]$Executable){
 $stream=[IO.File]::OpenRead($Executable)
 try{
  $header=New-Object byte[] 64
  if($stream.Length -lt 128 -or $stream.Read($header,0,64) -ne 64 -or $header[0] -ne 77 -or $header[1] -ne 90){throw 'Select an actual game executable'}
  $pe=[BitConverter]::ToInt32($header,60)
  if($pe -lt 64 -or $pe -gt 16777216 -or $pe -gt $stream.Length-26){throw 'Invalid executable header'}
  $stream.Position=$pe;$header=New-Object byte[] 26
  if($stream.Read($header,0,26) -ne 26 -or [BitConverter]::ToUInt32($header,0) -ne 17744){throw 'Invalid executable signature'}
  $machine=[BitConverter]::ToUInt16($header,4);$size=[BitConverter]::ToUInt16($header,20);$magic=[BitConverter]::ToUInt16($header,24)
  if($size -gt 4096 -or $pe+24+$size -gt $stream.Length){throw 'Invalid executable optional header'}
  if($machine -eq 0x8664 -and $magic -eq 0x20b -and $size -ge 240){return 'x64'}
  if($machine -eq 0x14c -and $magic -eq 0x10b -and $size -ge 224){return 'x86'}
  return 'unknown'
 }finally{$stream.Dispose()}
}
function Get-NeuroticInstallProfile([string]$Executable,$Package,$GameIdentity){
 $architecture=Get-NeuroticExecutableArchitecture $Executable
 $limitations=@();$route='native-x64'
 if($architecture -ne 'x64' -or (Get-NeuroticField $Package 'architecture' 'x64') -ne 'x64'){
  $route='unsupported';$limitations+=('A compatible in-game integration is unavailable for this executable/package architecture. Use NR Anything.')
 }
 $catalog=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'NeuRotic-GameCatalog.json') -Raw|ConvertFrom-Json
 $row=$null;$store=Get-NeuroticField $GameIdentity 'store';$id=Get-NeuroticField $GameIdentity 'storeId'
 if($store -and $id){foreach($game in $catalog.games){foreach($identity in $game.ids){if($identity.store -ieq $store -and [string]$identity.id -ceq [string]$id){$row=$game;break}};if($row){break}}}
 if(-not $row){$title=Get-NeuroticField $GameIdentity 'title';if($title){foreach($game in $catalog.games){if($game.name -ieq $title -or @($game.aliases|Where-Object{$_ -ieq $title}).Count){$row=$game;break}}}}
 $proxy='';$api='unknown';$profileId='native-default'
 if($row){
  $proxies=@($row.profiles|ForEach-Object{if($_.api -eq 'vulkan'){'winmm.dll'}else{$_.proxy}}|Sort-Object -Unique)
  if($proxies.Count -eq 1){$proxy=$proxies[0]}
  $apis=@($row.profiles.api|Sort-Object -Unique);if($apis.Count -eq 1){$api=$apis[0]}
  $profileId='catalog:'+ $row.name
 }
 return [pscustomobject]@{route=$route;architecture=$architecture;api=$api;proxy=$proxy;profileId=$profileId
  iniPatches=@([pscustomobject]@{path='OptiScaler.ini';section='Plugins';key='LoadReshade';value='false';policy='default'})
  configurationPaths=@('OptiScaler.ini');limitations=@($limitations)}
}
function Update-NeuroticProfileIni([string]$Text,$Patches,[bool]$NewConfiguration){
 foreach($patch in $Patches){
  if($patch.policy -notin @('default','routeRequired')){throw 'Unknown profile patch policy'}
  if($patch.policy -eq 'default' -and -not $NewConfiguration){continue}
  if($patch.section -match '[\r\n\[\]]' -or $patch.key -match '[\r\n=]' -or $patch.value -match '[\r\n]'){throw 'Invalid INI patch'}
  $newline=$(if($Text.Contains("`r`n")){"`r`n"}else{"`n"})
  $lines=New-Object 'Collections.Generic.List[string]';$lines.AddRange([string[]]($Text -split '\r?\n'))
  $section=-1;$end=$lines.Count;$key=-1
  for($i=0;$i -lt $lines.Count;$i++){
   if($lines[$i] -match '^\s*\[([^]]+)\]'){
    if($section -ge 0){$end=$i;break}
    if($Matches[1] -ieq $patch.section){$section=$i}
   }elseif($section -ge 0 -and $lines[$i] -match ('^\s*'+[regex]::Escape($patch.key)+'\s*=')){$key=$i}
  }
  if($key -ge 0){
   $pattern='^(?<prefix>\s*'+[regex]::Escape($patch.key)+'\s*=\s*)(?<value>[^;\r\n]*?)(?<suffix>\s*(?:;.*)?)$'
   $match=[regex]::Match($lines[$key],$pattern,[Text.RegularExpressions.RegexOptions]::IgnoreCase)
   $lines[$key]=$match.Groups['prefix'].Value+$patch.value+$match.Groups['suffix'].Value
  }
  elseif($section -ge 0){$lines.Insert($end,$patch.key+'='+$patch.value)}
  else{$lines.Add('['+$patch.section+']');$lines.Add($patch.key+'='+$patch.value)}
  $Text=$lines -join $newline
 }
 return $Text
}

function Get-NeuroticRenderingRequirements {
 $path=Join-Path $PSScriptRoot 'NeuRotic-RenderingRequirements.json'
 if((Get-Item -LiteralPath $path).Length -gt 16384){throw (New-NeuroticCodedError 'desktop.automatic_setup.requirements_limit' 'Rendering requirements exceed the limit')}
 $requirements=Get-Content -LiteralPath $path -Raw|ConvertFrom-Json
 if($requirements.schemaVersion -ne 1 -or $requirements.architecture -ne 'x64' -or -not $requirements.runtimeValidationRequired){throw (New-NeuroticCodedError 'desktop.automatic_setup.requirements_unsupported' 'Unsupported rendering requirements')}
 return $requirements
}
function Get-NeuroticAutomaticSetup($Profile,$Actions){
 $requirements=Get-NeuroticRenderingRequirements
 $files=@($Actions|Where-Object{$_.operation -eq 'Copy'}|ForEach-Object{$_.path})
 $missing=@($requirements.requiredRuntimeFiles|Where-Object{$files -notcontains $_})
 $state=$(if($Profile.route -ne 'native-x64'){'Unsupported'}elseif($missing -contains 'nvngx_dlssnr.dll'){'NeedsModel'}elseif($missing.Count){'MissingRuntime'}else{'ReadyForRuntime'})
 $reason=$(switch($state){
  'Unsupported'{'A compatible in-game integration is unavailable. Use NR Anything.'}
  'NeedsModel'{'Neural Rendering needs a compatible nvngx_dlssnr.dll. Add it to Runtime Files; game settings cannot replace this file.'}
  'MissingRuntime'{'The package is missing a required Neural Rendering runtime file.'}
  default{'Automatic Neural Rendering will be prepared after delivery. The game must validate inputs, device support and output safety when it runs.'}
 })
 $reasonCode=$(switch($state){
  'Unsupported'{'desktop.planintent.a_compatible_in_game_integration_is_unavailable__5a817f1b'}
  'NeedsModel'{'desktop.automatic_setup.needsmodel'}
  'MissingRuntime'{'desktop.automatic_setup.missingruntime'}
  default{'desktop.automatic_setup.default'}
 })
 return [pscustomobject]@{schemaVersion=1;state=$state;runtime='NotAttempted';reason=$reason;reasonCode=$reasonCode;missingFiles=$missing;requirements=$requirements}
}
function Get-NeuroticAutomaticSetupPatches($Requirements,[bool]$Enabled=$false){
 $d=$Requirements.defaults
 return @(
  [pscustomobject]@{section='DlssNr';key='Enabled';value=$Enabled.ToString().ToLowerInvariant();policy='default'},
  [pscustomobject]@{section='DlssNr';key='Route';value=[string]$d.route;policy='default'},
  [pscustomobject]@{section='DlssNr';key='Style';value=[string]$d.style;policy='default'},
  [pscustomobject]@{section='DlssNr';key='InputSource';value=[string]$d.inputSource;policy='default'},
  [pscustomobject]@{section='DlssNr';key='InputTransport';value=[string]$d.inputTransport;policy='default'},
  [pscustomobject]@{section='DlssNr';key='PresentInputPolicy';value=[string]$d.presentInputPolicy;policy='default'},
  [pscustomobject]@{section='DlssNr';key='AllowCpuFallback';value=$d.allowCpuFallback.ToString().ToLowerInvariant();policy='default'}
 )
}
