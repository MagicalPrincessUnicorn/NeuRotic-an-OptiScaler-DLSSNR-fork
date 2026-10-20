[CmdletBinding()]
param([string]$PackageRoot,[string]$BinRoot,[switch]$SupportOnly,[switch]$PlanOnly)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
. (Join-Path $root 'tools/build/Build.Context.ps1')
if(-not $BinRoot){$BinRoot=Join-Path $root 'builds/source/app/bin'}
$bin=[IO.Path]::GetFullPath($BinRoot);$support=Join-Path $bin 'support'
$owned=[IO.Path]::GetFullPath((Join-Path $root 'builds')).TrimEnd('\','/')+'\'
if(-not $bin.StartsWith($owned,[StringComparison]::OrdinalIgnoreCase)){throw 'BinRoot must stay in this checkout builds'}
$cursor=$support
while($cursor -and $cursor.Length -ge $root.Length){if(Test-Path -LiteralPath $cursor){if((Get-Item -LiteralPath $cursor).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked output ancestry'}};$cursor=Split-Path -Parent $cursor}
$names=@((Get-Content -LiteralPath (Join-Path $root 'tools/packaging/customer/SupportInventory.json') -Raw | ConvertFrom-Json).files)
if(-not $names.Count -or @($names|Select-Object -Unique).Count -ne $names.Count){throw 'Invalid canonical support inventory'}
Assert-NeuRoticOwnedOutput $root $bin -Tree
foreach($name in $names){if($name -notmatch '^[A-Za-z0-9_.-]+$'){throw 'Invalid support name'};Assert-NeuRoticOwnedOutput $root (Join-Path $support $name)}
foreach($name in @('Hub-Packages.json','NeuRotic-HubHistory.ps1','NeuRotic-HubSanitize.ps1','NeuRotic-HubDumbfire.ps1','NeuRotic-ConnectionDependencies.ps1')){Assert-NeuRoticOwnedOutput $root (Join-Path $support $name)}
foreach($name in $names){if(-not(Test-Path -LiteralPath (Join-Path $root ('tools/packaging/customer/support/'+$name)) -PathType Leaf)){throw ('Missing support input: '+$name)}}
if(-not $PackageRoot){$PackageRoot=Join-Path $root 'builds/release-bundle/staged/NeuRotic/packages/current'}
if($PlanOnly){[ordered]@{qualification='INTERNAL_DEVELOPMENT_PLAN_ONLY';bin=$bin;package=$PackageRoot;supportFiles=$names;dependencyStager='tools/hub/Stage-Dependencies.py'}|ConvertTo-Json -Depth 5;return}
if(-not $SupportOnly -and -not [IO.File]::Exists((Join-Path $PackageRoot 'support/PACKAGE-MANIFEST.json'))){throw 'Stage the current verified internal development assembly first'}
New-Item -ItemType Directory -Force -Path $support|Out-Null
foreach($name in $names){$destination=Join-Path $support $name;Assert-NeuRoticOwnedOutput $root $destination;Copy-Item -LiteralPath (Join-Path $root ('tools/packaging/customer/support/'+$name)) -Destination $destination -Force}
# Remove only known obsolete files from this generated support directory.
foreach($name in @('NeuRotic-HubHistory.ps1','NeuRotic-HubSanitize.ps1','NeuRotic-HubDumbfire.ps1','NeuRotic-ConnectionDependencies.ps1')){
 $path=Join-Path $support $name;if(Test-Path -LiteralPath $path){if((Get-Item -LiteralPath $path).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked generated support file'};Remove-Item -LiteralPath $path -Force}
}
if($SupportOnly){Write-Output 'Current support staged; package approval unchanged.';return}
if(-not $PackageRoot){$PackageRoot=Join-Path $root 'builds/release-bundle/staged/NeuRotic/packages/current'}
$package=[IO.Path]::GetFullPath($PackageRoot);$manifest=Join-Path $package 'support/PACKAGE-MANIFEST.json'
if(-not [IO.File]::Exists($manifest)){throw 'Stage the current verified native assembly first; no old accepted package is silently selected.'}
. (Join-Path $support 'NeuRotic-InstallReceipt.ps1')
$document=Get-Content -Raw -LiteralPath $manifest|ConvertFrom-Json
if($document.kind -ne 'neurotic-internal-development' -or $document.version -ne '0.9.9 DEVELOPMENT'){throw 'Only the selected internal 0.9.9 development package may be staged'}
if($document.components.characterInspector.relativeRoot -notmatch '^OptiScaler/CharacterInspector/Version [1-9][0-9]*$'){throw 'Staged package lacks required versioned Inspector'}
$seen=@{}
foreach($file in $document.files){
 $relative=ConvertTo-NeuroticRelativePath $file.path;if($seen.ContainsKey($relative)){throw 'Duplicate staged member'};$seen[$relative]=$true
 if($relative -match '(?i)(^|/)(Prepared|PreparedGuides|ReShade-shaders)/|(^|/)ReShade(?:32|64)?\.(dll|ini)$|\.addon(?:32|64)$'){throw 'Retired payload in staged native package'}
 $path=Resolve-NeuroticOwnedPath $package $relative
 if((Get-Item -LiteralPath $path).Length -ne $file.bytes -or (Get-FileHash -LiteralPath $path).Hash -ine $file.sha256){throw ('Staged package changed: '+$relative)}
}
$from=New-Object Uri(([IO.Path]::GetFullPath($support).TrimEnd('\')+'\'));$to=New-Object Uri($package)
$relativeRoot=[Uri]::UnescapeDataString($from.MakeRelativeUri($to).ToString())
$registry=[ordered]@{schemaVersion=1;packages=@([ordered]@{id='flagship-approved';name='Current verified native development assembly';relativeRoot=$relativeRoot;manifestSha256=(Get-FileHash $manifest -Algorithm SHA256).Hash})}
$registryPath=Join-Path $support 'Hub-Packages.json';Assert-NeuRoticOwnedOutput $root $registryPath
[IO.File]::WriteAllText($registryPath,($registry|ConvertTo-Json -Depth 5),(New-Object Text.UTF8Encoding($false)))
