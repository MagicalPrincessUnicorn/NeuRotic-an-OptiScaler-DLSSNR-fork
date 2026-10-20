$ErrorActionPreference='Stop'
$repo=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$support=Join-Path $repo 'tools/packaging/customer/support'
$shell=Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/powershell.exe'
function Assert-X86($Condition,[string]$Message){if(-not $Condition){throw $Message};Write-Output ('PASS '+($Message -split ': ',2)[0])}
function Write-X86File([string]$Root,[string]$Relative,[byte[]]$Bytes){$path=Join-Path $Root $Relative;[void][IO.Directory]::CreateDirectory((Split-Path -Parent $path));[IO.File]::WriteAllBytes($path,$Bytes)}
function New-PeBytes([int]$Bits,[int]$Mark=0){
 $b=New-Object byte[] 512;$b[0]=0x4d;$b[1]=0x5a;$b[60]=0x80;$b[128]=0x50;$b[129]=0x45
 if($Bits -eq 32){$b[132]=0x4c;$b[133]=1;$b[152]=0x0b;$b[153]=1}else{$b[132]=0x64;$b[133]=0x86;$b[152]=0x0b;$b[153]=2};$b[511]=$Mark;return $b
}
function Seal-X86Package([string]$Root){
 $files=@(Get-ChildItem -LiteralPath $Root -File -Recurse|Where-Object{$_.Name -ne 'PACKAGE-MANIFEST.json'}|ForEach-Object{[ordered]@{path=$_.FullName.Substring($Root.Length+1);sha256=(Get-FileHash -LiteralPath $_.FullName).Hash}})
 [IO.File]::WriteAllText((Join-Path $Root 'support/PACKAGE-MANIFEST.json'),([ordered]@{kind='neurotic-customer-candidate';name='Prepared fixture';commit=('a'*40);files=$files}|ConvertTo-Json -Depth 8))
}
function Invoke-X86Engine([string]$Package,[string]$Game,[string[]]$Options){
 $log=Join-Path $work 'engine.log'
 $oldPreference=$ErrorActionPreference;$ErrorActionPreference='Continue'
 try{& $shell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $support 'NeuRotic-Setup-Engine.ps1') -HubPackageRoot $Package -GameExecutable (Join-Path $Game 'FixtureGame.exe') @Options *> $log;$code=$LASTEXITCODE}finally{$ErrorActionPreference=$oldPreference}
 return [pscustomobject]@{exit=$code;log=[IO.File]::ReadAllText($log)}
}
