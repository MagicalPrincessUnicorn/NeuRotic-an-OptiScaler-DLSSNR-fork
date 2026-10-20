$ErrorActionPreference='Stop'
$repo=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$source=[IO.File]::ReadAllText((Join-Path $repo 'OptiScaler/menu/menu_common.cpp'))
$manualStart=$source.IndexOf('static Neurotic::KeyChord::ReleaseTracker uwpShortcuts;')
$manualEnd=$source.IndexOf('void MenuCommon::ShowTooltip', $manualStart)
$uwpStart=$source.IndexOf('void UwpShortcutFocusChanged(bool focused)')
if($uwpStart -lt 0){$uwpStart=$source.IndexOf('void KeyDown(UINT vKey,bool ctrl,bool shift,bool alt)')}
$uwpEnd=$source.IndexOf('// The lamp, and only the lamp.', $uwpStart)
if($manualStart -lt 0 -or $manualEnd -lt 0 -or $uwpStart -lt 0 -or $uwpEnd -lt 0){throw 'Runtime reader function boundaries not found'}
$readers=$source.Substring($manualStart,$manualEnd-$manualStart)+$source.Substring($uwpStart,$uwpEnd-$uwpStart)
$owner=[IO.File]::ReadAllText((Join-Path $repo 'OptiScaler/menu/input/input_system.cpp'))
$getterStart=$owner.IndexOf('std::uint64_t GetFocusGeneration()')
$getterEnd=$owner.IndexOf('bool IsKeyDown(', $getterStart)
if($getterStart -lt 0 -or $getterEnd -lt 0){throw 'Input focus generation getter not found'}
$readers='namespace OptiInput {'+$owner.Substring($getterStart,$getterEnd-$getterStart)+'}'+$readers
if($source.Contains('void UwpShortcutFocusChanged(bool focused)')){$readers="#define NH_UWP_FOCUS_CALLBACK_AVAILABLE`n"+$readers}
$backend=[IO.File]::ReadAllText((Join-Path $repo 'OptiScaler/include/imgui/imgui_impl_uwp.cpp'))
$activationStart=$backend.LastIndexOf('int WindowActivated(ABI::Windows::UI::Core::ICoreWindow* sender,')
$activationEnd=$backend.IndexOf('int PointerPressed(', $activationStart)
if($activationStart -lt 0 -or $activationEnd -lt 0){throw 'UWP activation callback boundary not found'}
$readers+=$backend.Substring($activationStart,$activationEnd-$activationStart)
$messages=[IO.File]::ReadAllText((Join-Path $repo 'OptiScaler/menu/input/input_system_messages.cpp'))
$aggregateStart=$messages.IndexOf('void SyncAggregateModifierButtonStateLocked(')
$aggregateEnd=$messages.IndexOf('bool IsMouseMessage(', $aggregateStart)
$producerStart=$messages.IndexOf('void SetKeyDown(')
$producerEnd=$messages.IndexOf('void SetMouseDown(', $producerStart)
$readers+='namespace OptiInput {'+$messages.Substring($aggregateStart,$aggregateEnd-$aggregateStart)+$messages.Substring($producerStart,$producerEnd-$producerStart)+'}'
$clearStart=$messages.IndexOf('void ResetKeyboardEdgesForFocusLocked()')
if($clearStart -ge 0){$clearEnd=$messages.IndexOf('void ResetButtonBlockedStateLocked()', $clearStart);$readers+='namespace OptiInput {'+$messages.Substring($clearStart,$clearEnd-$clearStart)+'}'}
$messageStart=$messages.IndexOf('case WM_SETFOCUS:', $messages.IndexOf('bool shouldBlock = false;'))
$messageEnd=$messages.IndexOf('case WM_MOUSEMOVE:', $messageStart)
if($messageStart -lt 0 -or $messageEnd -lt 0){throw 'Win32 focus message cases not found'}
$readers+="namespace OptiInput { void FocusMessage(int msg){switch(msg){"+$messages.Substring($messageStart,$messageEnd-$messageStart)+"}} }"
[IO.File]::WriteAllText((Join-Path $repo 'builds/hub/keybind-runtime-readers.inc'),$readers)
$runner=@'
@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
cl /nologo /EHsc /std:c++20 /utf-8 tests\hub\KeybindRuntimeTests.cpp /Fobuilds\hub\obj\KeybindRuntimeTests.obj /Febuilds\hub\KeybindRuntimeTests.exe
if errorlevel 1 exit /b 1
builds\hub\KeybindRuntimeTests.exe
'@
[IO.File]::WriteAllText((Join-Path $repo 'builds/hub/Test-KeybindRuntime.cmd'),$runner)
Push-Location $repo
try{& cmd /c builds\hub\Test-KeybindRuntime.cmd;if($LASTEXITCODE -ne 0){throw 'Runtime reader tests failed'}}finally{Pop-Location}
