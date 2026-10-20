$ErrorActionPreference='Stop'
$repo=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
. (Join-Path $repo 'tools/packaging/customer/support/NeuRotic-HubSettings.ps1')
function SafePath($root,$relative){return Join-Path $root $relative}
function Assert($ok,$message){if(-not $ok){throw $message};Write-Output ('PASS '+$message)}
# Unique task-owned fixture; never claim or remove an existing shared directory.
$work=Join-Path ([IO.Path]::GetTempPath()) ('NeuRotic-NR-WO-0041-NativeResolution-'+[Guid]::NewGuid().ToString('N'))
$expectedWork=[IO.Path]::GetFullPath($work)
if(Test-Path -LiteralPath $work){throw 'Existing fixture path preserved'}
New-Item -ItemType Directory -Path $work -ErrorAction Stop|Out-Null
$owner=Join-Path $work 'NR-WO-0041-test-owner.txt'
[IO.File]::WriteAllText($owner,$expectedWork)
$path=Join-Path $work 'OptiScaler.ini'
function Write-Ini([string]$text){[IO.File]::WriteAllText($path,$text,(New-Object Text.UTF8Encoding($false)))}
function Choice(){return @(Get-HubSettingsProjection $work|Where-Object{$_.key -ceq 'NativeResolutionChoice'})[0]}
function Patch([string]$key,[string]$value){return [pscustomobject]@{section='DlssNr';key=$key;value=$value}}
function Save-Choice([string]$choice,$extra=@()){$bytes=Get-HubSettingsBytes $work (@((Patch NativeResolutionChoice $choice))+$extra);[IO.File]::WriteAllBytes($path,$bytes)}
function Assert-Value([string]$key,[string]$value){$entry=@((Read-HubSettingsDocument $path).entries|Where-Object{$_.section -ceq 'DlssNr' -and $_.key -ceq $key});Assert ($entry.Count -eq 1 -and $entry[0].value -ceq $value) ($key+'='+$value)}
try{
 foreach($case in @(
  @{body="RenderingMode=1`nWorkingScale=1`n";expected='pre'},
  @{body="WorkingScale=1`n";expected='pre'},
  @{body="RenderingMode=2`nWorkingScale=1`n";expected='pre'},
  @{body="RunBeforeSR=true`nWorkingScale=1`n";expected='pre'},
  @{body="RenderingMode=0`nWorkingScale=1`n";expected='full'},
  @{body="RenderingMode=1`nWorkingScale=0.67`n";expected='manual'},
  @{body="RenderingMode=0`nWorkingScale=1`nUiManualResolution=true`n";expected='manual'},
  @{body="NativeResolution=1`nRenderingMode=0`nWorkingScale=1`n";expected='post'}
 )){Write-Ini ("[DlssNr]`nRoute=0`n"+$case.body);$before=(Get-FileHash $path).Hash;Assert ((Choice).value -ceq $case.expected) ('safe legacy projection '+$case.expected);Assert ((Get-FileHash $path).Hash -ceq $before) 'projection does not normalize persisted fields'}
 Write-Ini "[DlssNr]`nRoute=0`nRenderingMode=0`nWorkingScale=0.67`nUiManualResolution=true`nUnknown=keep ; retained`n[Menu]`nOverlayMenu=false`n"
 Save-Choice post
 foreach($pair in @(@('NativeResolution','1'),@('RenderingMode','0'),@('RunBeforeSR','false'),@('WorkingScale','1'),@('UiManualResolution','false'),@('UiManualScale','0.67'))){Assert-Value $pair[0] $pair[1]}
 Assert ((Choice).value -ceq 'post') 'Post reloads as Post'
 Save-Choice pre
 foreach($pair in @(@('NativeResolution','0'),@('RenderingMode','1'),@('RunBeforeSR','true'),@('WorkingScale','1'),@('UiManualResolution','false'))){Assert-Value $pair[0] $pair[1]}
 Save-Choice manual
 Assert-Value NativeResolution 0;Assert-Value WorkingScale 0.67;Assert-Value UiManualResolution true;Assert-Value RenderingMode 0;Assert-Value RunBeforeSR false
 Save-Choice full
 Assert-Value NativeResolution 0;Assert-Value WorkingScale 1;Assert-Value UiManualResolution false;Assert ((Choice).value -ceq 'full') 'Full remains Full'
 Save-Choice manual @((Patch WorkingScale '0.5'))
 Assert-Value WorkingScale 0.5;Assert-Value UiManualScale 0.5
 [IO.File]::WriteAllBytes($path,(Get-HubSettingsBytes $work @((Patch WorkingScale 'auto'))));Assert-Value WorkingScale auto;Assert ((Choice).value -ceq 'manual') 'Manual scale Auto retains manual selection at the default scale'
 Assert ([IO.File]::ReadAllText($path).Contains('Unknown=keep ; retained')) 'unrelated comments and unknown keys preserved'
 Write-Ini "[DlssNr]`nRoute=0`nNativeResolution=9`nRenderingMode=1`nWorkingScale=1`n[Menu]`nOverlayMenu=false`n"
 $before=(Get-FileHash $path).Hash;Assert ((Choice).value -ceq 'unknown:9') 'unknown new mode is not silently projected as known';Assert ((Get-FileHash $path).Hash -ceq $before) 'unknown remains unchanged on read'
 [IO.File]::WriteAllBytes($path,(Get-HubSettingsBytes $work @([pscustomobject]@{section='Menu';key='OverlayMenu';value='true'})));Assert-Value NativeResolution 9
 Save-Choice post;Assert-Value NativeResolution 1
 foreach($body in @("NativeResolution=1`nNativeResolution=0`n","WorkingScale=1`nWorkingScale=0.5`n","RenderingMode=1`n[DlssNr]`nRoute=0`n")){
  Write-Ini ("[DlssNr]`nRoute=0`n"+$body);$before=(Get-FileHash $path).Hash;Assert (-not (Choice).available) 'ambiguous dependency disables semantic selector';$refused=$false;try{Save-Choice pre}catch{$refused=$true};Assert $refused 'ambiguous coupled save refused';Assert ((Get-FileHash $path).Hash -ceq $before) 'refusal leaves complete file unchanged'
 }
 Write-Ini "[DlssNr]`nRoute=0`nRenderingMode=0`nWorkingScale=1`n";$refused=$false;try{Save-Choice post @((Patch RenderingMode '1'))}catch{$refused=$true};Assert $refused 'conflicting direct placement and semantic patch refused'
 Write-Ini "[DlssNr]`nRoute=1`nPresentResolution=0`nWorkingScale=0.67`n";$before=(Get-FileHash $path).Hash;$refused=$false;try{Save-Choice post}catch{$refused=$true};Assert $refused 'Present route cannot receive Native-only choice';Assert ((Get-FileHash $path).Hash -ceq $before) 'Present remains unchanged'
 Assert (-not (Test-HubPreparedSetting DlssNr NativeResolution)) 'NativeResolution excluded from NR Anything prepared settings'
 Assert (-not (Test-HubPreparedSetting DlssNr NativeResolutionChoice)) 'semantic Native choice excluded from NR Anything prepared settings'
 foreach($encoding in @((New-Object Text.UTF8Encoding($true)),(New-Object Text.UnicodeEncoding($false,$true)),(New-Object Text.UnicodeEncoding($true,$true)),[Text.Encoding]::GetEncoding(1252))){
  $text="; retain caf$([char]233)`r`n[DlssNr]`r`nRoute=0`r`nWorkingScale=1`r`nRenderingMode=1 ; stage note`r`nUnknown=keep`r`n"
  [IO.File]::WriteAllBytes($path,[byte[]]($encoding.GetPreamble()+$encoding.GetBytes($text)));Save-Choice post
  $doc=Read-HubSettingsDocument $path;Assert ([Convert]::ToBase64String($doc.bom) -ceq [Convert]::ToBase64String($encoding.GetPreamble())) ('coupled action preserves BOM '+$encoding.CodePage)
  Assert ($doc.text.Contains("; retain caf$([char]233)") -and $doc.text.Contains('RenderingMode=0 ; stage note') -and $doc.text.Contains('Unknown=keep') -and $doc.text -notmatch '(?<!\r)\n') ('coupled action preserves comments, unknowns and CRLF '+$encoding.CodePage)
 }
 $before=(Get-FileHash $path).Hash;$refused=$false;try{Get-HubSettingsBytes $work @((Patch NativeResolution '1'))|Out-Null}catch{$refused=$true};Assert $refused 'raw internal mode writes require semantic action';Assert ((Get-FileHash $path).Hash -ceq $before) 'raw internal refusal preserves bytes'
 Write-Output 'PASS Native resolution desktop contract'
}finally{
 $resolved=[IO.Path]::GetFullPath($work);if($resolved -cne $expectedWork -or -not (Test-Path -LiteralPath $owner) -or [IO.File]::ReadAllText($owner) -cne $expectedWork){throw 'Unexpected or unowned cleanup path'}
 if((Get-Item -LiteralPath $resolved -Force).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked fixture root cleanup refused'}
 foreach($entry in Get-ChildItem -LiteralPath $resolved -Recurse -Force){if($entry.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked fixture cleanup refused'}}
 Remove-Item -LiteralPath $resolved -Recurse -Force
}
