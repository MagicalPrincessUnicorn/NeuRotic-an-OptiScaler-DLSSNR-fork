param([string]$SupportRoot,[string]$Receipt)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if(-not $SupportRoot){$SupportRoot=Join-Path $repo 'tools/packaging/customer/support'}
. (Join-Path $SupportRoot 'NeuRotic-RuntimeFiles.ps1')
$work=[IO.Path]::GetFullPath((Join-Path $repo 'builds/audit-installer-runtime'))
if(Test-Path -LiteralPath $work){throw 'Inspect existing runtime fixture before reuse'}
$previous=$env:NEUROTIC_HUB_FIXTURE_ROOT;$checks=@()
function Check($okay,[string]$name){
 $script:checks+=,[pscustomobject]@{case=$name;passed=[bool]$okay}
 if(-not $okay){throw $name};Write-Output ('PASS '+$name)
}
try {
 $env:NEUROTIC_HUB_FIXTURE_ROOT=$work
 $app=Join-Path $work 'app';[void][IO.Directory]::CreateDirectory($app)
 $stream=Join-Path $work 'Runtime/streamline'
 $state=Get-NeuroticRuntimeComponents $app
 Check ($state.streamline.status -eq 'Missing') 'Empty folder is Missing'
 $names=@('sl.interposer.dll','sl.common.dll','sl.dlss_g.dll','sl.reflex.dll','sl.pcl.dll','nvngx_dlssg.dll')
 foreach($name in $names){[IO.File]::WriteAllText((Join-Path $stream $name),('user bytes '+$name))}
 $state=Get-NeuroticRuntimeComponents $app
 Check ($state.streamline.status -eq 'Present' -and $state.streamline.files.Count -eq 6) 'Matching filenames accepted without PE or pinned content checks'
 foreach($row in $state.streamline.files){Check ($row.sha256 -eq (Get-FileHash -LiteralPath $row.source).Hash) ('Copy identity records actual bytes: '+$row.path)}
 [IO.File]::WriteAllBytes((Join-Path $stream 'sl.common.dll'),[byte[]]@())
 $state=Get-NeuroticRuntimeComponents $app
 Check ($state.streamline.status -eq 'Present') 'Different and empty contents do not change filename recognition'
 $mixed=Join-Path $stream 'sl.reflex.dll';$temp=Join-Path $stream 'rename.tmp'
 [IO.File]::Move($mixed,$temp);[IO.File]::Move($temp,(Join-Path $stream 'SL.REFLEX.DLL'))
 Check ((Get-NeuroticRuntimeComponents $app).streamline.status -eq 'Present') 'Filename matching ignores case'
 $legacy=Join-Path $app 'streamline';[void][IO.Directory]::CreateDirectory($legacy)
 [IO.File]::WriteAllText((Join-Path $legacy 'sl.interposer.dll'),'release replacement')
 [void](Get-NeuroticRuntimeComponents $app)
 Check ([IO.File]::ReadAllText((Join-Path $stream 'sl.interposer.dll')) -eq 'user bytes sl.interposer.dll') 'Release migration preserves existing user files'
 $target=Join-Path $stream 'sl.pcl.dll';[IO.File]::Move($target,($target+'.wrong'))
 $state=Get-NeuroticRuntimeComponents $app
 Check ($state.streamline.status -eq 'Partial' -and $state.streamline.missing -contains 'sl.pcl.dll') 'Wrong name leaves the required file missing'
 Check (@(Get-NeuroticRuntimeProjection $app (Join-Path $work 'game') $false).Count -eq 0) 'Incomplete set is not projected for installation'
 [void][IO.Directory]::CreateDirectory($target)
 Check ((Get-NeuroticRuntimeComponents $app).streamline.status -eq 'Partial') 'A directory with the required name is not a file'
 [IO.Directory]::Delete($target);[IO.File]::Move(($target+'.wrong'),$target)
 $projection=@(Get-NeuroticRuntimeProjection $app (Join-Path $work 'game') $false)
 Check ($projection.Count -eq 6) 'All six user files project to the installer'
 $selected=@($projection|Where-Object{$_.path -like '*sl.common.dll'})[0]
 $before=$selected.installed_hash
 [IO.File]::WriteAllText($selected.source,'new user build')
 Check ($selected.installed_hash -eq $before -and $before -ne (Get-FileHash -LiteralPath $selected.source).Hash) 'Planned copy retains the original bytes identity after replacement'
 Check ((Get-NeuroticAcceptedRuntimeComponents).streamline.status -eq 'Unavailable') 'Changed files require refreshing the cached copy receipt'
 Check ((Get-NeuroticRuntimeComponents $app).streamline.status -eq 'Present') 'Refresh accepts the newly supplied contents'
} finally {
 if($Receipt){[IO.File]::WriteAllText([IO.Path]::GetFullPath($Receipt),($checks|ConvertTo-Json -Depth 4))}
 $env:NEUROTIC_HUB_FIXTURE_ROOT=$previous
 if(Test-Path -LiteralPath $work){
  if((Resolve-Path -LiteralPath $work).Path -ne [IO.Path]::GetFullPath((Join-Path $repo 'builds/audit-installer-runtime'))){throw 'Unexpected fixture cleanup root'}
  $pending=[Collections.Generic.Queue[string]]::new();$pending.Enqueue($work)
  while($pending.Count){$folder=$pending.Dequeue();foreach($item in @(Get-Item -LiteralPath $folder)+@(Get-ChildItem -LiteralPath $folder -Force)){if($item.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked cleanup member refused'};if($item.PSIsContainer -and $item.FullName -ne $folder){$pending.Enqueue($item.FullName)}}}
  Remove-Item -LiteralPath $work -Recurse -Force
 }
}
