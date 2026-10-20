$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'FixtureCommon.ps1')
$taskRoot=[IO.Path]::GetFullPath((Join-Path $script:Repo 'builds/hub-update-fixture'))
if(Test-Path -LiteralPath $taskRoot){throw 'Inspect the existing application-update fixture before reuse'}
New-Item -ItemType Directory -Path $taskRoot|Out-Null
$install=Join-Path $taskRoot 'installed';$patch=Join-Path $taskRoot 'patch';$persistent=Join-Path $taskRoot 'AppData/NeuRotic/Hub/Runtime'
New-Item -ItemType Directory -Path $install,$patch,$persistent|Out-Null
function Put([string]$root,[string]$name,[string]$text){$path=Join-Path $root $name;New-Item -ItemType Directory -Path (Split-Path -Parent $path) -Force|Out-Null;[IO.File]::WriteAllText($path,$text);return (Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant()}
function Apply([switch]$Rollback,[int]$FailAfter=-1){$arguments=@('-NoProfile','-ExecutionPolicy','Bypass','-File',(Join-Path $patch 'Apply-AppUpdate.ps1'),'-InstallDirectory',$install);if($Rollback){$arguments+='-Rollback'};if($FailAfter -ge 0){$arguments+=@('-TestFailAfter',[string]$FailAfter)};$prior=$ErrorActionPreference;try{$ErrorActionPreference='Continue';& $script:PowerShell @arguments *> (Join-Path $taskRoot 'update.log');return $LASTEXITCODE}finally{$ErrorActionPreference=$prior}}
try {
 foreach($name in @('Apply-AppUpdate.ps1','NeuRotic-AppUpdateSafety.cs')){Copy-Item -LiteralPath (Join-Path $script:Repo ('tools/hub/'+$name)) -Destination $patch}
 $oldApp=Put $install 'NeuRotic.Hub.exe' 'previous app';$newApp=Put (Join-Path $patch 'NeuRotic') 'NeuRotic.Hub.exe' 'updated app'
 $oldSupport=Put $install 'support/file.ps1' 'old support';$newSupport=Put (Join-Path $patch 'NeuRotic') 'support/file.ps1' 'new support'
 $oldManual=Put $install 'NeuRotic-Setup.cmd' 'old manual launcher';$newManual=Put (Join-Path $patch 'NeuRotic') 'NeuRotic-Manual-Setup.cmd' 'new manual launcher'
 $dependency=Put $install 'Tools/worker.dll' 'matched dependency'
 $model=Put $persistent 'Models/nvngx_dlssnr.dll' 'private persistent model';$settings=Put $install 'library.json' 'user library';$private=Put $install 'models/nvngx_dlssnr.dll' 'private legacy model'
 $manifest=[ordered]@{schemaVersion=1;kind='neurotic-app-delta';id='1234567890abcdef';dependencies=@(@{path='Tools/worker.dll';sha256=$dependency});files=@(
  @{path='NeuRotic.Hub.exe';action='put';sha256=$newApp;previousSha256=$oldApp},@{path='support/file.ps1';action='put';sha256=$newSupport;previousSha256=$oldSupport},
  @{path='NeuRotic-Manual-Setup.cmd';action='put';sha256=$newManual;previousSha256=$null},@{path='NeuRotic-Setup.cmd';action='remove';sha256=$null;previousSha256=$oldManual})}
 [IO.File]::WriteAllText((Join-Path $patch 'APP-UPDATE-MANIFEST.json'),($manifest|ConvertTo-Json -Depth 6))
 $env:NEUROTIC_APP_UPDATE_TEST_ROOT=$install
 Assert ((Apply -FailAfter 1) -ne 0) 'APP-UPDATE: injected failure reports failure'
 Assert ((Get-FileHash (Join-Path $install 'NeuRotic.Hub.exe')).Hash -ieq $oldApp) 'APP-UPDATE: interrupted update restores the previous app'
 Assert ((Apply) -eq 0) 'APP-UPDATE: normal delta applies successfully'
 Assert ((Get-FileHash (Join-Path $install 'NeuRotic.Hub.exe')).Hash -ieq $newApp) 'APP-UPDATE: updated app has the exact intended bytes'
 Assert ((Test-Path (Join-Path $install 'NeuRotic-Manual-Setup.cmd')) -and -not (Test-Path (Join-Path $install 'NeuRotic-Setup.cmd'))) 'APP-UPDATE: obsolete manual script is replaced by its clear new name'
 Assert ((Apply) -eq 0) 'APP-UPDATE: reapplying the same committed update is safe'
 $external=Put $install 'NeuRotic.Hub.exe' 'external app change'
 Assert ((Apply) -ne 0 -and (Get-FileHash (Join-Path $install 'NeuRotic.Hub.exe')).Hash -ieq $external) 'APP-UPDATE: changed committed content is preserved instead of claiming current'
 [void](Put $install 'NeuRotic.Hub.exe' 'updated app')
 Assert ((Get-FileHash (Join-Path $persistent 'Models/nvngx_dlssnr.dll')).Hash -ieq $model) 'APP-UPDATE: persistent private NR bytes survive application update'
 Assert ((Get-FileHash (Join-Path $install 'library.json')).Hash -ieq $settings -and (Get-FileHash (Join-Path $install 'models/nvngx_dlssnr.dll')).Hash -ieq $private) 'APP-UPDATE: legacy model and library settings are preserved'
 Assert ((Apply -Rollback) -eq 0) 'APP-UPDATE: explicit rollback succeeds'
 Assert ((Get-FileHash (Join-Path $install 'NeuRotic.Hub.exe')).Hash -ieq $oldApp -and (Test-Path (Join-Path $install 'NeuRotic-Setup.cmd'))) 'APP-UPDATE: rollback restores previous app and script names'
 [IO.File]::WriteAllText((Join-Path $install 'Tools/worker.dll'),'different worker')
 Assert ((Apply) -ne 0 -and (Get-FileHash (Join-Path $install 'NeuRotic.Hub.exe')).Hash -ieq $oldApp) 'APP-UPDATE: incompatible dependency refuses without changing the app'
 Assert ((Get-FileHash (Join-Path $persistent 'Models/nvngx_dlssnr.dll')).Hash -ieq $model) 'APP-UPDATE: refusal and rollback preserve persistent private files'
 [void](Put $install 'Tools/worker.dll' 'matched dependency')
 Assert ((Apply) -eq 0) 'APP-UPDATE: another installation remains supported after rollback'
 [void](Put $install 'Tools/worker.dll' 'unrelated dependency changed after update')
 $savedPatch=$patch;$patch=Join-Path $install '.neurotic-app-update/1234567890abcdef/recovery-tools'
 $removedDownload=Join-Path $taskRoot 'download-removed';Move-Item -LiteralPath $savedPatch -Destination $removedDownload
 Assert ((Apply -Rollback) -eq 0) 'APP-UPDATE: local recovery succeeds without its download and with an unrelated dependency change'
 Assert ((Get-FileHash (Join-Path $install 'NeuRotic.Hub.exe')).Hash -ieq $oldApp) 'APP-UPDATE: self-contained recovery restores original app bytes'
}finally{
 Remove-Item Env:NEUROTIC_APP_UPDATE_TEST_ROOT -ErrorAction SilentlyContinue
 if($taskRoot -ne [IO.Path]::GetFullPath((Join-Path $script:Repo 'builds/hub-update-fixture'))){throw 'Unexpected update fixture cleanup root'}
 foreach($entry in Get-ChildItem -LiteralPath $taskRoot -Recurse -Force){if($entry.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked update fixture cleanup refused'}}
 Remove-Item -LiteralPath $taskRoot -Recurse -Force
}
