param([string]$DepthBundle = '')
$ErrorActionPreference='Stop'
$taskRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
function Invoke-CleanEnvironment([string]$Executable,[string[]]$Arguments) {
    $taskStart=[Diagnostics.ProcessStartInfo]::new()
    $taskStart.FileName=$Executable
    $taskStart.WorkingDirectory=$taskRoot
    $taskStart.UseShellExecute=$false
    $taskStart.RedirectStandardOutput=$true
    $taskStart.RedirectStandardError=$true
    $taskStart.Environment.Clear()
    foreach($taskEntry in [Environment]::GetEnvironmentVariables().GetEnumerator()) {
        $taskStart.Environment[$taskEntry.Key.ToUpperInvariant()]=[string]$taskEntry.Value
    }
    foreach($taskArgument in $Arguments) { $taskStart.ArgumentList.Add($taskArgument) }
    $taskProcess=[Diagnostics.Process]::Start($taskStart)
    $taskOutput=$taskProcess.StandardOutput.ReadToEndAsync()
    $taskError=$taskProcess.StandardError.ReadToEndAsync()
    $taskProcess.WaitForExit()
    if($taskOutput.Result){Write-Host $taskOutput.Result -NoNewline}
    if($taskError.Result){Write-Host $taskError.Result -NoNewline}
    return $taskProcess.ExitCode
}
$taskMsbuild='C:/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/MSBuild.exe'
$taskCode=Invoke-CleanEnvironment $taskMsbuild @('tools/window-worker/guidance/GuidanceTests.vcxproj','/p:Configuration=Release','/p:Platform=x64','/p:VCToolsVersion=14.44.35207','/v:minimal','/nologo')
if($taskCode -ne 0) {exit $taskCode}
$taskArguments=@();if($DepthBundle){$taskArguments+=([IO.Path]::GetFullPath($DepthBundle))}
$taskCode=Invoke-CleanEnvironment (Join-Path $taskRoot 'builds/window-worker/guidance/GuidanceTests.exe') $taskArguments
exit $taskCode
