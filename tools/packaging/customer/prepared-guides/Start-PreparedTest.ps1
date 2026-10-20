# Supports Steam launch options: this CMD followed by %command% passes literal argv.
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'PreparedProfile.ps1')
$command=@($args|ForEach-Object {[string]$_});if(-not $command.Count){throw 'Missing comparison mode'}
$literalLaunch=$command.Count -ge 2
$mode=$command[0];$check=$mode.StartsWith('Check');$mode=$mode.Replace('Check','')
if($mode -cnotin @('Prepared','Baseline','BuiltIn','Shared','TransportOnly','VulkanPrepared','VulkanBaseline')){throw 'Unknown test mode'}
$graphicsApi=$(if($mode -in @('VulkanPrepared','VulkanBaseline','Shared','TransportOnly')){'Vulkan'}else{'Proxy'})
if($mode -eq 'VulkanPrepared'){$mode='Prepared'}elseif($mode -eq 'VulkanBaseline'){$mode='Baseline'}
$root=Split-Path $PSScriptRoot -Parent
if($command.Count -lt 2){$last=Join-Path $root 'prepared-game.local.json';if(-not(Test-Path -LiteralPath $last)){throw 'Run NeuRotic-Setup.cmd first, or pass the game executable'};$command=@($command[0],(Get-Content -LiteralPath $last -Raw|ConvertFrom-Json).gameExecutable)}
$exe=(Resolve-Path -LiteralPath $command[1]).Path;$game=Split-Path $exe -Parent;$null=NrSafe $game ([IO.Path]::GetFileName($exe))
if([IO.Path]::GetExtension($exe) -ine '.exe'){throw 'The first game argument must be its EXE'}
$package=Join-Path $root 'packages\current';$manifest=Get-Content (Join-Path $package 'support\PACKAGE-MANIFEST.json') -Raw|ConvertFrom-Json
$product=@($manifest.files|Where-Object path -eq 'payload\OptiScaler.dll');if($product.Count -ne 1){throw 'Missing package product identity'}
$matched=@();foreach($name in @('dxgi.dll','winmm.dll','version.dll','dbghelp.dll','d3d12.dll','wininet.dll','winhttp.dll','OptiScaler.asi','OptiScaler.dll')){$path=NrSafe $game $name;if((Test-Path -LiteralPath $path -PathType Leaf) -and (NrHash $path) -eq $product[0].sha256){$matched+=$name}}
if($matched.Count -ne 1){throw 'Install this experimental core beside the selected game EXE first'}
foreach($file in $manifest.files){if($file.path -match '^payload\\(?:NeuRotic-PreparedGuides\.addon64|WinPixEventRuntime\.dll|ReShade64\.dll|OptiScaler\\PreparedGuides\\)'){$relative=$file.path.Substring(8);$path=NrSafe $game $relative;if(-not(Test-Path -LiteralPath $path -PathType Leaf) -or (NrHash $path) -ne $file.sha256){throw "Installed test component differs: $relative"}}}
if($check){Write-Output "PASS $mode launcher preflight; no profile, game or registry changes";return}
if(Get-Process -Name ([IO.Path]::GetFileNameWithoutExtension($exe)) -ErrorAction SilentlyContinue){throw 'Close the game before starting a comparison run'}
if($mode -ne 'TransportOnly' -and -not(Test-Path -LiteralPath (NrSafe $game 'nvngx_dlssnr.dll') -PathType Leaf)){throw 'Keep your NVIDIA NR model beside the game EXE as nvngx_dlssnr.dll'}
function Hash([string]$path){NrHash $path};function SafeLocal([string]$path){$full=[IO.Path]::GetFullPath($path);NrSafe (Split-Path $full -Parent) ([IO.Path]::GetFileName($full))}
. (Join-Path $package 'support\Candidate-Launch.ps1')
# Literal %command% argv is already the platform's approved game launch.
$launch=$(if($literalLaunch){GetCandidateLaunch $exe 'Direct' '' ''}else{GetCandidateLaunch $exe 'Auto' '' ''})
function QuoteNr([string]$value){return '"'+[regex]::Replace([regex]::Replace($value,'(\\*)"','$1$1\"'),'(\\+)$','$1$1')+'"'}
$start=New-Object Diagnostics.ProcessStartInfo;$start.FileName=$launch.client;$start.WorkingDirectory=$game;$start.UseShellExecute=$false
$start.WindowStyle=[Diagnostics.ProcessWindowStyle]::Normal # User explicitly launches an interactive game.
if($launch.route -eq 'Steam'){$start.Arguments='-applaunch '+$launch.app_id}else{$start.Arguments=(@($command|Select-Object -Skip 2|ForEach-Object {QuoteNr $_}) -join ' ')}
$start.EnvironmentVariables['NEUROTIC_PREPARED_GUIDES']=$(if($mode -in @('Baseline','TransportOnly')){'0'}else{'1'})
$start.EnvironmentVariables['NEUROTIC_PREPARED_VULKAN']=$(if($mode -in @('Shared','TransportOnly')){'1'}else{'0'})
$provider=$(if($mode -eq 'BuiltIn'){'BuiltIn'}else{'External'});$capture=Join-Path $root ('reviews\prepared-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $capture|Out-Null
try{
    NrProfileApply $game $mode $provider $graphicsApi ([IO.Path]::GetFileName($exe))
    Write-Host "$mode run. Insert opens NeuRotic; Home opens ReShade. Verify a usable depth buffer before judging quality."
    $started=[DateTime]::UtcNow;$process=[Diagnostics.Process]::Start($start)
    if($launch.route -eq 'Steam'){$process=WaitCandidateGame $exe $started 180}
    $process.WaitForExit()
}finally{
    $statePath=NrSafe $game 'NeuRotic\prepared-test-profile.json'
    if(Test-Path -LiteralPath $statePath){$state=Get-Content -LiteralPath $statePath -Raw|ConvertFrom-Json;if($state.PSObject.Properties['preset'] -and $state.preset -cmatch '^NeuRotic\\prepared-test-[a-f0-9]{32}\.ini$'){$presetPath=NrSafe $game $state.preset;if(Test-Path -LiteralPath $presetPath){Copy-Item -LiteralPath $presetPath -Destination (Join-Path $capture 'TestPreset.ini')}}}
    foreach($name in @('OptiScaler.log','ReShade.log','OptiScaler.ini','ReShade.ini')){$path=NrSafe $game $name;if(Test-Path -LiteralPath $path -PathType Leaf){if((Get-Item -LiteralPath $path).Length -le 32MB){Copy-Item -LiteralPath $path -Destination (Join-Path $capture $name)}}}
    if(Get-Process -Name ([IO.Path]::GetFileNameWithoutExtension($exe)) -ErrorAction SilentlyContinue){Write-Warning 'Game is still running. Close it, then use Restore-Test-Settings.cmd.'}else{NrProfileRestore $game}
    Write-Host "Test logs: $capture"
}
