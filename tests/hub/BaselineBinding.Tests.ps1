$ErrorActionPreference='Stop'
$root=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$resolver=Join-Path $root 'tools/hub/Resolve-HubBindings.ps1'
function Assert($condition,$message) { if(-not $condition){throw $message}; Write-Output "PASS $message" }
$before=(Get-FileHash -LiteralPath (Join-Path $root 'OptiScaler/menu/menu_common.cpp')).Hash
Assert (Test-Path -LiteralPath $resolver) 'NH-00-IDENTITY: source binding resolver exists'
$bindings=& $resolver -Root $root
Assert ($bindings.ui.sha256 -eq $before) 'NH-00-IDENTITY: live UI hash matches'
Assert ($bindings.acceptedAnchor -eq '23b1cb299d8e378371ba9eb7753f499d6ae4bc6b') 'NH-00-IDENTITY: accepted anchor retained'
Assert ((Get-FileHash -LiteralPath (Join-Path $root 'OptiScaler/menu/menu_common.cpp')).Hash -eq $before) 'NH-00-ISOLATION: binding leaves live source bytes unchanged'
try { & $resolver -Root $root -ExpectedCommit ('0'*40); throw 'mismatch accepted' } catch { Assert ($_.Exception.Message -ne 'mismatch accepted') 'NH-00-IDENTITY: mismatching source commit rejected' }
Assert ($bindings.installer.path -eq 'tools/packaging/customer/support/NeuRotic-Setup-Engine.ps1') 'NH-00-IDENTITY: current installer authority bound'
