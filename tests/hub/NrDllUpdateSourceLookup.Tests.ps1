param([Parameter(Mandatory=$true)][string]$SupportRoot,[Parameter(Mandatory=$true)][string]$FixtureRoot,[ValidateSet('skip','legacy')][string]$Case='skip')
$ErrorActionPreference='Stop'
try{
 $root=[IO.Path]::GetFullPath($FixtureRoot);if(-not $root.StartsWith('C:\NeuRotic\',[StringComparison]::OrdinalIgnoreCase)-or(Test-Path -LiteralPath $root)){throw 'new owned workspace child required'}
 [void][IO.Directory]::CreateDirectory($root);$env:NEUROTIC_HUB_FIXTURE_ROOT=$root
 . (Join-Path $SupportRoot 'NeuRotic-InstallCore.ps1');. (Join-Path $SupportRoot 'NeuRotic-RuntimeFiles.ps1')
 $runtime=Get-NeuroticRuntimeRoot;[void][IO.Directory]::CreateDirectory($runtime);$sentinel=Join-Path $root 'unavailable-selected-source/nvngx_dlssnr.dll'
 $cache=[ordered]@{schemaVersion=1;components=[ordered]@{streamline=[ordered]@{status='Missing';files=@()};neuralModel=[ordered]@{status='Present';path=$sentinel;fileIdentity='fixture';bytes=1;lastWriteTicks=1}}}
 [IO.File]::WriteAllText((Join-Path $runtime 'accepted-runtime.json'),($cache|ConvertTo-Json -Depth 10))
 $script:NrSourceReads=0;$script:RealRuntimePath=(Get-Item Function:Assert-NeuroticRuntimePath).ScriptBlock
 # Observe the existing path-validation boundary. Refuse the synthetic source
 # before any file stat; all accepted-cache reading and dispatch stay actual.
 function Assert-NeuroticRuntimePath([string]$Path){if($Path -ceq $sentinel){$script:NrSourceReads++;throw 'inert source-boundary witness'};return (& $script:RealRuntimePath $Path)}
 $value=Get-NeuroticAcceptedRuntimeComponents -IncludeNeuralModel:($Case -eq 'legacy')
 if($Case -eq 'legacy'){
  if($script:NrSourceReads -ne 1){throw 'legacy initial source observation control must reach the real boundary'}
 }elseif($script:NrSourceReads -ne 0){throw 'ordinary updates must not inspect any NR source'}
 [Console]::Out.WriteLine(('PASS NR source lookup boundary case={0} observations={1}; no source file/provider load' -f $Case,$script:NrSourceReads));exit 0
}catch{[Console]::Error.WriteLine(('FAIL '+$_.Exception.Message));exit 1}
