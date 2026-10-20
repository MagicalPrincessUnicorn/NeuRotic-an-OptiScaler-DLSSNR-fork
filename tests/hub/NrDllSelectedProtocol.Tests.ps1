param([Parameter(Mandatory=$true)][string]$SupportRoot)
$ErrorActionPreference='Stop'
$script:Checks=0
function Check($value,[string]$why){if(-not $value){throw $why};$script:Checks++}
function Copy-NrProtocolChoice($source){$next=[ordered]@{};foreach($key in $source.Keys){$next[$key]=$source[$key]};return $next}
function Reject($request,[string]$why){try{[void](Read-HubRequestText ($request|ConvertTo-Json -Depth 12 -Compress))}catch{$script:Checks++;return};throw $why}
try{
 . (Join-Path $SupportRoot 'NeuRotic-InstallCore.ps1')
 . (Join-Path $SupportRoot 'NeuRotic-HubProtocol.ps1')
 $copy=[ordered]@{mode='profile';profileId='aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa';profileName='User chosen bytes';source='C:\fixture\profiles\aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\nvngx_dlssnr.dll';sha256=('a'*64);bytes=[uint64]9007199254740993}
 $request=[ordered]@{protocolVersion=1;requestId='fixture';kind='Plan';operation='Install';gameExecutable='C:\fixture\game.exe';nrDllCopy=$copy}
 try{$valid=Read-HubRequestText ($request|ConvertTo-Json -Depth 12 -Compress)}catch{throw 'selected NR DLL copy request must be accepted'}
 Check ($valid.nrDllCopy.bytes -eq [uint64]9007199254740993) 'selected source uint64 continuity must not round through floating point'
 foreach($mode in @('none','preserve')){
  $request.nrDllCopy=[ordered]@{mode=$mode};$valid=Read-HubRequestText ($request|ConvertTo-Json -Depth 12 -Compress)
  Check ($valid.nrDllCopy.mode -ceq $mode) 'manual and preserve are explicit independent choices'
 }
 $request.nrDllCopy=[ordered]@{mode='none';source=$copy.source};Reject $request 'manual must not carry an executable copy source'
 $request.nrDllCopy=(Copy-NrProtocolChoice $copy);$request.nrDllCopy.source='C:\fixture\another.dll';Reject $request 'selected source filename must match the supplied DLL contract'
 $request.nrDllCopy=(Copy-NrProtocolChoice $copy);$request.nrDllCopy.source='C:relative\nvngx_dlssnr.dll';Reject $request 'selected source cannot depend on a worker current directory'
 $request.nrDllCopy=(Copy-NrProtocolChoice $copy);$request.nrDllCopy.bytes=-1;Reject $request 'negative continuity size refused'
 $request.nrDllCopy=(Copy-NrProtocolChoice $copy);$request.nrDllCopy.bytes=1.5;Reject $request 'fractional continuity size refused'
 $request.nrDllCopy=(Copy-NrProtocolChoice $copy);$request.nrDllCopy.sha256='developer-reference';Reject $request 'continuity hash must identify selected bytes'
 $request.nrDllCopy=(Copy-NrProtocolChoice $copy);$request.nrDllCopy.fallback='default';Reject $request 'unknown source or fallback controls refused'
 $request.nrDllCopy=(Copy-NrProtocolChoice $copy);$request.nrDllCopy.bytes=0;$valid=Read-HubRequestText ($request|ConvertTo-Json -Depth 12 -Compress)
 Check ($valid.nrDllCopy.bytes -eq 0) 'no reference byte-size admission requirement'
 $request.nrDllCopy=(Copy-NrProtocolChoice $copy);$request.nrDllCopy.bytes=[uint64]::MaxValue;$valid=Read-HubRequestText ($request|ConvertTo-Json -Depth 12 -Compress)
 Check ([uint64]$valid.nrDllCopy.bytes -eq [uint64]::MaxValue) 'full unsigned continuity field remains exact through the PS5.1 protocol'
 $request.operation='Uninstall';Reject $request 'NR copy selection belongs only to Install'
 $request.operation='Install';$request.kind='Execute';Reject $request 'Execute cannot replace its bound selection'
 [Console]::Out.WriteLine(('PASS {0} selected NR DLL protocol checks; metadata only, no provider load' -f $script:Checks));exit 0
}catch{[Console]::Error.WriteLine(('FAIL '+$_.Exception.Message));exit 1}
