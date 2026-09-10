"""Source/UI integration gates. Run from any directory with Python 3; no network."""
import ast
import importlib.util
import pathlib
import re
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('catalog', ROOT/'tools/localization/catalog.py')
catalog = importlib.util.module_from_spec(spec)
spec.loader.exec_module(catalog)
catalog.verify()
normalize = lambda text: ' '.join(text.split())
inventory = {normalize(key) for key in catalog.inventory()}
literal = r'"(?:[^"\\]|\\.)*"'
sequence = re.compile(literal + r'(?:\s*' + literal + r')*')
def strings(text):
    return [ast.literal_eval(match.group()) for match in re.finditer(literal, text)]
def joined_strings(text):
    return [''.join(strings(match.group())) for match in sequence.finditer(text)]
def visible(text):
    result = set()
    # Balanced calls, so ImVec4 and nested casts do not terminate TextColored early.
    for start in re.finditer(r'(?:ImGui::(?:Text\w*|Combo|Checkbox|Button|SmallButton)|HelpMarker)\(', text):
        depth, pos, end = 1, start.end(), start.end()
        while depth and end < len(text):
            token = re.match(literal, text[end:])
            if token:
                end += len(token.group()); continue
            if text[end] == '(': depth += 1
            if text[end] == ')': depth -= 1
            end += 1
        result.update(joined_strings(text[pos:end]))
    for match in re.finditer(r'(?:routeNames|presentWorkNames)\[\]\s*=\s*\{(.*?)\}', text, re.S):
        result.update(joined_strings(match.group(1)))
    return result
def source(path): return (ROOT/path).read_text(encoding='utf-8')
def require(value, message):
    if not value: raise AssertionError(message)
    print('PASS:', message)
missing = set()
for path in ['OptiScaler/dlssnr/DlssNr_Menu.cpp', 'OptiScaler/menu/menu_common.cpp']:
    old = subprocess.check_output(['git','-c','safe.directory='+ROOT.as_posix(),'-C',str(ROOT),
                                   'show','7040d75d:'+path]).decode('utf-8')
    for key in visible(source(path)) - visible(old):
        if normalize(key) not in inventory and not key.startswith('https://'):
            missing.add(key)
for match in re.finditer(r'ToggleBurstMessages\s*=\s*\{(.*?)\}', source('OptiScaler/dlssnr/NrToggleNotes.h'), re.S):
    for key in joined_strings(match.group(1)):
        if normalize(key) not in inventory: missing.add(key)
for key in strings(source('OptiScaler/dlssnr/DlssNr_BridgeTelemetry.h')):
    if key and normalize(key) not in inventory: missing.add(key)
present = source('OptiScaler/dlssnr/DlssNr_Present.cpp')
for match in re.finditer(r'SetFallback\(.*?\);', present, re.S):
    for key in joined_strings(match.group()):
        if key and not key.startswith('DLSS-NR Present diagnostic:') and normalize(key) not in inventory:
            missing.add(key)
for key in strings(source('OptiScaler/dlssnr/DlssNr_PresentCompatibility.h')):
    if key and key not in {'supported', 'unsupported Present target'} and normalize(key) not in inventory:
        missing.add(key)
for path in ['OptiScaler/hooks/Vulkan_Hooks.cpp','OptiScaler/wrapped/wrapped_swapchain.cpp']:
    for match in re.finditer(r'ReportPresentUnavailable\(.*?\);', source(path), re.S):
        for key in joined_strings(match.group()):
            if normalize(key) not in inventory: missing.add(key)
require(not missing, 'all imported visible UI strings and fallback reasons localized: '+repr(sorted(missing)))
menu = source('OptiScaler/menu/menu_common.cpp')
nr = source('OptiScaler/dlssnr/DlssNr_Menu.cpp')
dx = source('OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp')
require(menu.index('ScopedCollapsingHeader("Updates"') > menu.index('void MenuCommon::RenderGeneralPage'), 'Updates remain in General')
require(menu.count('DlssNr::RenderMenu(') == 1 and menu.count('DlssNr::RenderMultipassMenu(') == 0 and
        nr.count('RenderMultipassMenu(config, menuResScale);') == 1,
        'Multipass is a collapsible section within the Neural Rendering page')
require(nr.count('ImGui::Combo("NR route"') == 1 and
        nr.count('ImGui::Checkbox("Enable NR Multipass"') == 1,
        'single route and bounded multipass controls')
require('renderSecondLayerControls' not in nr and
        'Enable second neural-rendering layer' not in nr and
        nr.count('DlssNrMultipassSection') == 1,
        'legacy two-layer editor removed and Multipass owns one stable section ID')
require('BeginChild("##DlssNrMultipassInactiveWarning"' in nr and
        'ImGuiCol_ChildBg' in nr and 'ImGuiCol_Border' in nr,
        'inactive multi-pass selection uses a bordered yellow warning box')
require('one to ten Neural Rendering passes' in nr and 'independent model session and temporal history' in nr,
        'independent pass-chain behavior and cost are disclosed')
require('!IsVulkanInput() && State::Instance().api == API::DX12' in nr,
        'multipass enable requires D3D12')
require('CompositionPool' not in nr and 'HardCap' not in nr, 'adaptive capacity has no menu control')
require('Automatic installation will' not in menu, 'no automatic installer promise')
require('ParkAllAdditionalLayerFeatures("NR route domain changed")' in dx and
        'g_nr.resumeFeatureAwaitingRelease = g_nr.feature;' in dx,
        'all independent history domains retire safely')
require('CompositionPool::RequiredSlots(healthyPassCount)' in dx and
        'Prepare(cmdList, true, RequestedPassCount(cfg))' in dx,
        'Pre-SR and Post-SR reserve for the complete requested chain')
require('cfg.DlssNrRoute.value_or_default() != 0' in dx and 'frame.Reset = gameReset != 0;' in dx and
        'frame.Reset = resetHistory;' in dx, 'native game reset and explicit Present-history reset are retained')
require('completionUntrackable' in present and 'DXGI_PRESENT_TEST' in present and 'DirtyRectsCount' in present, 'Present failure-safe gates retained')
require('dlssnr_call_probe_d3d12' in source('OptiScaler/dlssnr/forwarder/dlssnr_forwarder.cpp'), 'direct Feature 18 capability probe retained')
print('PASS: integration source contracts; runtime evidence remains required')
