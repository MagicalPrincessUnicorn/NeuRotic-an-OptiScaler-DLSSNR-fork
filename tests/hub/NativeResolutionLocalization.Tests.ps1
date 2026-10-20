$ErrorActionPreference='Stop'
$repo=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
function Assert($ok,$message){if(-not $ok){throw $message};Write-Output ('PASS '+$message)}
$new=@(
 [pscustomobject]@{id='desktop.settings.native_resolution';english='Native resolution'},
 [pscustomobject]@{id='desktop.settings.native_resolution_help';english='Both Match Native options use the native input dimensions. The selected stage controls when Neural Rendering runs.'},
 [pscustomobject]@{id='desktop.settings.native_resolution_full';english='Full output'},
 [pscustomobject]@{id='desktop.settings.native_resolution_pre';english='Match Native (Pre Super Resolution)'},
 [pscustomobject]@{id='desktop.settings.native_resolution_post';english='Match Native (Post Super Resolution)'},
 [pscustomobject]@{id='desktop.settings.native_resolution_unknown';english='Unknown saved resolution. Select a known option to replace it.'}
)
$english=@(Get-Content -LiteralPath (Join-Path $repo 'localization/english.json') -Raw -Encoding UTF8|ConvertFrom-Json)
Assert (@($english|Group-Object id|Where-Object{$_.Count -ne 1}).Count -eq 0) 'English inventory has unique stable IDs'
foreach($entry in $new){$found=@($english|Where-Object{$_.id -ceq $entry.id});Assert ($found.Count -eq 1 -and $found[0].english -ceq $entry.english -and $found[0].surface -ceq 'desktop') ('canonical desktop text '+$entry.id)}
foreach($locale in @('de','es','fr','pl','ja','ko','pt-BR','ru','zh-CN')){
 $packFile=Get-Item -LiteralPath (Join-Path $repo ('localization/bundled/'+$locale+'.nrlang'))
 $pack=Get-Content -LiteralPath $packFile.FullName -Raw -Encoding UTF8|ConvertFrom-Json
 foreach($entry in $new){$found=@($pack.desktop|Where-Object{$_.id -ceq $entry.id});Assert ($found.Count -eq 1 -and -not [string]::IsNullOrWhiteSpace($found[0].translation) -and $found[0].revision -ge 0) ($pack.locale+' translation '+$entry.id)}
 Assert (@($pack.in_game|Where-Object{$_.id -cin @($new.id)}).Count -eq 0) ($pack.locale+' Native desktop additions remain outside in-game entries')
}
$embedded=Get-Content -LiteralPath (Join-Path $repo 'apps/NeuRoticHub/shipping-localization/OptiScaler/menu/localization/EnglishInventory.inc') -Raw
$chunks=@([regex]::Matches($embedded,'R"NR_ENGLISH\(([\s\S]*?)\)NR_ENGLISH"')|ForEach-Object{$_.Groups[1].Value})
$decoded=@(($chunks -join '')|ConvertFrom-Json)
Assert ($decoded.Count -eq $english.Count) 'desktop embedded inventory includes the complete canonical inventory'
foreach($entry in $new){Assert (@($decoded|Where-Object{$_.id -ceq $entry.id -and $_.english -ceq $entry.english}).Count -eq 1) ('embedded desktop text '+$entry.id)}
Write-Output 'PASS Native resolution desktop localization contract'
