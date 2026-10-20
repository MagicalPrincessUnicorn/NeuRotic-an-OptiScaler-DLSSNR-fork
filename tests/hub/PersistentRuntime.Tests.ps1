$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'FixtureCommon.ps1')
$helper=Join-Path $script:Repo 'tools/packaging/customer/support/NeuRotic-RuntimeFiles.ps1'
Assert (Test-Path -LiteralPath $helper) 'NH-RUNTIME: persistent runtime authority exists'
. $helper
Init-HubFixtures
try {
 $app=Join-Path $script:Work 'old-app';New-Item -ItemType Directory -Force -Path (Join-Path $app 'streamline'),(Join-Path $app 'models')|Out-Null
 [IO.File]::WriteAllText((Join-Path $app 'streamline/sl.interposer.dll'),'first user file')
 [IO.File]::WriteAllText((Join-Path $app 'models/nvngx_dlssnr.dll'),'invalid DLL bytes')
 $root=Get-NeuroticRuntimeRoot
 Assert ($root -eq (Join-Path $env:NEUROTIC_HUB_FIXTURE_ROOT 'Runtime')) 'NH-RUNTIME: UI and installer authority is AppData Runtime'
 $outside=Join-Path $script:Work 'unrelated-runtime';New-Item -ItemType Directory -Path $outside|Out-Null
 $link=Join-Path $script:Work 'runtime-link';New-Item -ItemType Junction -Path $link -Target $outside|Out-Null
 try{$refused=$false;try{[void](Assert-NeuroticRuntimePath (Join-Path $link 'streamline/sl.interposer.dll'))}catch{$refused=$true};Assert $refused 'NH-RUNTIME: linked runtime ancestry refuses access'}finally{[IO.Directory]::Delete($link)}
 $state=Get-NeuroticRuntimeComponents $app
 Assert ($state.streamline.status -eq 'Partial') 'NH-RUNTIME: legacy Streamline is migrated once'
 Assert ($state.neuralModel.status -ne 'Present') 'NH-RUNTIME: filename alone never reports NR verified'
 $source=Join-Path $root 'streamline/sl.interposer.dll';$hash=(Get-FileHash -LiteralPath $source).Hash
 [IO.File]::WriteAllText((Join-Path $app 'streamline/sl.interposer.dll'),'different release file')
 [void](Get-NeuroticRuntimeComponents $app)
 Assert ((Get-FileHash -LiteralPath $source).Hash -eq $hash) 'NH-RUNTIME: migration never overwrites a persistent user DLL'
 $newApp=Join-Path $script:Work 'new-app';New-Item -ItemType Directory -Path $newApp|Out-Null
 $next=Get-NeuroticRuntimeComponents $newApp
 Assert ($next.streamline.status -eq 'Partial' -and $next.streamline.path -eq (Join-Path $root 'streamline')) 'NH-RUNTIME: a new release automatically reuses persistent files'
 $custom=Join-Path $script:Work 'custom/nvngx_dlssnr.dll';New-Item -ItemType Directory -Path (Split-Path -Parent $custom)|Out-Null;[IO.File]::WriteAllText($custom,'invalid custom model')
 [IO.File]::WriteAllText((Join-Path $root 'anything.json'),(@{schemaVersion=1;modelPath=$custom}|ConvertTo-Json))
 $override=Get-NeuroticRuntimeComponents $newApp
 Assert ($override.neuralModel.path -eq (Join-Path $root 'Place NVNGX DLSS NR File Here/nvngx_dlssnr.dll') -and $override.neuralModel.status -ne 'Present') 'NH-RUNTIME: legacy selection is checked for migration and readiness uses the named deposit'
 Assert ((Get-Content -LiteralPath (Join-Path $root 'anything.json') -Raw|ConvertFrom-Json).modelPath -eq $custom) 'NH-RUNTIME: failed validation preserves the user selection'
 # Read the preserved canonical private input; never export or overwrite it.
 $production=Join-Path $script:Repo 'builds/hub/bin';$worker=Find-NeuroticModelWorker $production
 Assert ([bool]$worker) 'NH-RUNTIME: production validator is available in the staged App'
 [IO.File]::Delete((Join-Path $root 'anything.json'))
 $oldPreferences=Join-Path $env:NEUROTIC_HUB_FIXTURE_ROOT 'Anything';New-Item -ItemType Directory -Path $oldPreferences|Out-Null
 $canonical=Join-Path (Split-Path -Parent $script:Repo) 'provider-baselines/20260926-canonical/nvngx_dlssnr.dll'
 [IO.File]::WriteAllText((Join-Path $oldPreferences 'anything.json'),(@{schemaVersion=1;modelPath=$canonical}|ConvertTo-Json))
 $verified=Get-NeuroticRuntimeComponents $production
 Assert ($verified.neuralModel.status -eq 'Present' -and $verified.neuralModel.path.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)) 'NH-RUNTIME: production validation imports legacy selection into persistent AppData'
 Assert ((Get-FileHash -LiteralPath $verified.neuralModel.path).Hash -eq (Get-FileHash -LiteralPath $canonical).Hash) 'NH-RUNTIME: persistent private model is an exact verified copy'
 $saved=Get-Content -LiteralPath (Join-Path $root 'anything.json') -Raw|ConvertFrom-Json
 Assert ($saved.modelPath -eq $verified.neuralModel.path) 'NH-RUNTIME: selector preference equals preflight validated path'
 # This check uses the actual validator-bearing App, matching the direct
 # production check above. The generic installer fixture intentionally has
 # only a tiny inert payload and cannot claim model validation readiness.
 $fixtureSupport=$script:Support
 try{$script:Support=Join-Path $production 'support';$result=HubCall ([ordered]@{kind='Preflight'})}finally{$script:Support=$fixtureSupport}
 Assert ($result.components.neuralModel.status -eq 'Present' -and $result.components.neuralModel.path -eq $saved.modelPath) 'NH-RUNTIME: startup UI and installer check agree on verified persistent file'
 $later=Get-NeuroticRuntimeComponents $production
 Assert ($later.neuralModel.path -eq $saved.modelPath) 'NH-RUNTIME: later check reuses the selection without a second migration'
} finally {End-HubFixtures}
