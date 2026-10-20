. (Join-Path $PSScriptRoot 'SimpleInstallerFixtureCommon.ps1')
$script:LoggingCases=0;$script:LoggingFailures=0
$script:LoggingDefaults=[pscustomobject]@{status='Missing';preferences=$null;note=''}
$script:LoggingFault=$null
function Invoke-SimpleFault([string]$Operation,[string]$Path){
 if($Operation -eq 'ConfigureLogging' -and $script:LoggingFault){$action=$script:LoggingFault;$script:LoggingFault=$null;& $action}
 if($script:SimpleFaultOperation -eq $Operation -and $script:SimpleFaultPath -eq $Path){throw "Injected $Operation failure for $Path"}
}
function Test-LoggingCase([string]$Name,[scriptblock]$Action){
 $script:LoggingCases++
 try{& $Action;Write-Output "PASS $Name"}catch{$script:LoggingFailures++;Write-Output "FAIL $Name : $($_.Exception.Message)"}
}
function Assert-Logging($Value,[string]$Message){if(-not $Value){throw $Message}}
function Set-FixtureLoggingProfile($Package,[string]$Profile){
 if($Package.manifest.PSObject.Properties['loggingProfile']){$Package.manifest.PSObject.Properties.Remove('loggingProfile')}
 if($Profile){$Package.manifest|Add-Member -NotePropertyName loggingProfile -NotePropertyValue $Profile}
 $manifestDirectory=Join-Path $Package.root 'support';[IO.Directory]::CreateDirectory($manifestDirectory)|Out-Null
 $manifestBytes=[Text.Encoding]::UTF8.GetBytes(($Package.manifest|ConvertTo-Json -Depth 12))
 [IO.File]::WriteAllBytes((Join-Path $manifestDirectory 'PACKAGE-MANIFEST.json'),$manifestBytes)
 $digest=Get-NeuroticLoggingHash $manifestBytes
 if($Package.PSObject.Properties['digest']){$Package.digest=$digest}else{$Package|Add-Member -NotePropertyName digest -NotePropertyValue $digest}
}
Start-SimpleFixtures
try{
 . (Join-Path $script:SimpleSupport 'NeuRotic-InstallCore.ps1')
 # Define only the actual four save helpers at script scope. Parsing the setup
 # engine does not execute its picker, launcher or protocol entry point.
 $tokens=$null;$parseErrors=$null;$engineAst=[Management.Automation.Language.Parser]::ParseFile((Join-Path $script:SimpleSupport 'NeuRotic-Setup-Engine.ps1'),[ref]$tokens,[ref]$parseErrors)
 Assert-Logging (-not $parseErrors.Count) 'setup helper source parses'
 foreach($name in @('HashBytes','HashFile','SafePath','WriteVerifiedBytes')){
  $definition=@($engineAst.FindAll({param($node)$node -is [Management.Automation.Language.FunctionDefinitionAst]},$false)|Where-Object{$_.Name -ceq $name})
  Assert-Logging ($definition.Count -eq 1) 'actual helper definition is unique';Invoke-Expression $definition[0].Extent.Text
 }
 $package=New-SimplePackageFixture
 [IO.File]::WriteAllText((Join-Path $package.root 'payload/OptiScaler.ini'),"[DlssNr]`r`nEnabled=false`r`n[Log]`r`nLogToFile=true`r`nLogLevel=0`r`n")
 foreach($variant in @('auto','false','missing','already')){
  Test-LoggingCase "internal update $variant preserves unrelated preferences and unowned INI" {
   Set-FixtureLoggingProfile $package 'internal-testing'
   $game=New-SimpleGameFixture ('logging-'+$variant);$path=Join-Path $game 'OptiScaler.ini'
   $log=$(if($variant -eq 'missing'){''}elseif($variant -eq 'already'){"[lOg]`r`nLogToFile=true ; keep comment`r`nLogLevel=0`r`n"}else{"[lOg]`r`nLogToFile=$variant ; keep comment`r`nLogLevel=2`r`n"})
   $text="; personal settings`r`n[DlssNr]`r`nEnabled=true`r`nStyle=2`r`n"+$log+"[Custom]`r`nPreference=retained`r`n"
   [IO.File]::WriteAllText($path,$text)
   $result=Invoke-NeuroticSimpleInstall (Join-Path $game 'FixtureGame.exe') $package 'dxgi.dll' $false @() '' $null $null $script:LoggingDefaults
   Assert-Logging ($result.status -like 'Succeeded*') 'install must succeed without runtime/model deposit'
   $actual=[IO.File]::ReadAllText($path)
   Assert-Logging ($actual -match '(?im)^LogToFile=true(?:\s|$)' -and $actual -match '(?im)^LogLevel=0(?:\s|$)') 'internal update must apply explicit file Trace to reused INI'
   Assert-Logging ($actual.Contains("Enabled=true`r`nStyle=2") -and $actual.Contains('Preference=retained') -and $actual.Contains('; personal settings')) 'unrelated NR/preferences/comments must remain'
   Assert-Logging (-not @($result.receipt.files|Where-Object{$_.path -ieq 'OptiScaler.ini'}).Count) 'logging migration alone must not claim unowned INI for uninstall'
   $durable=(Read-NeuroticInstallReceipt $game).receipt.loggingPolicy
   Assert-Logging ($durable.profile -ceq 'internal-testing' -and $durable.action -ceq 'Reuse' -and $durable.status -cin @('Applied','AlreadyConfigured')) 'durable policy identifies copied/reused and actual outcome'
   Assert-Logging ($durable.packageManifestSha256 -ceq $package.digest -and $durable.afterSha256 -ceq (Get-NeuroticLoggingHash ([IO.File]::ReadAllBytes($path))) -and $durable.afterLogToFile -ceq 'true' -and $durable.afterLogLevel -ceq '0') 'bounded durable receipt agrees with selected manifest and final INI'
  }
 }
 foreach($profile in @('public','')){
  Test-LoggingCase "public or absent profile update byte-preserves existing logging [$profile]" {
   Set-FixtureLoggingProfile $package $profile
   $game=New-SimpleGameFixture ('logging-preserve-'+$(if($profile){$profile}else{'absent'}));$path=Join-Path $game 'OptiScaler.ini'
   $text="; exact public preference`r`n[DlssNr]`r`nEnabled=true`r`n[Log]`r`nLogToFile=auto`r`nLogLevel=2`r`n"
   [IO.File]::WriteAllText($path,$text);$before=[Convert]::ToBase64String([IO.File]::ReadAllBytes($path))
   $result=Invoke-NeuroticSimpleInstall (Join-Path $game 'FixtureGame.exe') $package 'dxgi.dll' $false @() '' $null $null $script:LoggingDefaults
   Assert-Logging ($result.status -like 'Succeeded*') 'public update succeeds'
   Assert-Logging ([Convert]::ToBase64String([IO.File]::ReadAllBytes($path)) -ceq $before) 'public or absent profile must preserve exact existing bytes'
  }
 }
 Test-LoggingCase 'internal fresh configuration remains NR off and explicit Trace' {
  Set-FixtureLoggingProfile $package 'internal-testing';$game=New-SimpleGameFixture 'logging-fresh'
  $result=Invoke-NeuroticSimpleInstall (Join-Path $game 'FixtureGame.exe') $package 'dxgi.dll' $false @() '' $null $null $script:LoggingDefaults
  $actual=[IO.File]::ReadAllText((Join-Path $game 'OptiScaler.ini'))
  Assert-Logging ($result.status -like 'Succeeded*' -and $actual -match '(?im)^Enabled=false\r?$' -and $actual -match '(?im)^LogToFile=true\r?$' -and $actual -match '(?im)^LogLevel=0\r?$') 'clean internal remains NR off/Trace even without model'
 }
 foreach($encodingName in @('utf8-bom','utf16-le','utf16-be','windows1252')){
  Test-LoggingCase "internal migration preserves encoding and comments $encodingName" {
   Set-FixtureLoggingProfile $package 'internal-testing';$game=New-SimpleGameFixture ('logging-'+$encodingName);$path=Join-Path $game 'OptiScaler.ini'
   $encoding=switch($encodingName){
    'utf8-bom'{New-Object Text.UTF8Encoding($true,$true)}
    'utf16-le'{New-Object Text.UnicodeEncoding($false,$true,$true)}
    'utf16-be'{New-Object Text.UnicodeEncoding($true,$true,$true)}
    'windows1252'{[Text.Encoding]::GetEncoding(1252)}
   }
   $text="; caf$([char]233) personal`r`n[DlssNr]`r`nEnabled=true`r`n[lOg]`r`n  lOgToFiLe = auto ; keep comment`r`nLogLevel=2`r`n[Custom]`r`nValue=keep`r`n"
   [IO.File]::WriteAllBytes($path,[byte[]]($encoding.GetPreamble()+$encoding.GetBytes($text)))
   $result=Invoke-NeuroticSimpleInstall (Join-Path $game 'FixtureGame.exe') $package 'dxgi.dll' $false @() '' $null $null $script:LoggingDefaults
   $expected=$text.Replace('lOgToFiLe = auto','lOgToFiLe = true').Replace('LogLevel=2','LogLevel=0')
   Assert-Logging ($result.status -like 'Succeeded*' -and [Convert]::ToBase64String([IO.File]::ReadAllBytes($path)) -ceq [Convert]::ToBase64String([byte[]]($encoding.GetPreamble()+$encoding.GetBytes($expected)))) 'only two value spans change, preserving exact encoding/BOM/casing/comments/other bytes'
  }
 }
 foreach($ambiguity in @('section','key')){
  Test-LoggingCase "ambiguous Log $ambiguity is preserved before delivery" {
   Set-FixtureLoggingProfile $package 'internal-testing';$game=New-SimpleGameFixture ('logging-duplicate-'+$ambiguity);$path=Join-Path $game 'OptiScaler.ini'
   $text="[Log]`r`nLogToFile=auto`r`nLogLevel=2`r`n"+$(if($ambiguity -eq 'section'){"[lOg]`r`nOther=value`r`n"}else{"LogLevel=1`r`n"})
   [IO.File]::WriteAllText($path,$text)
   $result=Invoke-NeuroticSimpleInstall (Join-Path $game 'FixtureGame.exe') $package 'dxgi.dll' $false @() '' $null $null $script:LoggingDefaults
   Assert-Logging ($result.status -ceq 'FailedWithoutChanges' -and [IO.File]::ReadAllText($path) -ceq $text -and -not [IO.File]::Exists((Join-Path $game 'dxgi.dll'))) 'ambiguous logging cannot silently target one duplicate or mutate the game'
  }
 }
 Test-LoggingCase 'outside INI edit after preflight is preserved and recorded as incomplete' {
  Set-FixtureLoggingProfile $package 'internal-testing';$game=New-SimpleGameFixture 'logging-race';$path=Join-Path $game 'OptiScaler.ini'
  [IO.File]::WriteAllText($path,"[Log]`r`nLogToFile=auto`r`nLogLevel=2`r`n")
  $outside="; outside edit`r`n[Log]`r`nLogToFile=false`r`nLogLevel=1`r`n[Custom]`r`nPreserve=outside`r`n"
  $script:LoggingFault={
   if(-not [IO.Path]::IsPathRooted($path) -or -not $path.StartsWith(($game+[IO.Path]::DirectorySeparatorChar),[StringComparison]::OrdinalIgnoreCase)){throw 'Fault mutation escaped its owned game fixture'}
   [IO.File]::WriteAllText($path,$outside)
  }.GetNewClosure()
  try{$result=Invoke-NeuroticSimpleInstall (Join-Path $game 'FixtureGame.exe') $package 'dxgi.dll' $false @() '' $null $null $script:LoggingDefaults}finally{$script:LoggingFault=$null}
  $durable=(Read-NeuroticInstallReceipt $game).receipt.loggingPolicy
  Assert-Logging ($result.status -ceq 'Partial' -and [IO.File]::ReadAllText($path) -ceq $outside -and $durable.status -ceq 'Failed' -and -not $durable.patched) 'revision/identity race fails without overwriting outside preferences or claiming Trace'
 }
 Test-LoggingCase 'identical bytes in a replacement INI do not inherit the original file identity' {
  Set-FixtureLoggingProfile $package 'internal-testing';$game=New-SimpleGameFixture 'logging-identity';$path=Join-Path $game 'OptiScaler.ini'
  $text="[Log]`r`nLogToFile=auto`r`nLogLevel=2`r`n";[IO.File]::WriteAllText($path,$text)
  $script:LoggingFault={
   if(-not [IO.Path]::IsPathRooted($path) -or -not $path.StartsWith(($game+[IO.Path]::DirectorySeparatorChar),[StringComparison]::OrdinalIgnoreCase)){throw 'Fault mutation escaped its owned game fixture'}
   [IO.File]::Move($path,($path+'.prior'));[IO.File]::WriteAllText($path,$text)
  }.GetNewClosure()
  try{$result=Invoke-NeuroticSimpleInstall (Join-Path $game 'FixtureGame.exe') $package 'dxgi.dll' $false @() '' $null $null $script:LoggingDefaults}finally{$script:LoggingFault=$null}
  $durable=(Read-NeuroticInstallReceipt $game).receipt.loggingPolicy
  Assert-Logging ($result.status -ceq 'Partial' -and [IO.File]::ReadAllText($path) -ceq $text -and $durable.beforeSha256 -ceq $durable.afterSha256 -and $durable.beforeIdentity -cne $durable.afterIdentity -and $durable.status -ceq 'Failed') 'actual held-handle identity guard rejects a same-content replacement without changing it'
 }
 Test-LoggingCase 'already configured no-op also detects an outside settings change' {
  Set-FixtureLoggingProfile $package 'internal-testing';$game=New-SimpleGameFixture 'logging-noop-race';$path=Join-Path $game 'OptiScaler.ini'
  [IO.File]::WriteAllText($path,"[Log]`r`nLogToFile=true`r`nLogLevel=0`r`n")
  $outside="[Log]`r`nLogToFile=false`r`nLogLevel=2`r`n";$script:LoggingFault={
   if(-not [IO.Path]::IsPathRooted($path) -or -not $path.StartsWith(($game+[IO.Path]::DirectorySeparatorChar),[StringComparison]::OrdinalIgnoreCase)){throw 'Fault mutation escaped its owned game fixture'}
   [IO.File]::WriteAllText($path,$outside)
  }.GetNewClosure()
  try{$result=Invoke-NeuroticSimpleInstall (Join-Path $game 'FixtureGame.exe') $package 'dxgi.dll' $false @() '' $null $null $script:LoggingDefaults}finally{$script:LoggingFault=$null}
  $durable=(Read-NeuroticInstallReceipt $game).receipt.loggingPolicy
  Assert-Logging ($result.status -ceq 'Partial' -and [IO.File]::ReadAllText($path) -ceq $outside -and -not $durable.writeAttempted -and $durable.status -ceq 'Failed') 'no-op cannot claim current Trace after an outside edit'
 }
 Test-LoggingCase 'actual sharing failure preserves INI and durable failure evidence' {
  Set-FixtureLoggingProfile $package 'internal-testing';$game=New-SimpleGameFixture 'logging-sharing';$path=Join-Path $game 'OptiScaler.ini'
  $text="[Log]`r`nLogToFile=auto`r`nLogLevel=2`r`n";[IO.File]::WriteAllText($path,$text);$held=[pscustomobject]@{handle=$null}
  $script:LoggingFault={
   if(-not [IO.Path]::IsPathRooted($path) -or -not $path.StartsWith(($game+[IO.Path]::DirectorySeparatorChar),[StringComparison]::OrdinalIgnoreCase)){throw 'Fault mutation escaped its owned game fixture'}
   $held.handle=[IO.File]::Open($path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
  }.GetNewClosure()
  try{$result=Invoke-NeuroticSimpleInstall (Join-Path $game 'FixtureGame.exe') $package 'dxgi.dll' $false @() '' $null $null $script:LoggingDefaults}
  finally{if($held.handle){$held.handle.Dispose();$held.handle=$null};$script:LoggingFault=$null}
  $durable=(Read-NeuroticInstallReceipt $game).receipt.loggingPolicy
  Assert-Logging ($result.status -ceq 'Partial' -and [IO.File]::ReadAllText($path) -ceq $text -and $durable.status -ceq 'Failed' -and -not $durable.patched) 'real Windows write-sharing refusal is reported, not labeled successful Trace'
 }
 Test-LoggingCase 'unknown package logging profile is reported and preserves bytes' {
  Set-FixtureLoggingProfile $package 'future-policy';$game=New-SimpleGameFixture 'logging-unknown';$path=Join-Path $game 'OptiScaler.ini'
  $text="[Log]`r`nLogToFile=auto`r`nLogLevel=2`r`n";[IO.File]::WriteAllText($path,$text)
  $result=Invoke-NeuroticSimpleInstall (Join-Path $game 'FixtureGame.exe') $package 'dxgi.dll' $false @() '' $null $null $script:LoggingDefaults
  Assert-Logging ($result.status -ceq 'SucceededWithNotes' -and [IO.File]::ReadAllText($path) -ceq $text -and $result.loggingPolicy.status -ceq 'UnknownProfile') 'unknown policy is explicit and never inferred from version or package name'
 }
 Test-LoggingCase 'owned INI update and explicit FreshInstall preserve their ownership and NR-off contracts' {
  Set-FixtureLoggingProfile $package '';$game=New-SimpleGameFixture 'logging-owned';$path=Join-Path $game 'OptiScaler.ini'
  $base=Invoke-NeuroticSimpleInstall (Join-Path $game 'FixtureGame.exe') $package 'dxgi.dll' $false @() '' $null $null $script:LoggingDefaults
  Assert-Logging ($base.status -like 'Succeeded*') 'owned update base install succeeds'
  [IO.File]::WriteAllText($path,"[DlssNr]`r`nEnabled=true`r`n[Log]`r`nLogToFile=auto`r`nLogLevel=2`r`n")
  Set-FixtureLoggingProfile $package 'internal-testing'
  $result=Invoke-NeuroticSimpleInstall (Join-Path $game 'FixtureGame.exe') $package 'dxgi.dll' $false @() '' $null $null $script:LoggingDefaults
  Assert-Logging ($result.status -like 'Succeeded*' -and [IO.File]::ReadAllText($path) -match 'Enabled=true' -and @($result.receipt.files|Where-Object{$_.path -ieq 'OptiScaler.ini' -and $_.removeOnUninstall}).Count -eq 1 -and $result.loggingPolicy.action -ceq 'Reuse') 'already owned INI remains owned and update preserves NR choice'
  $result=Invoke-NeuroticSimpleInstall (Join-Path $game 'FixtureGame.exe') $package 'dxgi.dll' $true @() '' $null $null $script:LoggingDefaults
  $text=[IO.File]::ReadAllText($path)
  Assert-Logging ($result.status -like 'Succeeded*' -and $text -match 'Enabled=false' -and $text -match 'LogToFile=true' -and $text -match 'LogLevel=0' -and $result.loggingPolicy.action -ceq 'Copy') 'explicit fresh replacement stays NR off and records copied Trace configuration'
 }
 Test-LoggingCase 'public fresh payload remains public logging and NR off' {
  $payload=Join-Path $package.root 'payload/OptiScaler.ini';$prior=[IO.File]::ReadAllBytes($payload)
  try{
   [IO.File]::WriteAllText($payload,"[DlssNr]`r`nEnabled=false`r`n[Log]`r`nLogToFile=false`r`nLogLevel=2`r`n")
   Set-FixtureLoggingProfile $package 'public';$game=New-SimpleGameFixture 'logging-public-fresh'
   $result=Invoke-NeuroticSimpleInstall (Join-Path $game 'FixtureGame.exe') $package 'dxgi.dll' $false @() '' $null $null $script:LoggingDefaults
   $text=[IO.File]::ReadAllText((Join-Path $game 'OptiScaler.ini'))
   Assert-Logging ($result.status -like 'Succeeded*' -and $text -match 'Enabled=false' -and $text -match 'LogToFile=false' -and $text -match 'LogLevel=2' -and $result.loggingPolicy.status -ceq 'Preserved') 'public selected defaults are not converted to internal Trace'
  }finally{[IO.File]::WriteAllBytes($payload,$prior)}
 }
 Test-LoggingCase 'hard-linked INI is refused without claiming or changing either name' {
  Set-FixtureLoggingProfile $package 'internal-testing';$game=New-SimpleGameFixture 'logging-hardlink';$path=Join-Path $game 'OptiScaler.ini';$other=Join-Path $game 'other.ini'
  $text="[Log]`r`nLogToFile=auto`r`nLogLevel=2`r`n";[IO.File]::WriteAllText($other,$text)
  # Windows PowerShell 5.1's provider refused this fixture before product code.
  # Construct the real NTFS link through its public API; never skip the refusal.
  if(-not ('NeuRoticLoggingFixtureLinks' -as [type])){
   Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class NeuRoticLoggingFixtureLinks {
 [DllImport("kernel32.dll", EntryPoint="CreateHardLinkW", ExactSpelling=true, CharSet=CharSet.Unicode, SetLastError=true)]
 [return: MarshalAs(UnmanagedType.Bool)]
 public static extern bool CreateHardLinkW(string name, string existing, IntPtr attributes);
}
'@
  }
  if(-not [NeuRoticLoggingFixtureLinks]::CreateHardLinkW($path,$other,[IntPtr]::Zero)){
   throw (New-Object ComponentModel.Win32Exception([Runtime.InteropServices.Marshal]::GetLastWin32Error()))
  }
  $result=Invoke-NeuroticSimpleInstall (Join-Path $game 'FixtureGame.exe') $package 'dxgi.dll' $false @() '' $null $null $script:LoggingDefaults
  Assert-Logging ($result.status -ceq 'FailedWithoutChanges' -and [IO.File]::ReadAllText($path) -ceq $text -and [IO.File]::ReadAllText($other) -ceq $text) 'actual NTFS hard link is preserved and cannot become a logging-write target'
 }
 Test-LoggingCase 'internal policy coexists with ReShade requirement and preserves existing NR defaults' {
  Set-FixtureLoggingProfile $package 'internal-testing';$game=New-SimpleGameFixture 'logging-reshade';$path=Join-Path $game 'OptiScaler.ini'
  [IO.File]::WriteAllText((Join-Path $game 'dxgi.dll'),'owned fixture foreign proxy bytes')
  [IO.File]::WriteAllText($path,"[DlssNr]`r`nEnabled=true`r`nRoute=0`r`nStyle=2`r`n[Plugins]`r`nLoadReshade=false`r`n[Log]`r`nLogToFile=auto`r`nLogLevel=2`r`n")
  $defaults=[pscustomobject]@{status='Loaded';preferences=[pscustomobject]@{schemaVersion=1;nrMode=3;style=1};note=''}
  $result=Invoke-NeuroticSimpleInstall (Join-Path $game 'FixtureGame.exe') $package 'dxgi.dll' $false @() 'RenameReShade' $null $null $defaults
  $text=[IO.File]::ReadAllText($path)
  Assert-Logging ($result.status -like 'Succeeded*' -and [IO.File]::ReadAllText((Join-Path $game 'ReShade64.dll')) -ceq 'owned fixture foreign proxy bytes' -and $text -match 'LoadReshade=true' -and $text.Contains("Enabled=true`r`nRoute=0`r`nStyle=2") -and $text -match 'LogToFile=true' -and $text -match 'LogLevel=0') 'one guarded rewrite combines explicit ReShade patch and Trace without resetting existing rendering preferences'
 }
 Test-LoggingCase 'ordinary actual Hub save preserves Trace and deliberate logging edit remains allowed' {
  Set-FixtureLoggingProfile $package 'internal-testing';$game=New-SimpleGameFixture 'logging-save';$path=Join-Path $game 'OptiScaler.ini'
  [IO.File]::WriteAllText($path,"[DlssNr]`r`nEnabled=true`r`nStyle=2`r`n[Log]`r`nLogToFile=auto`r`nLogLevel=2`r`n")
  $result=Invoke-NeuroticSimpleInstall (Join-Path $game 'FixtureGame.exe') $package 'dxgi.dll' $false @() '' $null $null $script:LoggingDefaults
  Assert-Logging ($result.status -like 'Succeeded*') 'migration succeeds before actual Hub save'
  $target=[pscustomobject]@{directory=$game};$request=[pscustomobject]@{configRevision=(HashFile $path);settings=@([pscustomobject]@{section='DlssNr';key='Style';value='1'})}
  $save=Invoke-HubSettingsSave $target $request;$text=[IO.File]::ReadAllText($path)
  Assert-Logging ($save.status -ceq 'Succeeded' -and $text -match 'Style=1' -and $text -match 'LogToFile=true' -and $text -match 'LogLevel=0') 'unrelated actual Hub save round-trips explicit Trace'
  $request.configRevision=HashFile $path;$request.settings=@([pscustomobject]@{section='Log';key='LogToFile';value='false'})
  $save=Invoke-HubSettingsSave $target $request
  Assert-Logging ($save.status -ceq 'Succeeded' -and [IO.File]::ReadAllText($path) -match 'LogToFile=false') 'later explicit user logging edits are not permanently overridden'
 }
 Write-Output "INTERNAL_LOGGING_INSTALL cases=$script:LoggingCases failures=$script:LoggingFailures privateRuntime=0 fakePEExecuted=0"
}finally{End-SimpleFixtures}
if($script:LoggingFailures){exit 1}
