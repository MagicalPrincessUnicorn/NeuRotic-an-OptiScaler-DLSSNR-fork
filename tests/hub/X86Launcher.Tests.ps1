$ErrorActionPreference='Stop'
$repo=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$launcher=Join-Path $repo 'tools/packaging/customer/prepared-guides/Start-x86-Prepared.ps1'
$root=Join-Path $repo 'builds/audit-prepared-launch';[void][IO.Directory]::CreateDirectory($root)
$exe=Join-Path $root 'FixtureGame.exe'
$compiler=New-Object Microsoft.CSharp.CSharpCodeProvider;$options=New-Object CodeDom.Compiler.CompilerParameters
$options.GenerateExecutable=$true;$options.OutputAssembly=$exe;$options.CompilerOptions='/platform:x86 /optimize'
$compiled=$compiler.CompileAssemblyFromSource($options,'public class Fixture { public static void Main(string[] args) { System.IO.File.WriteAllText(args[0],System.Environment.GetEnvironmentVariable("NEUROTIC_PREPARED_GUIDES")+":"+System.Environment.GetEnvironmentVariable("SteamAppId")+":"+System.Environment.GetEnvironmentVariable("SteamGameId")); } }')
if($compiled.Errors.HasErrors){throw 'Inert x86 fixture failed to compile'};$compiler.Dispose()
function Put($path,$text){[void][IO.Directory]::CreateDirectory((Split-Path $path));[IO.File]::WriteAllText($path,$text)}
function Hash($path){(Get-FileHash -LiteralPath $path).Hash}
$recordPath=Join-Path $root 'NeuRotic-backups/install-fixture/INSTALL-MANIFEST.json';$files=@()
foreach($name in @('dxgi.dll','NeuRotic\Prepared\NeuRotic-PreparedGuides.addon32','NeuRotic\Prepared\NeuRotic.GpuHost\NeuRotic.PreparedGpuHost.exe','NeuRotic\Prepared\NeuRotic.GpuHost\OptiScaler.dll','NeuRotic\Prepared\NeuRotic.GpuHost\nvngx.dll_dlssnr.dll','NeuRotic\Prepared\NeuRotic.GpuHost\WinPixEventRuntime.dll','NeuRotic\Prepared\Start-PreparedGame.ps1')){
 $path=Join-Path $root $name;Put $path 'inert owned cohort bytes';if($name.EndsWith('.ps1')){[IO.File]::Copy($launcher,$path,$true)}
 $files+=@{path=$name;installed_hash=(Hash $path);installed_exists=$true}
}
$record=@{kind='neurotic-customer-candidate-install';status='installed-verified';game_executable=$exe;files=$files}
Put $recordPath ($record|ConvertTo-Json -Depth 8)
$state=@{kind='neurotic-public-install';schema_version=2;status='installed-verified';install_route='prepared-x86';game_executable=$exe;restore_chain=@($recordPath);restore_record_ledger=@(@{path=$recordPath;sha256=(Hash $recordPath);record_json=[IO.File]::ReadAllText($recordPath)})}
Put (Join-Path $root 'NeuRotic/Installer/Current-Install.json') ($state|ConvertTo-Json -Depth 10)
$before=$env:NEUROTIC_PREPARED_GUIDES
$output=& $launcher -GameExecutable $exe -SteamAppId '620' -CheckOnly
if($output -notmatch 'SteamAppId=620'){throw 'Valid committed prepared cohort was refused'}
if($env:NEUROTIC_PREPARED_GUIDES -cne $before){throw 'Parent process activation changed'}
$pending=Join-Path $root 'NeuRotic/Installer/Install-Transaction.json';Put $pending '{}'
$refused=$false;try{& $launcher -GameExecutable $exe -CheckOnly}catch{$refused=$true}
if(-not $refused){throw 'Pending recovery must refuse launch'};[IO.File]::Delete($pending)
$component=Join-Path $root 'dxgi.dll';Put $component 'external modification'
$refused=$false;try{& $launcher -GameExecutable $exe -CheckOnly}catch{$refused=$true}
if(-not $refused){throw 'Altered prepared cohort must refuse launch'};Put $component 'inert owned cohort bytes'
try{& $launcher -GameExecutable $exe -SteamAppId 'bad-id' -CheckOnly;throw 'accepted malformed Steam identity'}catch{if($_.Exception.Message -eq 'accepted malformed Steam identity'){throw}}
try{& $launcher -GameExecutable "$env:SystemRoot/System32/cmd.exe" -CheckOnly;throw 'accepted x64 executable'}catch{if($_.Exception.Message -eq 'accepted x64 executable'){throw}}
$result=Join-Path $root 'child.txt'
& $launcher -GameExecutable $exe -SteamAppId '620' -GameArguments @($result)
if(-not(Test-Path -LiteralPath $result) -or [IO.File]::ReadAllText($result).Trim() -ne '1:620:620'){throw 'Inert child did not receive prepared and Steam identity'}
if($env:NEUROTIC_PREPARED_GUIDES -cne $before){throw 'Launch changed parent process environment'}
Write-Output 'PASS prepared launcher: committed cohort, pending and altered-component refusal, x86 child environment, parent unchanged'