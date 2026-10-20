$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'FixtureCommon.ps1')
. (Join-Path $script:Repo 'tools/packaging/customer/support/NeuRotic-HubProtocol.ps1')
Assert ((Read-HubRequestText '{"protocolVersion":1,"kind":"Preflight"}').kind -eq 'Preflight') 'NH-PREFLIGHT: target-free read-only startup request accepted'
try{[void](Read-HubRequestText '{"protocolVersion":1,"kind":"Preflight","gameExecutable":"C:/game.exe"}');throw 'accepted target'}catch{Assert ($_.Exception.Message -ne 'accepted target') 'NH-PREFLIGHT: startup check rejects target or mutation fields'}
Init-HubFixtures
try {
 $result=HubCall ([ordered]@{kind='Preflight'})
 Assert ($result.status -eq 'PreflightChecked' -and $result.runtime.minimumVersion -eq '14.44.35207.0') 'NH-PREFLIGHT: current approved package determines runtime minimum'
 Assert ($result.runtime.downloadUrl -eq 'https://aka.ms/vc14/vc_redist.x64.exe') 'NH-PREFLIGHT: official Microsoft x64 download'
 Assert ($result.runtime.ready -is [bool]) 'NH-PREFLIGHT: runtime readiness is typed'
}finally{End-HubFixtures}
