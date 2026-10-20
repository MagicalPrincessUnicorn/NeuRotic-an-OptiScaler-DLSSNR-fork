$ErrorActionPreference='Stop'
$script:SimpleRepo=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$script:SimpleWork=Join-Path $script:SimpleRepo 'builds/hub/simple-installer-fixtures'
$script:SimpleSupport=Join-Path $script:SimpleRepo 'tools/packaging/customer/support'
function Assert-Simple($Condition,[string]$Message){if(-not $Condition){throw $Message};Write-Output "PASS $Message"}
function Start-SimpleFixtures {
 if(Test-Path -LiteralPath $script:SimpleWork){throw 'Simple fixtures already exist; another run may own them'}
 New-Item -ItemType Directory -Path $script:SimpleWork -Force|Out-Null
}
function New-SimpleGameFixture([string]$Name){
 $path=Join-Path $script:SimpleWork $Name
 New-Item -ItemType Directory -Path $path|Out-Null
 Write-SimplePe (Join-Path $path 'FixtureGame.exe') 64
 [IO.File]::WriteAllText((Join-Path $path 'foreign-mod.txt'),'foreign bytes')
 return $path
}
function Write-SimplePe([string]$Path,[int]$Bits,[switch]$Library){
 $bytes=New-Object byte[] 512;$bytes[0]=77;$bytes[1]=90
 [BitConverter]::GetBytes([int]64).CopyTo($bytes,60);[BitConverter]::GetBytes([int]17744).CopyTo($bytes,64)
 [BitConverter]::GetBytes([uint16]$(if($Bits -eq 64){0x8664}else{0x14c})).CopyTo($bytes,68)
 [BitConverter]::GetBytes([uint16]1).CopyTo($bytes,70)
 [BitConverter]::GetBytes([uint16]$(if($Library){0x2002}else{0x0002})).CopyTo($bytes,86)
 [BitConverter]::GetBytes([uint16]$(if($Bits -eq 64){240}else{224})).CopyTo($bytes,84)
 [BitConverter]::GetBytes([uint16]$(if($Bits -eq 64){0x20b}else{0x10b})).CopyTo($bytes,88)
 [IO.File]::WriteAllBytes($Path,$bytes)
}
function New-SimplePackageFixture {
 $root=Join-Path $script:SimpleWork 'package';New-Item -ItemType Directory -Path (Join-Path $root 'payload') -Force|Out-Null
 Write-SimplePe (Join-Path $root 'payload/OptiScaler.dll') 64 -Library
 [IO.File]::WriteAllText((Join-Path $root 'payload/OptiScaler.ini'),"[Hooks]`r`nDxgi=auto`r`nRestoreComputeSignature=auto`r`n[Plugins]`r`nLoadReshade=false`r`n[DlssNr]`r`nEnabled=false`r`n")
 [IO.File]::WriteAllText((Join-Path $root 'payload/dependency.dll'),'new dependency')
 $inspector='OptiScaler/CharacterInspector/Version 1';New-Item -ItemType Directory -Path (Join-Path $root ('payload/'+$inspector)) -Force|Out-Null
 foreach($name in @('worker.py','models.lock.json')){[IO.File]::WriteAllText((Join-Path $root ('payload/'+$inspector+'/'+$name)),'tiny worker fixture '+$name)}
 return [pscustomobject]@{id='fixture-native';root=$root;architecture='x64';manifest=[pscustomobject]@{components=[pscustomobject]@{characterInspector=[pscustomobject]@{version='Version 1';relativeRoot=$inspector}};files=@('OptiScaler.dll','OptiScaler.ini','dependency.dll',($inspector+'/worker.py'),($inspector+'/models.lock.json')|ForEach-Object{[pscustomobject]@{path=('payload/'+$_)}})}}
}
function Invoke-SimpleFault([string]$Operation,[string]$Path){
 if($script:SimpleFaultOperation -eq $Operation -and $script:SimpleFaultPath -eq $Path){throw "Injected $Operation failure for $Path"}
}
function End-SimpleFixtures {
 $expected=[IO.Path]::GetFullPath((Join-Path $script:SimpleRepo 'builds/hub/simple-installer-fixtures'))
 if([IO.Path]::GetFullPath($script:SimpleWork) -ne $expected){throw 'Unexpected fixture cleanup target'}
 # Traverse one level at a time so a junction is rejected before descending.
 $pending=New-Object 'Collections.Generic.Queue[string]';$pending.Enqueue($expected)
 while($pending.Count){foreach($entry in Get-ChildItem -LiteralPath $pending.Dequeue() -Force){
  if($entry.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked fixture cleanup refused'}
  if($entry.PSIsContainer){$pending.Enqueue($entry.FullName)}
 }}
 Remove-Item -LiteralPath $expected -Recurse -Force
}
