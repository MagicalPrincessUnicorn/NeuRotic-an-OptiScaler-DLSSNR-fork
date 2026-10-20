. (Join-Path $PSScriptRoot 'SimpleInstallerFixtureCommon.ps1')
Start-SimpleFixtures
try{
 . (Join-Path $script:SimpleSupport 'NeuRotic-InstallCore.ps1')
 $package=New-SimplePackageFixture
 function Set-Legacy($Game,$Records){
  $state=[ordered]@{kind='neurotic-public-install';schema_version=2;status='installed-verified';game_directory='D:\old-drive\Game';game_executable='D:\old-drive\Game\FixtureGame.exe';selected_proxy='dxgi.dll';target_bit_size=64;restore_chain=@('D:\old-drive\Game\NeuRotic-backups\missing\INSTALL-MANIFEST.json');restore_record_ledger=@($Records|ForEach-Object{[ordered]@{record_json=($_|ConvertTo-Json -Depth 8 -Compress)}})}
  $folder=Join-Path $Game 'NeuRotic/Installer';New-Item -ItemType Directory -Path $folder -Force|Out-Null
  [IO.File]::WriteAllText((Join-Path $folder 'Current-Install.json'),($state|ConvertTo-Json -Depth 10))
 }
 $game=New-SimpleGameFixture 'legacy-backups'
 $record=[pscustomobject]@{kind='neurotic-customer-candidate-install';files=@([pscustomobject]@{path='dependency.dll';operation='copy-package';installed_hash=('a'*64)},[pscustomobject]@{path='../outside.dll';operation='copy-package'},[pscustomobject]@{path='OptiScaler/CharacterInspector/worker.py';operation='copy-package'});reshade_operation=[pscustomobject]@{original_name='dxgi.dll';new_name='ReShade64.dll'}}
 Set-Legacy $game @($record)
 $backup=Join-Path $game 'NeuRotic-backups/keep/previous';New-Item -ItemType Directory -Path $backup -Force|Out-Null
 $original=Join-Path $backup 'original.dll';[IO.File]::WriteAllText($original,'historical original')
 $locked=[IO.File]::Open($original,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
 function HashFile{throw 'Installed/backup content hashing is forbidden in legacy import'}
 try{
  $read=Read-NeuroticInstallReceipt $game
  Assert-Simple ($read.receipt.schemaVersion -eq 3 -and $read.receipt.files.path -contains 'dependency.dll' -and $read.receipt.files.path -notcontains '../outside.dll') 'LegacyWithBackupsMigratesMetadataOnly'
  Assert-Simple ($read.receipt.reshadeRename.to -eq 'ReShade64.dll' -and $read.receipt.files.path -notcontains 'OptiScaler/CharacterInspector/worker.py') 'recorded rename imported; old Inspector remains inert'
 }finally{$locked.Dispose()}
 foreach($name in @('missing-history','malformed-history','pending-install','pending-uninstall')){
  $g=New-SimpleGameFixture $name;Set-Legacy $g @()
  if($name -eq 'malformed-history'){[IO.File]::WriteAllText((Join-Path $g 'NeuRotic/Installer/Current-Install.json'),'{ malformed metadata')}
  if($name -match '^pending-'){[IO.File]::WriteAllText((Join-Path $g ('NeuRotic/Installer/'+$(if($name -eq 'pending-install'){'Install-Transaction.json'}else{'Uninstall-Transaction.json'}))),'{ old pending state')}
  $result=Invoke-NeuroticSimpleInstall (Join-Path $g 'FixtureGame.exe') $package 'dxgi.dll' $false @() '' $null $null
  Assert-Simple ($result.status -in @('Succeeded','SucceededWithNotes') -and [IO.File]::Exists((Join-Path $g 'dxgi.dll'))) ($name+' does not gate Install')
 }
 $g=New-SimpleGameFixture 'recognized';Copy-Item -LiteralPath (Join-Path $script:SimpleRepo 'builds/hub/simple-loader/OptiScaler.dll') -Destination (Join-Path $g 'winmm.dll')
 [IO.File]::WriteAllText((Join-Path $g 'dxgi.dll'),'foreign proxy');New-Item -ItemType Directory -Path (Join-Path $g 'NeuRotic-backups/keep') -Force|Out-Null
 [IO.File]::WriteAllText((Join-Path $g 'NeuRotic-backups/keep/old.txt'),'keep')
 $result=Invoke-NeuroticSimpleUninstall $g
 Assert-Simple (-not [IO.File]::Exists((Join-Path $g 'winmm.dll'))) 'MissingReceiptUninstallRemovesRecognizableLoader'
 Assert-Simple ([IO.File]::ReadAllText((Join-Path $g 'dxgi.dll')) -eq 'foreign proxy' -and @($result.notes|Where-Object{$_ -like '*Ambiguous*'}).Count) 'AmbiguousSharedFilesAreLeftWithNote'
 Assert-Simple ([IO.File]::Exists((Join-Path $g 'NeuRotic-backups/keep/old.txt')) -and [IO.File]::ReadAllText($original) -eq 'historical original') 'OldFoldersStayInert'
 $g=New-SimpleGameFixture 'completed-legacy';Set-Legacy $g @();$statePath=Join-Path $g 'NeuRotic/Installer/Current-Install.json';$state=Get-Content -LiteralPath $statePath -Raw|ConvertFrom-Json;$state.status='uninstalled';[IO.File]::WriteAllText($statePath,($state|ConvertTo-Json -Depth 8));[IO.File]::WriteAllText((Join-Path $g 'dxgi.dll'),'new foreign proxy')
 $result=Invoke-NeuroticSimpleUninstall $g
 Assert-Simple ([IO.File]::ReadAllText((Join-Path $g 'dxgi.dll')) -eq 'new foreign proxy') 'completed legacy uninstall cannot claim a newer foreign loader'
 $g=New-SimpleGameFixture 'movable';$result=Invoke-NeuroticSimpleInstall (Join-Path $g 'FixtureGame.exe') $package 'dxgi.dll' $false @() '' $null $null
 $moved=Join-Path $script:SimpleWork 'moved game &mods';Move-Item -LiteralPath $g -Destination $moved
 $start=New-Object Diagnostics.ProcessStartInfo;$start.FileName=Join-Path $env:SystemRoot 'System32/cmd.exe';$start.Arguments='/d /s /c ""'+(Join-Path $moved 'Uninstall NeuRotic.cmd')+'""';$start.UseShellExecute=$false;$start.CreateNoWindow=$true;$start.RedirectStandardInput=$true;$start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
 $start.EnvironmentVariables['PSModulePath']=Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/Modules'
 $child=[Diagnostics.Process]::Start($start)
 try{$stdout=$child.StandardOutput.ReadToEndAsync();$stderr=$child.StandardError.ReadToEndAsync();$child.StandardInput.WriteLine();$child.StandardInput.Close();$child.WaitForExit();$exit=$child.ExitCode;[IO.File]::WriteAllText((Join-Path $script:SimpleWork 'local-uninstall.log'),$stdout.Result+$stderr.Result);if($exit){Write-Output ($stdout.Result+$stderr.Result)}}finally{$child.Dispose()}
 Assert-Simple ($exit -eq 0 -and -not [IO.File]::Exists((Join-Path $moved 'dxgi.dll')) -and [IO.File]::Exists((Join-Path $moved 'foreign-mod.txt'))) 'actual game-local CMD uninstall works after moving game without App'
}finally{End-SimpleFixtures}
