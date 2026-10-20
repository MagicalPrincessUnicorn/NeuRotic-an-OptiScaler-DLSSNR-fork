. (Join-Path $PSScriptRoot 'SimpleInstallerFixtureCommon.ps1')
. (Join-Path $script:SimpleSupport 'NeuRotic-InstallReceipt.ps1')
Start-SimpleFixtures
try {
 $game=New-SimpleGameFixture 'receipt'
 $none=Read-NeuroticInstallReceipt $game
 Assert-Simple ($null -eq $none.receipt) 'AbsentReceiptIsUsable'
 $receipt=[pscustomobject]@{schemaVersion=3;kind='neurotic-game-install';selectedExecutable='FixtureGame.exe';architecture='x64';packageId='fixture';proxy='dxgi.dll';status='Partial';files=@([pscustomobject]@{path='dxgi.dll';removeOnUninstall=$true;role='proxy'})}
 Save-NeuroticInstallReceipt $game $receipt
 $json=Get-Content -Raw -LiteralPath (Join-Path $game 'NeuRotic/Installer/Current-Install.json')
 Assert-Simple ($json -notmatch 'sha256|preimage|backup|history|[A-Za-z]:\\') 'receipt contains only relative ownership, no hashes or restoration data'
 $moved=Join-Path $script:SimpleWork 'moved'
 Move-Item -LiteralPath $game -Destination $moved
 $read=Read-NeuroticInstallReceipt $moved
 Assert-Simple ($read.receipt.selectedExecutable -eq 'FixtureGame.exe' -and (Resolve-NeuroticOwnedPath $moved $read.receipt.files[0].path) -eq (Join-Path $moved 'dxgi.dll')) 'RelativeReceiptMovesWithGame'
 foreach($architecture in @('pe32-machine-01c4','pe64-machine-aa64','pe64-machine-0000','pe32-machine-ffff')){
  $receipt.architecture=$architecture
  Save-NeuroticInstallReceipt $moved $receipt
  $roundtrip=Read-NeuroticInstallReceipt $moved
  Assert-Simple ($roundtrip.receipt.architecture -ceq $architecture -and $roundtrip.receipt.files[0].removeOnUninstall -eq $true -and $roundtrip.receipt.files[0].path -ceq 'dxgi.dll') "ObservedMachineIdentityRetainsOwnership $architecture"
 }
 foreach($architecture in @('../outside','pe64-machine-aa6','pe64-machine-aa640','pe64-machine-gggg','pe128-machine-aa64',"pe64-machine-aa64`n",64,$null,@('x64','x86'))){
  $receipt.architecture=$architecture
  $failed=$false;try{Save-NeuroticInstallReceipt $moved $receipt}catch{$failed=$_.Exception.Message -eq 'Invalid receipt architecture'}
  Assert-Simple $failed 'MalformedArchitectureCannotReplaceOwnership'
  Assert-Simple ((Read-NeuroticInstallReceipt $moved).receipt.architecture -ceq 'pe32-machine-ffff') 'RejectedArchitecturePreservesPriorReceipt'
 }
 $receipt.architecture='x64'
 foreach($unsafe in @('../outside.dll','C:\outside.dll','/outside.dll','..\outside.dll','dxgi.dll:stream','bad.\dxgi.dll')){
  $failed=$false;try{Resolve-NeuroticOwnedPath $moved $unsafe|Out-Null}catch{$failed=$true}
  Assert-Simple $failed "RejectEscapingReceiptPath $unsafe"
 }
 $receipt.files=Merge-NeuroticOwnedFiles $read.receipt.files @([pscustomobject]@{path='OptiScaler/CharacterInspector/Version 1/worker.dll';removeOnUninstall=$true;role='inspector'})
 Save-NeuroticInstallReceipt $moved $receipt
 Assert-Simple (@((Read-NeuroticInstallReceipt $moved).receipt.files).Count -eq 2) 'OwnedPathsSurviveUpdates'
 # Intent is durable before an interrupted copy; no target file need exist yet.
 Assert-Simple ((Read-NeuroticInstallReceipt $moved).receipt.status -eq 'Partial' -and -not(Test-Path -LiteralPath (Join-Path $moved 'dxgi.dll'))) 'WriteIntentSurvivesInterruptedCopy'
 $outside=Join-Path $script:SimpleWork 'outside';New-Item -ItemType Directory -Path $outside|Out-Null
 $link=Join-Path $moved 'linked'
 New-Item -ItemType Junction -Path $link -Target $outside|Out-Null
 try{
  $failed=$false;try{Resolve-NeuroticOwnedPath $moved 'linked/foreign-mod.txt'|Out-Null}catch{$failed=$true}
  Assert-Simple $failed 'receipt destination junction is refused before traversal'
 }finally{[IO.Directory]::Delete($link)}
 Assert-Simple (@(Get-ChildItem -LiteralPath $moved -Directory|Where-Object{$_.Name -match 'backup|preserved'}).Count -eq 0) 'no backup or preserved-file directory created'
}finally{End-SimpleFixtures}
