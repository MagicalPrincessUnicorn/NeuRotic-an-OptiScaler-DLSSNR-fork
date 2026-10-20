param([string]$RepositoryRoot=(Split-Path -Parent (Split-Path -Parent $PSScriptRoot)))
$ErrorActionPreference='Stop'
. (Join-Path $RepositoryRoot 'tools/packaging/customer/support/NeuRotic-HubSettings.ps1')
function SafePath($root,$relative){Join-Path $root $relative}
function Assert($ok,$message){if(-not $ok){throw $message};Write-Output ('PASS '+$message)}
$work=Join-Path ([IO.Path]::GetTempPath()) ('NeuRotic-NR41-Memory03-'+[Guid]::NewGuid().ToString('N'))
if(Test-Path -LiteralPath $work){throw 'Existing fixture preserved'}
New-Item -ItemType Directory -Path $work -ErrorAction Stop|Out-Null
$expected=[IO.Path]::GetFullPath($work);$owner=Join-Path $work 'NR41-owner.txt';[IO.File]::WriteAllText($owner,$expected)
$ini=Join-Path $work 'OptiScaler.ini'
function MemoryEntries{@((Read-HubSettingsDocument $ini).entries|Where-Object{$_.section -ceq 'DlssNr' -and $_.key -ceq 'UiManualScale'})}
try{
 foreach($memory in @('2.5','99','future',$null)){
  foreach($choice in @('pre','post')){
   $line=if($null -eq $memory){''}else{'UiManualScale='+$memory+" ; retained`r`n"}
   [IO.File]::WriteAllText($ini,("[DlssNr]`r`nRoute=0`r`nRenderingMode=0`r`nWorkingScale=1`r`nUiManualResolution=false`r`n"+$line),(New-Object Text.UTF8Encoding($false)))
   $before=(Get-FileHash $ini).Hash
   $projection=@(Get-HubSettingsProjection $work|Where-Object{$_.key -ceq 'NativeResolutionChoice'})[0]
   Assert ($projection.value -ceq 'full' -and $projection.nativeManualScale -ceq '0.25' -and (Get-FileHash $ini).Hash -ceq $before) 'unknown or missing memory uses a read-only projection fallback'
   # No metadata means no actual Manual memory edit. The accompanying native
   # builder test checks that stage-only edits produce this request shape.
   $patch=[pscustomobject]@{section='DlssNr';key='NativeResolutionChoice';value=$choice}
   [IO.File]::WriteAllBytes($ini,(Get-HubSettingsBytes $work @($patch)))
   $entries=@(MemoryEntries)
   Assert ($(if($null -eq $memory){$entries.Count -eq 0}else{$entries.Count -eq 1 -and $entries[0].value -ceq $memory -and [IO.File]::ReadAllText($ini).Contains(' ; retained')})) ('stage-only '+$choice+' preserves raw remembered memory or absence')
   $edited=[pscustomobject]@{section='DlssNr';key='NativeResolutionChoice';value=$choice;nativeManualScale='0.5'}
   [IO.File]::WriteAllBytes($ini,(Get-HubSettingsBytes $work @($edited)))
   Assert (@(MemoryEntries)[0].value -ceq '0.5') 'an explicit changed Manual memory may replace the raw unknown value or absence'
   [IO.File]::WriteAllBytes($ini,(Get-HubSettingsBytes $work @([pscustomobject]@{section='DlssNr';key='NativeResolutionChoice';value='manual'})))
   Assert (@((Read-HubSettingsDocument $ini).entries|Where-Object{$_.section -ceq 'DlssNr' -and $_.key -ceq 'WorkingScale'})[0].value -ceq '0.5') 'Manual after automatic Save/reload restores the explicit changed memory'
  }
 }
 Write-Output 'PASS remembered Manual raw preservation writer contract; native builder and widget execution remain owner-05 QA'
}finally{
 if([IO.Path]::GetFullPath($work) -cne $expected -or [IO.File]::ReadAllText($owner) -cne $expected){throw 'Unowned cleanup refused'}
 if((Get-Item -LiteralPath $work -Force).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked root cleanup refused'}
 foreach($entry in Get-ChildItem -LiteralPath $work -Recurse -Force){if($entry.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked cleanup refused'}}
 Remove-Item -LiteralPath $work -Recurse -Force
}
