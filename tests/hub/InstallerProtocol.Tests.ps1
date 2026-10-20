$ErrorActionPreference='Stop'
$root=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
function Assert($condition,$message){if(-not $condition){throw $message};Write-Output "PASS $message"}
$module=Join-Path $root 'tools/packaging/customer/support/NeuRotic-HubProtocol.ps1'
Assert (Test-Path -LiteralPath $module) 'NH-03-REQUEST-VALIDATION: typed protocol exists'
. $module
foreach($kind in @('Inspect','RestoreDefaults','Plan','Execute','Preflight','LaunchPreflight')){
 $request=Read-HubRequestText (([ordered]@{protocolVersion=1;kind=$kind})|ConvertTo-Json -Compress)
 Assert ($request.kind -is [string] -and $request.kind -ceq $kind) ('NH-03-REQUEST-VALIDATION: scalar kind retained: '+$kind)
}
foreach($text in @('{"protocolVersion":2,"kind":"Inspect"}','{"protocolVersion":1,"protocolVersion":1,"kind":"Inspect"}','{"protocolVersion":1,"kind":"Inspect","script":"evil.ps1"}','{"protocolVersion":1,"kind":"Wrong"}','{"protocolVersion":"1","kind":"Inspect"}','{"protocolVersion":1,"kind":["Inspect"]}','{"protocolVersion":1,"kind":"Plan","continueWithoutRuntime":"false"}','{"protocolVersion":1,"kind":"Plan","settings":{}}')){
 try{[void](Read-HubRequestText $text);throw 'accepted invalid request'}catch{Assert ($_.Exception.Message -ne 'accepted invalid request') 'NH-03-REQUEST-VALIDATION: invalid request refused'}
}
try{[void](Read-HubRequestText ('{"protocolVersion":1,"kind":"Inspect","requestId":"'+('A'*6291456)+'"}'));throw 'accepted oversize'}catch{Assert ($_.Exception.Message -eq 'Request exceeds 6 MiB') 'NH-03-REQUEST-VALIDATION: actual UTF8 request above six MiB refused by size contract'}
