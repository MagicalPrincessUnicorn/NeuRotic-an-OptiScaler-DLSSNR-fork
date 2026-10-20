$ErrorActionPreference='Stop'
$repo=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$builder=Join-Path $repo 'tools/packaging/customer/Build-Public-InstallerPackage.ps1'
$tokens=$null;$errors=$null;$ast=[Management.Automation.Language.Parser]::ParseFile($builder,[ref]$tokens,[ref]$errors)
foreach($definition in $ast.FindAll({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst]},$false)){. ([scriptblock]::Create($definition.Extent.Text))}
$source=[IO.File]::ReadAllText((Join-Path $repo 'integration/OptiScaler.ini'))
Assert-PublicLoggingDefaults $source
Write-Output 'PASS public integration defaults disable diagnostic logging'
foreach($key in @('LogToFile','LogToConsole','LogToNGX','LogToDebug','OpenConsole')){
 $enabled=$source -replace ('(?m)^'+$key+'\s*=.*$'),($key+' = true')
 $refused=$false;try{Assert-PublicLoggingDefaults $enabled}catch{$refused=$true}
 if(-not $refused){throw ('Public package accepted enabled '+$key)}
 Write-Output ('PASS public builder refuses enabled '+$key)
 $config=[IO.File]::ReadAllText((Join-Path $repo 'OptiScaler/Config.h'))
 if($config -notmatch ('CustomOptional<bool>\s+'+$key+'\s*\{\s*false\s*\}')){throw ('Automatic logging default changed: '+$key)}
}
$duplicate=$source+"`n[Log]`nLogToFile=false`n"
$refused=$false;try{Assert-PublicLoggingDefaults $duplicate}catch{$refused=$true}
if(-not $refused){throw 'Public package accepted ambiguous Log section'}
Write-Output 'PASS public builder refuses ambiguous logging policy'
