$ErrorActionPreference='Stop'
$repo=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$support=Join-Path $repo 'tools/packaging/customer/support'
function Assert($ok,$message){if(-not $ok){throw $message};Write-Output ('PASS '+$message)}
# Load owner functions without executing the interactive installer.
foreach($name in @('NeuRotic-Setup-Engine.ps1','NeuRotic-HubProtocol.ps1')){
 $tokens=$null;$errors=$null;$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $support $name),[ref]$tokens,[ref]$errors)
 Assert (-not $errors) ('Parses '+$name)
 foreach($definition in $ast.FindAll({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst]},$false)){. ([scriptblock]::Create($definition.Extent.Text))}
}
. (Join-Path $support 'NeuRotic-HubSettings.ps1')
. (Join-Path $support 'NeuRotic-HubHistory.ps1')
Add-Type -Path (Join-Path $support 'NeuRotic-HubJson.cs')
$work=Join-Path $repo 'builds/hub/restore-defaults-fixture'
if(Test-Path -LiteralPath $work){throw 'Previous defaults fixture remains; inspect before reuse'}
New-Item -ItemType Directory -Path (Join-Path $work 'game'),(Join-Path $work 'package/payload') -Force|Out-Null
try{
 $game=Join-Path $work 'game';$ini=Join-Path $game 'OptiScaler.ini'
 $exe=Join-Path $game 'dd2.exe';Copy-Item -LiteralPath (Join-Path $env:SystemRoot 'System32/cmd.exe') -Destination $exe
 $template=Join-Path $work 'package/payload/OptiScaler.ini';Copy-Item -LiteralPath (Join-Path $repo 'integration/OptiScaler.ini') -Destination $template
 $package=[pscustomobject]@{root=(Join-Path $work 'package');id='flagship-approved';digest='fixture';manifest=[pscustomobject]@{files=@([pscustomobject]@{path='payload\OptiScaler.ini';sha256=(HashFile $template)})}}
 $text="; custom comment`r`n[Spoofing]`r`nDxgi=true`r`n[OptiFG]`r`nDisableHUDFix=false`r`n[Hotfix]`r`nRestoreComputeSignature=false`r`n[DlssNr]`r`nEnabled=true`r`n[Unknown]`r`nCustom=keep`r`n"
 [IO.File]::WriteAllText($ini,$text)
 $target=Get-HubTarget $exe
 $inspection=[pscustomobject]@{settings=@(Get-HubSettingsProjection $game);state=$null}
 $before=HashFile $ini
 $defaults=@(Get-HubDefaultSettings $target $inspection $package)
 Assert ((HashFile $ini) -eq $before) 'Defaults resolution does not write the installed INI'
 foreach($id in @('Spoofing/Dxgi','OptiFG/DisableHUDFix','Hotfix/RestoreComputeSignature')){
  $row=@($defaults|Where-Object{($_.section+'/'+$_.key) -eq $id})
  Assert ($row.Count -eq 1 -and $row[0].value -eq 'auto') ('Dragon Dogma restores runtime game/GPU rule: '+$id)
 }
 Assert (@($defaults|Where-Object{$_.section -eq 'Unknown'}).Count -eq 0) 'Unknown keys are excluded from reset'
 $bytes=Get-HubSettingsBytes $game $defaults
 Assert ([Text.Encoding]::UTF8.GetString($bytes).Contains('Custom=keep')) 'Save projection preserves unknown settings'
 Assert ([Text.Encoding]::UTF8.GetString($bytes).Contains('; custom comment')) 'Save projection preserves comments'
 # Unknown games use the same verified installation recipe and automatic runtime selection.
 $unknown=Join-Path $game 'UnknownGame.exe';Copy-Item -LiteralPath $exe -Destination $unknown
 $other=@(Get-HubDefaultSettings (Get-HubTarget $unknown) $inspection $package)
 Assert (($other|ConvertTo-Json -Compress) -ceq ($defaults|ConvertTo-Json -Compress)) 'No invented profile for an unknown executable'
 function Resolve-HubPackage($Id){return $package}
 $request=[pscustomobject]@{gameExecutable=$exe;configRevision=$before}
 $resolved=Resolve-HubSettingsDefaults $request
 Assert ($resolved.status -eq 'DefaultsResolved' -and $resolved.inspection.target.executable -eq $exe -and $resolved.inspection.configRevision -eq $before) 'Protocol binds fresh target and unchanged settings revision'
 Assert ((HashFile $ini) -eq $before) 'Protocol stages only, without saving defaults'
 function Resolve-HubPackage($Id){[IO.File]::AppendAllText($ini,'; concurrent change');return $package}
 $raced=Resolve-HubSettingsDefaults $request
 Assert ($raced.status -eq 'PreconditionChanged' -and [IO.File]::ReadAllText($ini).Contains('; concurrent change')) 'File changes during defaults resolution refuse and survive'
 function Resolve-HubPackage($Id){return $package}
 [IO.File]::WriteAllText($ini,$text)
 [IO.File]::AppendAllText($ini,'; external change');$external=HashFile $ini
 $stale=Resolve-HubSettingsDefaults $request
 Assert ($stale.status -eq 'PreconditionChanged' -and (HashFile $ini) -eq $external) 'Stale defaults request refuses and preserves external edits'
 [IO.File]::WriteAllText($ini,$text)
 $valid=[ordered]@{protocolVersion=1;kind='RestoreDefaults';gameExecutable=$exe;configRevision=$before}
 Assert ((Read-HubRequestText ($valid|ConvertTo-Json)).kind -eq 'RestoreDefaults') 'Typed read-only request accepted'
 $valid.settings=@();$refused=$false;try{Read-HubRequestText ($valid|ConvertTo-Json)|Out-Null}catch{$refused=$true}
 Assert $refused 'Defaults request refuses mutation fields'
 [IO.File]::WriteAllText($ini,$text+"[Spoofing]`r`nDxgi=false`r`n")
 $ambiguous=[pscustomobject]@{settings=@(Get-HubSettingsProjection $game);state=$null}
 $safe=@(Get-HubDefaultSettings $target $ambiguous $package)
 Assert (@($safe|Where-Object{$_.section -eq 'Spoofing' -and $_.key -eq 'Dxgi'}).Count -eq 0) 'Ambiguous installed sections stay untouched'
 [IO.File]::WriteAllText($ini,$text)
 # Explicit package preferences must win over the editable schema missing-value fallback.
 $packageText=[IO.File]::ReadAllText($template);$packageText=$packageText -replace '(?m)^LogToFile\s*=.*$','LogToFile = true'
 [IO.File]::WriteAllText($template,$packageText);$package.manifest.files[0].sha256=HashFile $template
 $testing=@(Get-HubDefaultSettings $target $inspection $package)
 Assert (@($testing|Where-Object{$_.section -eq 'Log' -and $_.key -eq 'LogToFile' -and $_.value -eq 'true'}).Count -eq 1) 'Testing package logging preference remains enabled'
 [IO.File]::AppendAllText($template,'; tamper');$refused=$false
 try{Get-HubDefaultSettings $target $inspection $package|Out-Null}catch{$refused=$true}
 Assert ($refused -and (HashFile $ini) -eq $before) 'Changed package defaults refuse without mutation'
 # The 32-bit capture route resets the separate renderer, preserving root settings.
 Copy-Item -LiteralPath (Join-Path $env:SystemRoot 'SysWOW64/cmd.exe') -Destination $exe -Force
 $helper=Join-Path $game 'NeuRotic/Prepared/NeuRotic.GpuHost'
 New-Item -ItemType Directory -Path $helper -Force|Out-Null
 [IO.File]::WriteAllText((Join-Path $helper 'OptiScaler.ini'),"[DlssNr]`nEnabled=false`nInputSource=2`n")
 $helperRelative='payload\NeuRotic\Prepared\NeuRotic.GpuHost\OptiScaler.ini'
 $helperTemplate=Join-Path $package.root $helperRelative
 New-Item -ItemType Directory -Path (Split-Path $helperTemplate) -Force|Out-Null
 [IO.File]::WriteAllText($helperTemplate,"[DlssNr]`nEnabled=true`nInputSource=3`nInputTransport=1`nAllowCpuFallback=false`n")
 $package.id='prepared-x86';$package.manifest.files=@([pscustomobject]@{path=$helperRelative;sha256=(HashFile $helperTemplate)})
 function Resolve-HubPackage($Id){Assert ($Id -eq 'prepared-x86') 'x86 defaults select prepared package'|Out-Host;return $package}
 $helperBefore=HashFile (Join-Path $helper 'OptiScaler.ini');$rootBefore=HashFile $ini
 $resolved=Resolve-HubSettingsDefaults ([pscustomobject]@{gameExecutable=$exe;configRevision=$helperBefore})
 Assert ($resolved.status -eq 'DefaultsResolved' -and @($resolved.defaultSettings|Where-Object{$_.section -eq 'DlssNr' -and $_.key -eq 'Enabled' -and $_.value -eq 'true'}).Count -eq 1) 'x86 defaults use helper payload and validation root'
 Assert ((HashFile $ini) -eq $rootBefore -and (HashFile (Join-Path $helper 'OptiScaler.ini')) -eq $helperBefore) 'x86 defaults resolution preserves both INIs'
 $quirks=[IO.File]::ReadAllText((Join-Path $repo 'OptiScaler/misc/Quirks.h'))
 Assert ($quirks -match 'QUIRK_ENTRY\("dd2.exe", GameQuirk::RestoreComputeSigOnNonNvidia, GameQuirk::DisableDxgiSpoofing,\s*GameQuirk::DisableHudfix\)') 'Dragon Dogma compatibility defaults remain owned by actual runtime quirk table'
}finally{
 $resolved=[IO.Path]::GetFullPath($work)
 if($resolved -ne [IO.Path]::GetFullPath((Join-Path $repo 'builds/hub/restore-defaults-fixture'))){throw 'Unexpected defaults fixture root'}
 foreach($entry in Get-ChildItem -LiteralPath $resolved -Recurse -Force){if($entry.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked fixture cleanup refused'}}
 Remove-Item -LiteralPath $resolved -Recurse -Force
}
