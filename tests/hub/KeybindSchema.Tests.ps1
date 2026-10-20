$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../../tools/packaging/customer/support/NeuRotic-HubSettings.ps1')
$schema=@($script:HubSettingsSchema|Where-Object{$_.type -eq 'keycode'})
$failed=0
foreach($s in $schema){
 foreach($v in @('auto','-1','0','255','0xFF','375','0x177','0x22D','846','0x777')){
  if(-not (Test-HubSettingValue $s $v)){Write-Output ('FAIL valid '+$s.key+' '+$v);$failed++}
 }
 foreach($v in @('256','0x100','0x800','0x117','0x15B','0x110','-2','1.5','0x7777')){
  if(Test-HubSettingValue $s $v){Write-Output ('FAIL malformed '+$s.key+' '+$v);$failed++}
 }
}
if($failed){throw "$failed keycode checks failed"}
Write-Output 'PASS all six shortcut schemas preserve legacy values and validate Ctrl/Shift/Alt integer chords'
