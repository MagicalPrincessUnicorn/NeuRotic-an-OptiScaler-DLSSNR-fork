# Configuration functions only. The paired launcher is the sole caller.
Set-StrictMode -Version 2.0
function NrHash([string]$path){(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()}
function NrSafe([string]$root,[string]$relative){
    $root=[IO.Path]::GetFullPath($root).TrimEnd('\');$path=[IO.Path]::GetFullPath((Join-Path $root $relative))
    if(-not $path.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Test path leaves game directory'}
    $walk=$path;while($walk){if(Test-Path -LiteralPath $walk){if((Get-Item -LiteralPath $walk -Force).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked test path refused'}};$next=Split-Path -Parent $walk;if($next -eq $walk){break};$walk=$next};return $path
}
function NrIniGet([string]$text,[string]$section,[string]$key){
    $s=[regex]::Matches($text,'(?ims)^\['+[regex]::Escape($section)+'\][ \t]*\r?\n(?<body>.*?)(?=^\[|\z)')
    if($s.Count -gt 1){throw 'Duplicate configuration section'};if(-not $s.Count){return $null}
    $m=[regex]::Matches($s[0].Groups['body'].Value,'(?im)^[ \t]*'+[regex]::Escape($key)+'[ \t]*=[ \t]*([^\r\n]*)')
    if($m.Count -gt 1){throw 'Duplicate configuration key'};if(-not $m.Count){return $null};return $m[0].Groups[1].Value.Trim()
}
function NrIniSet([string]$text,[string]$section,[string]$key,$value){
    $null=NrIniGet $text $section $key
    $s=[regex]::Match($text,'(?ims)^\['+[regex]::Escape($section)+'\][ \t]*\r?\n(?<body>.*?)(?=^\[|\z)')
    if(-not $s.Success){if($null -eq $value){return $text};return $text.TrimEnd()+"`r`n`r`n[$section]`r`n$key=$value`r`n"}
    $body=$s.Groups['body'];$m=[regex]::Match($body.Value,'(?im)^[ \t]*'+[regex]::Escape($key)+'[ \t]*=[^\r\n]*(?:\r?\n|$)')
    $line=$(if($null -eq $value){''}else{"$key=$value`r`n"})
    $replacement=$(if($m.Success){$body.Value.Substring(0,$m.Index)+$line+$body.Value.Substring($m.Index+$m.Length)}else{$line+$body.Value})
    return $text.Substring(0,$body.Index)+$replacement+$text.Substring($body.Index+$body.Length)
}
function NrRead([string]$path){
    if(-not (Test-Path -LiteralPath $path)){return [pscustomobject]@{text='';bytes=[byte[]]@();encoding=[Text.UTF8Encoding]::new($false,$true);bom=[byte[]]@()}}
    $bytes=[IO.File]::ReadAllBytes($path);if($bytes.Length -gt 1MB){throw 'Configuration exceeds 1 MiB'}
    $offset=0;$encoding=[Text.UTF8Encoding]::new($false,$true)
    if($bytes.Length -ge 2 -and $bytes[0] -eq 255 -and $bytes[1] -eq 254){$encoding=[Text.UnicodeEncoding]::new($false,$true,$true);$offset=2}
    elseif($bytes.Length -ge 2 -and $bytes[0] -eq 254 -and $bytes[1] -eq 255){$encoding=[Text.UnicodeEncoding]::new($true,$true,$true);$offset=2}
    elseif($bytes.Length -ge 3 -and $bytes[0] -eq 239 -and $bytes[1] -eq 187 -and $bytes[2] -eq 191){$offset=3}
    $bom=$(if($offset){[byte[]]$bytes[0..($offset-1)]}else{[byte[]]@()})
    return [pscustomobject]@{text=$encoding.GetString($bytes,$offset,$bytes.Length-$offset);bytes=$bytes;encoding=$encoding;bom=$bom}
}
function NrWrite([string]$root,[string]$relative,[byte[]]$bytes,$expected){
    $path=NrSafe $root $relative;$exists=Test-Path -LiteralPath $path -PathType Leaf
    if($exists -ne ($null -ne $expected) -or ($exists -and (NrHash $path) -ne $expected)){throw 'Configuration changed before write'}
    $mode=$(if($exists){[IO.FileMode]::Open}else{[IO.FileMode]::CreateNew})
    $stream=[IO.File]::Open($path,$mode,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
    try{if($exists){$sha=[Security.Cryptography.SHA256]::Create();try{$actual=([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-','').ToLowerInvariant()}finally{$sha.Dispose()};if($actual -ne $expected){throw 'Configuration changed under exclusive write lock'}};$stream.Position=0;$stream.Write($bytes,0,$bytes.Length);$stream.SetLength($bytes.Length);$stream.Flush($true)}finally{$stream.Dispose()}
}
function NrProfileRestore([string]$game){
    $statePath=NrSafe $game 'NeuRotic\prepared-test-profile.json';if(-not(Test-Path -LiteralPath $statePath)){return}
    $state=Get-Content -LiteralPath $statePath -Raw -Encoding UTF8|ConvertFrom-Json
    if($state.schema -ne 1 -or $state.game -ine [IO.Path]::GetFullPath($game)){throw 'Profile recovery identity mismatch'}
    foreach($file in $state.files){
        if($file.path -cnotin @('OptiScaler.ini','ReShade.ini')){throw 'Unexpected profile recovery path'}
        $path=NrSafe $game $file.path;if(-not(Test-Path -LiteralPath $path -PathType Leaf)){if(-not $file.existed){continue};Write-Warning "Missing $($file.path); preserving recovery record";throw 'Missing configuration during recovery'}
        $current=NrRead $path;$hash=NrHash $path;$original=[Convert]::FromBase64String($file.original)
        if($hash -eq $file.beforeHash){continue} # Planned write never happened, or already restored.
        if($hash -eq $file.appliedHash){
            if($file.existed){NrWrite $game $file.path $original $hash}else{Remove-Item -LiteralPath $path}
        }else{
            # Restore only keys still equal to this test's values. Keep user edits.
            $text=$current.text;foreach($key in $file.keys){if((NrIniGet $text $key.section $key.key) -ceq $key.applied){$text=NrIniSet $text $key.section $key.key $key.original}}
            $bytes=[byte[]]($current.bom+$current.encoding.GetBytes($text));NrWrite $game $file.path $bytes $hash
        }
    }
    if($state.PSObject.Properties['preset']){
        if($state.preset -cnotmatch '^NeuRotic\\prepared-test-[a-f0-9]{32}\.ini$'){throw 'Unexpected temporary preset path'}
        $presetPath=NrSafe $game $state.preset;if(Test-Path -LiteralPath $presetPath -PathType Leaf){Remove-Item -LiteralPath $presetPath}
    }
    Remove-Item -LiteralPath $statePath
}
function NrProfileApply([string]$game,[string]$mode,[string]$provider,[string]$graphicsApi='Proxy',[string]$targetProcess=''){
    NrProfileRestore $game
    $asset=NrSafe $game 'OptiScaler\PreparedGuides'
    # ReShade scalar commas are escaped as doubled commas.
    $shader=($asset+'\Shaders').Replace(',',',,');$helper=(NrSafe $game 'NeuRotic.GpuHost\NeuRotic.PreparedGpuHost.exe').Replace(',',',,')
    $model=(NrSafe $game 'nvngx_dlssnr.dll').Replace(',',',,');$forwarder=(NrSafe $game 'NeuRotic.GpuHost\nvngx.dll_dlssnr.dll').Replace(',',',,')
    $prepared=$mode -ne 'Baseline';$template=$asset+'\'+$(if(-not $prepared){'Baseline.ini'}elseif($provider -eq 'BuiltIn'){'BuiltIn.ini'}else{'Prepared.ini'})
    $presetRelative='NeuRotic\prepared-test-'+[Guid]::NewGuid().ToString('N')+'.ini';$preset=(NrSafe $game $presetRelative).Replace(',',',,')
    $presetBytes=[IO.File]::ReadAllBytes($template)
    $definitions='NRPG_MV_PROVIDER='+$(if($provider -eq 'BuiltIn'){5}else{0})
    $changes=@(
        @('OptiScaler.ini','Plugins','LoadReShade',$(if($graphicsApi -eq 'Vulkan'){'false'}else{'true'})),@('OptiScaler.ini','DlssNr','Enabled',$(if($mode -eq 'TransportOnly'){'false'}else{'true'})),
        @('OptiScaler.ini','DlssNr','InputSource','3'),@('OptiScaler.ini','DlssNr','InputTransport','1'),@('OptiScaler.ini','DlssNr','AllowCpuFallback','false'),
        @('OptiScaler.ini','DlssNr','Route','2'),@('OptiScaler.ini','DlssNr','PresentInputPolicy',$(if($prepared){'1'}else{'0'})),
        @('OptiScaler.ini','Menu','OverlayMenu','true'),@('OptiScaler.ini','Log','LogToFile','true'),
        @('ReShade.ini','GENERAL','EffectSearchPaths',$shader),@('ReShade.ini','GENERAL','PresetPath',$preset),
        @('ReShade.ini','GENERAL','PreprocessorDefinitions',$definitions),@('ReShade.ini','GENERAL','PerformanceMode','0'),@('ReShade.ini','ADDON','AddonPath',$game.Replace(',',',,')),
        @('ReShade.ini','NeuRoticPreparedGuides','HelperExe',$helper),@('ReShade.ini','NeuRoticPreparedGuides','Model',$model),
        @('ReShade.ini','NeuRoticPreparedGuides','Forwarder',$forwarder),@('ReShade.ini','NeuRoticPreparedGuides','FlowBackend','software'),
        @('ReShade.ini','NeuRoticPreparedGuides','ProbeContent','1'),@('ReShade.ini','NeuRoticPreparedGuides','SharedGpu',$(if($mode -eq 'Shared' -or $mode -eq 'TransportOnly'){'1'}else{'0'})),
        @('ReShade.ini','NeuRoticPreparedGuides','TransportOnly',$(if($mode -eq 'TransportOnly'){'1'}else{'0'})))
    if($targetProcess){if([IO.Path]::GetFileName($targetProcess) -ne $targetProcess -or [IO.Path]::GetExtension($targetProcess) -ine '.exe'){throw 'Expected selected game executable filename'};$changes+=,@('OptiScaler.ini','ProcessFilter','TargetProcessName',$targetProcess)}
    # Preserve existing depth definitions and other preprocessor settings.
    $state=[ordered]@{schema=1;game=[IO.Path]::GetFullPath($game);preset=$presetRelative;files=@()}
    foreach($relative in @('OptiScaler.ini','ReShade.ini')){
        $path=NrSafe $game $relative;$document=NrRead $path;$text=$document.text;$keys=@()
        foreach($change in $changes){if($change[0] -ne $relative){continue};$before=NrIniGet $text $change[1] $change[2];$value=$change[3]
            if($relative -eq 'ReShade.ini' -and $change[2] -eq 'PreprocessorDefinitions' -and $before){$value=(@($before.Split(',')|Where-Object {$_ -notmatch '^\s*NRPG_MV_PROVIDER='})+@($value))-join ','}
            $keys+=@([ordered]@{section=$change[1];key=$change[2];original=$before;applied=$value});$text=NrIniSet $text $change[1] $change[2] $value
        }
        $bytes=[byte[]]($document.bom+$document.encoding.GetBytes($text));$hash=$(if(Test-Path -LiteralPath $path){NrHash $path}else{$null})
        $sha=[Security.Cryptography.SHA256]::Create();try{$applied=([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-','').ToLowerInvariant()}finally{$sha.Dispose()}
        $state.files+=@([ordered]@{path=$relative;existed=($null -ne $hash);original=[Convert]::ToBase64String($document.bytes);beforeHash=$hash;appliedHash=$applied;keys=$keys;planned=[Convert]::ToBase64String($bytes)})
    }
    $statePath=NrSafe $game 'NeuRotic\prepared-test-profile.json';New-Item -ItemType Directory -Force (Split-Path -Parent $statePath)|Out-Null
    [IO.File]::WriteAllText($statePath,($state|ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
    try{NrWrite $game $presetRelative $presetBytes $null;foreach($file in $state.files){NrWrite $game $file.path ([Convert]::FromBase64String($file.planned)) $file.beforeHash}}
    catch{NrProfileRestore $game;throw}
}
