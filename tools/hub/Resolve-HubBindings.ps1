[CmdletBinding()]
param([string]$Root=(Split-Path -Parent (Split-Path -Parent $PSScriptRoot)),[string]$ExpectedCommit,[string]$OutputPath)
$ErrorActionPreference='Stop'
$commit=(& git -C $Root rev-parse HEAD).Trim()
if($LASTEXITCODE -ne 0){throw 'No source identity'}
if($ExpectedCommit -and $commit -ne $ExpectedCommit){throw 'Source commit mismatch'}
function Binding([string]$relative) {
 $path=Join-Path $Root $relative
 if(-not (Test-Path -LiteralPath $path -PathType Leaf)){throw "Missing live source binding: $relative"}
 return [ordered]@{path=$relative;sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()}
}
$result=[ordered]@{schemaVersion=1;checkout=$Root;branch=(& git -C $Root branch --show-current).Trim();commit=$commit;
 acceptedAnchor='23b1cb299d8e378371ba9eb7753f499d6ae4bc6b';dirty=@(& git -C $Root status --porcelain);
 ui=(Binding 'OptiScaler/menu/menu_common.cpp');imgui=(Binding 'OptiScaler/include/imgui/imgui.h');
 sleekUiReference=[ordered]@{commit='40d010688961a4f661b4bea8904a2b92d0741c0b';source='OptiScaler/menu/SleekTheme.h';adapter=(Binding 'apps/NeuRoticHub/ui/SharedUiAdapter.cpp')};
 installer=(Binding 'tools/packaging/customer/support/NeuRotic-Setup-Engine.ps1');
 installerTests=(Binding 'tests/Test-CustomerInstaller.ps1');
 evaluator=(Binding 'OptiScaler/dlssnr/DlssNrFeature_Dx12.h');
 lifetime=(Binding 'OptiScaler/dlssnr/NrGpuSafety.h');
 feeder=(Binding 'OptiScaler/dlssnr/NrDispatchResources.h');
 frameContext=(Binding 'OptiScaler/dlssnr/VulkanNrFrameContract.h');
 nativeFrameBridge=(Binding 'OptiScaler/nr/protocol/NativeFrameBridge.h');
 standaloneRendering='not compiled; NH-09 and later are queued';historicalExportImported=$false}
if($OutputPath){[IO.File]::WriteAllText($OutputPath,($result|ConvertTo-Json -Depth 10),(New-Object Text.UTF8Encoding($false)))}
return [pscustomobject]$result
