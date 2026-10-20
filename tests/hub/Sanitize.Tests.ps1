$ErrorActionPreference='Stop'
$repo=Split-Path -Parent (Split-Path -Parent $PSScriptRoot);$support=Join-Path $repo 'tools/packaging/customer/support'
function Assert($ok,$message){if(-not $ok){throw $message};Write-Output ('PASS '+$message)}
foreach($name in @('NeuRotic-Setup-Engine.ps1','NeuRotic-HubProtocol.ps1')){
 $tokens=$null;$errors=$null;$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $support $name),[ref]$tokens,[ref]$errors)
 foreach($definition in $ast.FindAll({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst]},$false)){. ([scriptblock]::Create($definition.Extent.Text))}
}
$module=Join-Path $support 'NeuRotic-HubSanitize.ps1';if(Test-Path $module){. $module}
$work=Join-Path $repo 'builds/hub/sanitize-fixture';if(Test-Path $work){throw 'Prior sanitizer fixture remains'}
New-Item -ItemType Directory -Path (Join-Path $work 'game/NeuRotic/Installer'),(Join-Path $work 'game/NeuRotic/Screenshots'),(Join-Path $work 'game/OptiScaler'),(Join-Path $work 'game/NeuRotic-backups/broken') -Force|Out-Null
try{
 $game=Join-Path $work 'game';$exe=Join-Path $game 'Fixture.exe';Copy-Item -LiteralPath (Join-Path $env:SystemRoot 'System32/cmd.exe') -Destination $exe
 foreach($entry in @{'dxgi.dll'='foreign';'winmm.dll'='trusted NR fixture';'NeuRotic-Setup.cmd'='NR script';'nvngx_dlssnr.dll'='private model';'OptiScaler.ini'='config';'user.txt'='keep';'NeuRotic/Installer/Current-Install.json'='broken history';'NeuRotic/Screenshots/shot.png'='photo';'OptiScaler/asset.bin'='owned asset';'NeuRotic-backups/broken/INSTALL-MANIFEST.json'='invalid manifest'}.GetEnumerator()){
  [IO.File]::WriteAllText((Join-Path $game $entry.Key),$entry.Value)
 }
 $target=Get-HubTarget $exe;$categories=@('rootFiles','assets','installer','backups','privateModels')
 foreach($appName in @('NeuRotic.exe','NeuRotic.Hub.exe')){
  $marker=Join-Path $game $appName;[IO.File]::WriteAllText($marker,'app marker')
  try{$refused=$false;try{Get-HubSanitizeInventory $target $categories @{}|Out-Null}catch{$refused=$_.Exception.Message -eq 'Application or volume root cannot be sanitized'};Assert $refused ('Sanitize protects App folder marked by '+$appName)}finally{[IO.File]::Delete($marker)}
 }
 $trusted=@{(HashFile (Join-Path $game 'winmm.dll'))='fixture approved package'}
 $allowedProxies=@('dxgi.dll','winmm.dll','version.dll','dbghelp.dll','d3d12.dll','wininet.dll','winhttp.dll','OptiScaler.asi','OptiScaler.dll')
 function Resolve-HubPackage($Id){throw 'Fixture intentionally has no approved package'}
 $backup=Join-Path $game 'NeuRotic-backups/broken';$manifest=Join-Path $backup 'INSTALL-MANIFEST.json'
 $record=[ordered]@{kind='neurotic-customer-candidate-install';status='installed-verified';game_executable=$exe;backup=$backup;files=@([ordered]@{path='winmm.dll';source='payload\OptiScaler.dll';operation='copy-package';installed_hash=(HashFile (Join-Path $game 'winmm.dll'))},[ordered]@{path='OptiScaler.ini';source='payload\OptiScaler.ini';operation='copy-package';installed_hash=(HashFile (Join-Path $game 'OptiScaler.ini'))})}
 [IO.File]::WriteAllText($manifest,($record|ConvertTo-Json -Depth 6))
 $historical=Get-HubSanitizeTrustedHashes $target
 Assert ($historical.ContainsKey((HashFile (Join-Path $game 'winmm.dll'))) -and -not $historical.ContainsKey((HashFile (Join-Path $game 'dxgi.dll')))) 'Independently valid old payload receipt identifies only its exact installed proxy hash'
 $record.game_executable=Join-Path $work 'OtherGame.exe';[IO.File]::WriteAllText($manifest,($record|ConvertTo-Json -Depth 6))
 Assert ((Get-HubSanitizeTrustedHashes $target).Count -eq 0) 'Other-game receipt cannot claim shared DLL ownership'
 $record.game_executable=$exe
 $record.files[0].source='payload\ReShade32.dll'
 [IO.File]::WriteAllText((Join-Path $game 'ReShade.ini'),'owned capture settings')
 $record.files+= [ordered]@{path='ReShade.ini';source='payload\ReShade.ini';operation='copy-package';existed=$false;installed_hash=(HashFile (Join-Path $game 'ReShade.ini'))}
 [IO.File]::WriteAllText($manifest,($record|ConvertTo-Json -Depth 6))
 $prepared=Get-HubSanitizeInventory $target $categories (Get-HubSanitizeTrustedHashes $target)
 Assert (@($prepared.files|Where-Object{$_.path -eq 'winmm.dll'}).Count -eq 1) 'Exact prepared proxy receipt permits permanent removal'
 Assert (@($prepared.files|Where-Object{$_.path -eq 'ReShade.ini'}).Count -eq 1) 'New owned unchanged ReShade config can be removed'
 [IO.File]::AppendAllText((Join-Path $game 'ReShade.ini'),' user change')
 $prepared=Get-HubSanitizeInventory $target $categories (Get-HubSanitizeTrustedHashes $target)
 Assert (@($prepared.files|Where-Object{$_.path -eq 'ReShade.ini'}).Count -eq 0) 'Changed ReShade config remains protected'
 [IO.File]::WriteAllText($manifest,'broken record')
 # Exercise the real fixed caps using small/temporary metadata, never a build
 # copy. Partial historical proof must be discarded at either limit.
 $bounds=Join-Path $work 'history-bounds';$boundsBase=Join-Path $bounds 'NeuRotic-backups';$boundsFirst=Join-Path $boundsBase 'first'
 [void][IO.Directory]::CreateDirectory($boundsFirst)
 $boundsTarget=[pscustomobject]@{directory=$bounds;executable=(Join-Path $bounds 'Fixture.exe')}
 $record.game_executable=$boundsTarget.executable;$record.backup=$boundsFirst
 [IO.File]::WriteAllText((Join-Path $boundsFirst 'INSTALL-MANIFEST.json'),($record|ConvertTo-Json -Depth 6))
 for($i=0;$i -lt 4096;$i++){[void][IO.Directory]::CreateDirectory((Join-Path $boundsBase ('empty-'+$i)))}
 Assert ((Get-HubSanitizeTrustedHashes $boundsTarget).Count -eq 0) '4096-record scan bound discards partial history proof without blocking cleanup'
 for($i=0;$i -lt 4096;$i++){[IO.Directory]::Delete((Join-Path $boundsBase ('empty-'+$i)))}
 $padding=(' ' * 4194302)+'{}'
 for($i=0;$i -lt 16;$i++){$directory=Join-Path $boundsBase ('metadata-'+$i);[void][IO.Directory]::CreateDirectory($directory);[IO.File]::WriteAllText((Join-Path $directory 'INSTALL-MANIFEST.json'),$padding)}
 Assert ((Get-HubSanitizeTrustedHashes $boundsTarget).Count -eq 0) '64 MiB metadata scan bound discards partial history proof without blocking cleanup'
 $padding=$null
 $snapshot=Get-HubSanitizeInventory $target $categories $trusted
 Assert (@($snapshot.files|Where-Object{$_.path -eq 'winmm.dll'}).Count -eq 1) 'Proven NR shared proxy is selected'
 Assert (@($snapshot.files|Where-Object{$_.path -eq 'dxgi.dll'}).Count -eq 0) 'Foreign shared proxy is preserved'
 Assert (@($snapshot.retained|Where-Object{$_.path -eq 'dxgi.dll'}).Count -eq 1) 'Foreign proxy exclusion has a review reason'
 $dxgl=Join-Path $game 'dxgl.dll';[IO.File]::WriteAllText($dxgl,'foreign dxgl')
 $dxglSnapshot=Get-HubSanitizeInventory $target $categories $trusted
 Assert (@($dxglSnapshot.files|Where-Object{$_.path -eq 'dxgl.dll'}).Count -eq 0 -and @($dxglSnapshot.retained|Where-Object{$_.path -eq 'dxgl.dll'}).Count -eq 1) 'Unrecognized dxgl.dll is preserved with the same shared-proxy policy'
 [IO.File]::WriteAllText($dxgl,'trusted NR fixture');$dxglSnapshot=Get-HubSanitizeInventory $target $categories $trusted
 Assert (@($dxglSnapshot.files|Where-Object{$_.path -eq 'dxgl.dll'}).Count -eq 1) 'dxgl.dll is removable only with exact NR payload hash proof'
 Assert (@($snapshot.files|Where-Object{$_.path -match 'Screenshots'}).Count -eq 0) 'Screenshots are kept by default'
 Assert (@($snapshot.files|Where-Object{$_.path -match 'Current-Install.json|NeuRotic-Setup.cmd|INSTALL-MANIFEST.json'}).Count -eq 3) 'Scripts and broken installation history remain sanitizable'
 Assert (@($snapshot.files|Where-Object{$_.path -eq 'user.txt' -or $_.path -eq 'Fixture.exe'}).Count -eq 0) 'Foreign user and game files stay outside cleanup'
 $preserved=Join-Path $game 'NeuRotic-preserved-files/changes-fixture';New-Item -ItemType Directory -Path (Join-Path $preserved 'f'),(Join-Path $preserved 'Screenshots') -Force|Out-Null
 [IO.File]::WriteAllText((Join-Path $preserved 'f/0000.bin'),'preserved external original');[IO.File]::WriteAllText((Join-Path $preserved 'FILES.json'),'preserved-file record');[IO.File]::WriteAllText((Join-Path $preserved 'Screenshots/kept.png'),'preserved screenshot')
 $preservedSnapshot=Get-HubSanitizeInventory $target $categories $trusted
 Assert (@($preservedSnapshot.files|Where-Object{$_.path -like 'NeuRotic-preserved-files\*'}).Count -eq 2) 'Backups category includes preserved external bytes and their FILES receipt'
 $keepBackups=Get-HubSanitizeInventory $target @('rootFiles','assets','installer','privateModels') $trusted
 Assert (@($keepBackups.files|Where-Object{$_.path -like 'NeuRotic-preserved-files\*'}).Count -eq 0 -and @($keepBackups.retained|Where-Object{$_.path -like 'NeuRotic-preserved-files\*' -and $_.reason -eq 'Category kept: backups'}).Count -eq 2) 'Backups opt-out keeps preserved originals and their record'
 Assert (@($preservedSnapshot.retained|Where-Object{$_.path -like 'NeuRotic-preserved-files\*' -and $_.reason -eq 'Category kept: screenshots'}).Count -eq 1) 'Screenshots inside preserved-file backups remain kept by default'
 $licenses=Join-Path $game 'Licenses';New-Item -ItemType Directory -Path $licenses|Out-Null
 [IO.File]::WriteAllText((Join-Path $licenses 'NR.txt'),'known license');[IO.File]::WriteAllText((Join-Path $licenses 'Game.txt'),'foreign license')
 $trusted['asset:Licenses\NR.txt:'+(HashFile (Join-Path $licenses 'NR.txt'))]='fixture exact asset proof'
 $assets=Get-HubSanitizeInventory $target $categories $trusted
 Assert (@($assets.files|Where-Object{$_.path -eq 'Licenses\NR.txt'}).Count -eq 1 -and @($assets.files|Where-Object{$_.path -eq 'Licenses\Game.txt'}).Count -eq 0) 'Shared license directory removes only a proven exact payload file'
 $screens=Get-HubSanitizeInventory $target @('screenshots') $trusted
 Assert (@($screens.files).Count -eq 2 -and @($screens.files|Where-Object{$_.path -match 'kept.png'}).Count -eq 1) 'Screenshots require explicit category selection'
 $refused=$false;try{Get-HubSanitizeInventory $target @('../escape') $trusted|Out-Null}catch{$refused=$true}
 Assert $refused 'Arbitrary category/path injection refused'
 $outside=Join-Path $work 'outside';New-Item -ItemType Directory -Path $outside|Out-Null
 [IO.File]::WriteAllText((Join-Path $outside 'keep.txt'),'outside')
 $junction=Join-Path $game 'OptiScaler/linked'
 New-Item -ItemType Junction -Path $junction -Target $outside|Out-Null
 try{$refused=$false;try{Get-HubSanitizeInventory $target $categories $trusted|Out-Null}catch{$refused=$true};Assert $refused 'Linked selected directory refuses the whole preview'}finally{[IO.Directory]::Delete($junction)}
 Assert ([IO.File]::ReadAllText((Join-Path $outside 'keep.txt')) -eq 'outside') 'Outside linked target is untouched'
 $models=Get-HubSanitizeInventory $target @('rootFiles','assets','installer','backups') $trusted
 Assert (@($models.files|Where-Object{$_.path -eq 'nvngx_dlssnr.dll'}).Count -eq 0) 'Private model category can be kept explicitly'
 Add-Type -Path (Join-Path $support 'NeuRotic-HubJson.cs')
 $valid=[ordered]@{protocolVersion=1;kind='Plan';operation='Sanitize';gameExecutable=$exe;sanitizeCategories=$categories;gameRoot=$game}
 Assert ((Read-HubRequestText ($valid|ConvertTo-Json)).operation -eq 'Sanitize') 'Typed plan accepts exact categories and standard game metadata'
 $valid.sanitizeCategories=@('rootFiles','rootFiles');$refused=$false;try{Read-HubRequestText ($valid|ConvertTo-Json)|Out-Null}catch{$refused=$true}
 Assert $refused 'Duplicate cleanup categories are refused'
 function Get-HubUserRoot { $path=Join-Path $work 'receipts';[void][IO.Directory]::CreateDirectory($path);return $path }
 function Get-HubSanitizeTrustedHashes($Target){return $trusted}
 $keepGame=Join-Path $work 'keep-backups';$keepDir=Join-Path $keepGame 'NeuRotic-preserved-files/changes-keep/f';[void][IO.Directory]::CreateDirectory($keepDir)
 $keepFile=Join-Path $keepDir '0000.bin';[IO.File]::WriteAllText($keepFile,'keep original');$keepHash=HashFile $keepFile
 $keepExe=Join-Path $keepGame 'Fixture.exe';Copy-Item -LiteralPath $exe -Destination $keepExe
 $keepRequest=[pscustomobject]@{kind='Plan';operation='Sanitize';gameExecutable=$keepExe;sanitizeCategories=@('assets')}
 $keepPlan=New-HubSanitizePlan $keepRequest;$keepPlanPath=Join-Path (Get-HubUserRoot) ($keepPlan.planId+'.plan.json')
 $keepResult=& {Set-StrictMode -Version 2.0;Invoke-HubSanitizeExecute ([pscustomobject]$keepPlan) (Get-HubTarget $keepExe) $keepPlanPath}
 Assert ($keepResult.status -eq 'Succeeded' -and (HashFile $keepFile) -eq $keepHash) 'Confirmed cleanup with Backups unchecked preserves original bytes, including an empty deletion plan'
 $request=[pscustomobject]@{kind='Plan';operation='Sanitize';gameExecutable=$exe;sanitizeCategories=$categories}
 $plan=New-HubSanitizePlan $request;$planPath=Join-Path (Get-HubUserRoot) ($plan.planId+'.plan.json')
 $oldDir=Join-Path $game 'OptiScaler';$renamed=Join-Path $game 'old-assets';Move-Item -LiteralPath $oldDir -Destination $renamed
 New-Item -ItemType Directory -Path $oldDir|Out-Null;Copy-Item -LiteralPath (Join-Path $renamed 'asset.bin') -Destination (Join-Path $oldDir 'asset.bin')
 $identity=Invoke-HubSanitizeExecute ([pscustomobject]$plan) $target $planPath
 Assert ($identity.status -eq 'PreconditionChanged') 'Same-content replacement directory invalidates reviewed identity'
 Remove-Item -LiteralPath (Join-Path $oldDir 'asset.bin');[IO.Directory]::Delete($oldDir);Move-Item -LiteralPath $renamed -Destination $oldDir
 $plan=New-HubSanitizePlan $request;$planPath=Join-Path (Get-HubUserRoot) ($plan.planId+'.plan.json')
 [IO.File]::AppendAllText((Join-Path $game 'OptiScaler.ini'),'external')
 $changed=Invoke-HubSanitizeExecute ([pscustomobject]$plan) $target $planPath
 Assert ($changed.status -eq 'PreconditionChanged' -and (Test-Path (Join-Path $game 'winmm.dll'))) 'Changed candidate cancels entire cleanup before deletion'
 $plan=New-HubSanitizePlan $request;$planPath=Join-Path (Get-HubUserRoot) ($plan.planId+'.plan.json')
 $success=Invoke-HubSanitizeExecute ([pscustomobject]$plan) $target $planPath
 Assert ($success.status -eq 'Succeeded' -and @($success.deleted).Count -eq 11) ('Confirmed fixture cleanup removes exactly eleven recognized files: '+($success|ConvertTo-Json -Depth 4 -Compress))
 Assert (-not (Test-Path (Join-Path $preserved 'f/0000.bin')) -and -not (Test-Path (Join-Path $preserved 'FILES.json')) -and (Test-Path (Join-Path $preserved 'Screenshots/kept.png'))) 'Confirmed default backup cleanup deletes preserved originals and record while keeping screenshots'
 Assert ((Test-Path (Join-Path $game 'dxgi.dll')) -and (Test-Path (Join-Path $game 'user.txt')) -and (Test-Path (Join-Path $game 'NeuRotic/Screenshots/shot.png'))) 'Cleanup keeps foreign DLL, unrelated user file and screenshots'
 Assert ((Get-Content -Raw $success.receipt|ConvertFrom-Json).status -eq 'Succeeded') 'Durable cleanup receipt records completion'
 # A delete-boundary failure after one completed removal must be reported as
 # partial, retaining exact progress and never claiming rollback.
 [IO.File]::WriteAllText((Join-Path $game 'OptiScaler.ini'),'first');[IO.File]::WriteAllText((Join-Path $game 'OptiScaler.log'),'second')
 $plan=New-HubSanitizePlan $request;$planPath=Join-Path (Get-HubUserRoot) ($plan.planId+'.plan.json')
 $ownerDelete=(Get-Command Remove-NeuroticPath).ScriptBlock;$script:deleteCount=0
 function Remove-NeuroticPath([string]$LiteralPath,[switch]$Force,[string]$ExpectedHash){
  if(++$script:deleteCount -eq 2){[IO.File]::AppendAllText($LiteralPath,'external edit')}
  & $ownerDelete -LiteralPath $LiteralPath -ExpectedHash $ExpectedHash
 }
 $partial=Invoke-HubSanitizeExecute ([pscustomobject]$plan) $target $planPath
 Assert ($partial.status -eq 'FailedWithChanges' -and @($partial.deleted).Count -eq 1 -and @($partial.remaining).Count -eq 1 -and $partial.pending) 'Boundary edit records honest partial cleanup and exact remaining item'
 Assert ([IO.File]::ReadAllText((Join-Path $game 'OptiScaler.log')).Contains('external edit')) 'CAS deletion preserves concurrent file modification'
 Assert ((Get-Content -Raw $partial.receipt|ConvertFrom-Json).pending -eq 'OptiScaler.log') 'Interrupted boundary is durably identifiable without automatic replay'
 # Attack the empty-directory leaf after inventory and the UI review. The
 # native delete primitive must reject a file at that path on its opened handle.
 $swap=Join-Path $game 'OptiScaler';New-Item -ItemType Directory -Path $swap|Out-Null
 $identity=[NeuRoticFileSafety]::Identity($swap)
 [IO.Directory]::Delete($swap);[IO.File]::WriteAllText($swap,'unreviewed replacement file')
 $refused=$false;try{Remove-HubSanitizeDirectory $swap $identity}catch{$refused=$true}
 Assert ($refused -and [IO.File]::ReadAllText($swap) -eq 'unreviewed replacement file') 'Directory-only native deletion refuses and preserves a replacement file'
 [IO.File]::Delete($swap);New-Item -ItemType Directory -Path $swap|Out-Null
 $refused=$false;try{Remove-HubSanitizeDirectory $swap $identity}catch{$refused=$true}
 Assert ($refused -and (Test-Path -LiteralPath $swap -PathType Container)) 'Directory delete checks expected identity on its actual opened handle'
 $identity=[NeuRoticFileSafety]::Identity($swap);[IO.File]::WriteAllText((Join-Path $swap 'late.txt'),'late')
 $refused=$false;try{Remove-HubSanitizeDirectory $swap $identity}catch{$refused=$true}
 Assert ($refused -and (Test-Path -LiteralPath (Join-Path $swap 'late.txt'))) 'Directory cleanup is nonrecursive and preserves a late-arriving file'
}finally{
 $resolved=[IO.Path]::GetFullPath($work);if($resolved -ne [IO.Path]::GetFullPath((Join-Path $repo 'builds/hub/sanitize-fixture'))){throw 'Unexpected sanitizer fixture root'}
 foreach($entry in Get-ChildItem -LiteralPath $resolved -Recurse -Force){if($entry.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked fixture cleanup refused'}}
 Remove-Item -LiteralPath $resolved -Recurse -Force
}
