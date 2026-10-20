param([string]$EvidenceDirectory='docs/implementation-evidence/app-next-pass-20261004')
. (Join-Path $PSScriptRoot 'FixtureCommon.ps1')
$script:Work=Join-Path $script:Repo 'builds/hub/bulk-backend-fixture'
$script:Support=Join-Path $script:Work 'app/support'
$evidence=Join-Path $script:Repo $EvidenceDirectory
$priorFixture=$env:NEUROTIC_HUB_FIXTURE_ROOT
$driver=$null;$complete=$false;$step=0
function Write-BulkJson($path,$value){[IO.File]::WriteAllText($path,($value|ConvertTo-Json -Depth 30))}
function New-BulkGame([string]$name){
 $game=New-GameFixture $name;$exe=Join-Path $game 'FixtureGame.exe';$manager=Join-Path $game 'NeuRotic/Installer';New-Item -ItemType Directory -Force $manager|Out-Null
 $chain=@();$history=@()
 foreach($i in 0..2){
  $backup=Join-Path $game ('NeuRotic-backups/install-'+$i);New-Item -ItemType Directory -Force (Join-Path $backup 'previous')|Out-Null
  $files=@(foreach($target in @('dxgi.dll','OptiScaler.ini')){
   $before=if($i -eq 0){$name+' original '+$target}else{$name+' generation '+($i-1)+' '+$target};$after=$name+' generation '+$i+' '+$target
   $previous=Join-Path $backup ('previous/'+$target);[IO.File]::WriteAllText($previous,$before);$live=Join-Path $game $target;[IO.File]::WriteAllText($live,$after)
   [pscustomobject]@{path=$target;existed=$true;previous_hash=(Get-FileHash $previous).Hash;installed_hash=(Get-FileHash $live).Hash;operation='copy-package'}
  })
  $path=Join-Path $backup 'INSTALL-MANIFEST.json'
  Write-BulkJson $path ([ordered]@{kind='neurotic-customer-candidate-install';status='installed-verified';game_executable=$exe;backup=$backup;selected_proxy='dxgi.dll';files=$files;previous_record=$(if($chain.Count){$chain[-1]}else{$null});owned_files=@();restored_utc=$null;reconciled_paths=@();completed_utc='2026-10-04T12:00:00Z'})
  $chain+=$path;$history+=[pscustomobject]@{record=$path;install_id=[string]$i}
 }
 Write-BulkJson (Join-Path $manager 'Current-Install.json') ([ordered]@{kind='neurotic-public-install';schema_version=2;status='installed-verified';product='NeuRotic';release_name='Bulk inert fixture';commit=('a'*40);game_directory=$game;game_executable=$exe;selected_proxy='dxgi.dll';restore_chain=$chain;install_history=$history;manager_files=@();uninstalled_utc=$null;uninstall_mode=$null;first_install_utc='2026-10-01T12:00:00Z';last_install_utc='2026-10-04T12:00:00Z'})
 return $game
}
function DriverCall($message){
 $driver.StandardInput.WriteLine(($message|ConvertTo-Json -Depth 40 -Compress -EscapeHandling EscapeNonAscii));$driver.StandardInput.Flush();$line=$driver.StandardOutput.ReadLine()
 if(-not $line){throw ('Bulk driver ended unexpectedly: '+$driver.StandardError.ReadToEnd())}
 return $line|ConvertFrom-Json
}
function BackendRequest($request){
 $wire=[ordered]@{};foreach($property in $request.PSObject.Properties){$wire[$property.Name]=$property.Value}
 $result=HubCall $wire;$script:step++
 [IO.File]::AppendAllText((Join-Path $evidence 'bulk-backend-protocol.jsonl'),(([ordered]@{step=$script:step;request=$wire;receipt=$result}|ConvertTo-Json -Depth 50 -Compress)+[Environment]::NewLine))
 Copy-Item -LiteralPath (Join-Path $script:Work 'engine.log') -Destination (Join-Path $evidence ('bulk-engine-'+$script:step+'.log'))
 return $result
}
Init-HubFixtures
try{
 New-Item -ItemType Directory -Force $script:Support,$evidence|Out-Null
 foreach($source in Get-ChildItem -LiteralPath (Join-Path $script:Repo 'tools/packaging/customer/support') -File){Copy-Item -LiteralPath $source.FullName -Destination (Join-Path $script:Support $source.Name)}
 [IO.File]::WriteAllText((Join-Path $evidence 'bulk-backend-protocol.jsonl'),'')
 $games=@((New-BulkGame 'Alpha'),(New-BulkGame 'Beta'))
 $before=@{};foreach($game in $games){$before[$game]=(Get-FileHash (Join-Path $game 'dxgi.dll')).Hash}
 $start=New-Object Diagnostics.ProcessStartInfo;$start.FileName=Join-Path $script:Repo 'builds/hub/bulk-tests/BulkUninstallTests.exe';$start.Arguments='--protocol';$start.UseShellExecute=$false;$start.CreateNoWindow=$true;$start.RedirectStandardInput=$true;$start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
 $start.StandardInputEncoding=New-Object Text.UTF8Encoding($false);$start.StandardOutputEncoding=New-Object Text.UTF8Encoding($false);$start.StandardErrorEncoding=New-Object Text.UTF8Encoding($false)
 $driver=[Diagnostics.Process]::Start($start)
 $targets=@($games|ForEach-Object{[ordered]@{executable=(Join-Path $_ 'FixtureGame.exe');title=(Split-Path -Leaf $_)}})
 $view=DriverCall @{command='begin';targets=$targets};Assert $view.accepted 'Actual backend fixture inventory begins'
 while($view.phase -eq 'Inventory'){
  $next=DriverCall @{command='next'};Assert ($next.request.kind -eq 'Inspect') 'Before confirmation only Inspect is issued'
  $receipt=BackendRequest $next.request;$view=DriverCall @{command='accept';receipt=$receipt}
 }
 Assert ($view.phase -eq 'Review' -and @($view.rows|Where-Object status -eq 'Ready').Count -eq 2) 'Two actual backend inspections produce a concrete ready target list'
 foreach($game in $games){Assert ((Get-FileHash (Join-Path $game 'dxgi.dll')).Hash -eq $before[$game]) 'Inventory does not mutate installed files'}
 $view=DriverCall @{command='confirm';mode='Full'};Assert $view.accepted 'One Full confirmation binds both fixtures'
 $executed=0
 while($view.phase -eq 'Running'){
  $next=DriverCall @{command='next'};Assert ($null -ne $next.request) 'Sequential workflow emits one request'
  if($next.request.kind -eq 'Execute'){$executed++}
  $receipt=BackendRequest $next.request;$view=DriverCall @{command='accept';receipt=$receipt}
 }
 Write-BulkJson (Join-Path $evidence 'bulk-backend-final.json') $view
 Assert ($view.phase -eq 'Finished' -and $executed -eq 2 -and @($view.rows|Where-Object status -eq 'Uninstalled').Count -eq 2) 'Both confirmed targets uninstall through real Plan/fingerprint Execute receipts'
 foreach($game in $games){$name=Split-Path -Leaf $game
  foreach($target in @('dxgi.dll','OptiScaler.ini')){Assert ([IO.File]::ReadAllText((Join-Path $game $target)) -eq ($name+' original '+$target)) ('Original baseline restored for '+$name+' '+$target)}
  Assert (-not(Test-Path (Join-Path $game 'NeuRotic/Installer/Current-Install.json'))) ('Full removal clears owned install state for '+$name)
  Assert (-not(Test-Path (Join-Path $game 'NeuRotic-backups'))) ('Full removal clears verified owned backups for '+$name)
  Assert ([IO.File]::ReadAllText((Join-Path $game 'private.txt')) -eq 'untouched' -and [IO.File]::ReadAllText((Join-Path $game 'nvngx_dlssnr.dll')) -eq 'private model fixture') ('Unrelated/private files preserved for '+$name)
 }
 $complete=$true
}finally{
 if($driver){$driver.StandardInput.Close();$driver.WaitForExit();$driver.Dispose()}
 $env:NEUROTIC_HUB_FIXTURE_ROOT=$priorFixture
 if($complete){
  $resolved=[IO.Path]::GetFullPath($script:Work);if($resolved -ne [IO.Path]::GetFullPath((Join-Path $script:Repo 'builds/hub/bulk-backend-fixture'))){throw 'Unexpected bulk fixture cleanup scope'}
  foreach($entry in Get-ChildItem -LiteralPath $resolved -Recurse -Force){if($entry.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked bulk fixture cleanup refused'}}
  Remove-Item -LiteralPath $resolved -Recurse -Force
 }
}
