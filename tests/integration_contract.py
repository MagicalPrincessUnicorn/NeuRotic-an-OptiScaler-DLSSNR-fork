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
        keys = joined_strings(text[pos:end])
        # Only widget labels hide ## ID suffixes; ordinary text/help does not.
        if start.group().startswith(('ImGui::Combo(', 'ImGui::Checkbox(',
                                     'ImGui::Button(', 'ImGui::SmallButton(')):
            label = sequence.match(text, pos + len(text[pos:end]) - len(text[pos:end].lstrip()))
            if label:
                original = ''.join(strings(label.group()))
                keys.remove(original)
                displayed = original.split('##', 1)[0]
                if displayed:
                    keys.append(displayed)
        result.update(keys)
    for match in re.finditer(r'(?:routeNames|presentWorkNames)\[\]\s*=\s*\{(.*?)\}', text, re.S):
        result.update(joined_strings(match.group(1)))
    return result
def source(path): return (ROOT/path).read_text(encoding='utf-8')
def require(value, message):
    if not value: raise AssertionError(message)
    print('PASS:', message)
missing = set()
for path in ['OptiScaler/menu/menu_common.cpp', 'OptiScaler/dlssnr/DlssNr_Menu.cpp']:
    old = subprocess.check_output(['git','-c','safe.directory='+ROOT.as_posix(),'-C',str(ROOT),
                                   'show','78c91b998dd05e58fb375560d8f6804bbd47531d:'+path]).decode('utf-8')
    for key in visible(source(path)) - visible(old):
        if normalize(key) not in inventory and not key.startswith('https://'):
            missing.add(key)
present = source('OptiScaler/dlssnr/DlssNr_Present.cpp')
# Exclude the function definition: its body includes diagnostic-only trace strings.
for match in re.finditer(r'SetFallback\((?!PresentApi\s+\w+).*?\);', present, re.S):
    for key in joined_strings(match.group()):
        if key and not key.startswith(('DLSS-NR Present diagnostic:', 'Basic Multipass')) and normalize(key) not in inventory:
            missing.add(key)
for path in ['OptiScaler/dlssnr/PreFg.h', 'OptiScaler/dlssnr/StreamlinePreFg.h']:
    for match in re.finditer(r'(?:refusal\s*=|ReportPresentUnavailable\(PresentApi::D3D12,)\s*("(?:[^"\\]|\\.)*")', source(path)):
        key = ast.literal_eval(match.group(1))
        if normalize(key) not in inventory: missing.add(key)
for key in strings(source('OptiScaler/dlssnr/DlssNr_PresentCompatibility.h')):
    if key and key not in {'supported', 'unsupported Present target'} and normalize(key) not in inventory:
        missing.add(key)
for path in ['OptiScaler/hooks/Vulkan_Hooks.cpp','OptiScaler/wrapped/wrapped_swapchain.cpp']:
    for match in re.finditer(r'ReportPresentUnavailable\(.*?\);', source(path), re.S):
        for key in joined_strings(match.group()):
            if normalize(key) not in inventory: missing.add(key)
# Older Preflight and Moonlight copy remains English-only. Keep this list exact
# so newly exposed strings and refusal reasons still fail the localization gate.
approved_english_fallback = {
    # Existing Vulkan FG status copy from the merge-ready 7d402eae flagship.
    # Record exact inherited English fallbacks; translations are unchanged here.
    'FG settings accepted. Opening this menu keeps generation enabled.',
    'Game FG override rejected (code %d).',
    'Game FG override: Off (accepted).',
    'Game controls Frame Generation.',
    "Waiting for the game's next FG settings update.",
    # DD2 native setup/status uses explicit English fallback, matching the accepted MFG donor.
    'Native MFG',
    "Uses the game's native DLSS Frame Generation. Enabling this selects None for FG Input and Output and disables replacement FG. Enable Frame Generation in the game's settings. High ratios require a qualified provider. Once unlocked, the unlock remains until exit; turning this off stops manual overrides.",
    'Native MFG requires an RTX 40 series GPU and DirectX 12.',
    'Native MFG selected. Save settings and restart to apply the native FG route.',
    'Native MFG waiting: %s',
    'Native MFG unavailable: %s',
    "Native MFG unlocked. Enable Frame Generation in the game's settings.",
    'None means the game controls FG. Higher ratios stay unavailable until the unlock succeeds.',
    'Manual overrides are off. The native unlock remains until the game exits.',

    # Accepted UI and Native MFG donors use these exact English fallbacks.
    # Keep the list explicit so future unregistered UI copy still fails.
    'Ada gates and temporal path published; displayed frames unverified.',
    'Adds Escape as a way to close the menu. Your Menu key still opens and closes it. Escape first cancels a key assignment, ends an active control or dismisses a dropdown. Use Save Settings to remember this option.',
    'Colour Strength',
    'Escape closes menu',
    "Experimental. Unlocks the game's DLSS Multi Frame Generation options above 2X on RTX 40 series cards in D3D12 games. Enable the game's Frame Generation option to use MFG. Manual controls support up to 6X when the loaded provider supports it. Temporal NR runs on real frames, after Ray Reconstruction when active. Present NR uses the final backbuffer with the existing HDR10/scRGB paths and completes the requested Multi-pass chain before supported FG/MFG. Supported queue/UI settings are required. Once unlocked, it stays unlocked until the game exits. Turning this off stops NeuRotic's manual MFG overrides.",
    'Forwarded: %s, %u generated; SetOptions: %s',
    'Game: %s, %u generated; selected: %s',
    'Legacy before-upscaling Manual placement is preserved until you choose a resolution.',
    'MFG publication uncertain; high ratio blocked for this provider.',
    'MFG: %s',
    'NeuRotic',
    'Observed inputs are last owner reports; NR-only checks need activation.\n\nLast Owner observations; Paused frames may leave a prior green indicator visible.\n\nKey:\nGreen check: Observed (last owner report)\nRed X: Missing / failed\nGrey dash: Not yet observable',
    'Present FG needs an observed fixed 2x-6x native provider and supported queue/UI settings',
    'Provider binding changed; high ratio blocked for this session.',
    'Provider profile refused; game setting is used.',
    'Reported maximum: %u; distinct frames unverified.',
    'Reported presentations: %u; distinct frames unverified.',
    'Save settings and restart the game',
    "Selected ratio exceeds this provider's limit; game setting is used.",
    'Selected ratio rejected; game request restored.',
    # Native MFG rejection status follows the existing explicit English
    # fallback policy for experimental MFG controls.
    'Current Frame Generation setting is unconfirmed.',
    'Current Frame Generation setting: %uX.',
    'Current Frame Generation setting: Off.',
    "Native MFG unlock applied. Higher ratios still require runtime acceptance. Enable Frame Generation in the game's settings.",
    'None means the game controls FG. Higher ratios require the native unlock and are disabled if the runtime rejects them.',
    'This runtime rejected higher ratios. They are disabled until the game restarts; Game, Off and 2X remain available.',
    "Selected ratio is unavailable; passing through the game's setting.",
    'Selected ratio rejected; restoring the game request failed.',
    'Signal Status',
    'Unlock DLSS MFG above 2X (RTX 40 series)',
    'Waiting for the supported DLSS-G provider; game setting is used.',
    'accepted', 'pending', 'rejected',
    'Present Multi-pass supports HDR and supported FG/MFG. Guided inputs still require matching guides.',
    'Present FG needs an observed fixed 2x-4x native provider and supported queue/UI settings',
    'Awaiting a usable renderer observation',
    'Canonical input readiness: Unknown - complete current input qualification and use receipts are not published to this panel.',
    'Confirmed GPU-corruption safety checks remain active. The selected changes apply only after you agree and the settings save succeeds.',
    'Core raster or guide observations are incomplete in the current owner snapshot.',
    'Enable the selected experimental options? These paths are untested or still under development and may cause instability or crashes.',
    'Frame raster and guide dimensions were recorded; semantic qualification is unknown.',
    'Image quality: Not measured from NR output',
    'I agree, save settings',
    'Input coverage: %s',
    'MOTION & HISTORY',
    'Moonlight',
    'NR deactivated due to this rendering combination being untested and potentially broken or unstable. To retry, enable Override Multipass NR Guardrails below, confirm it, and save experimental settings. Safety checks still apply.',
    'NR pass outlook: %s. Input coverage is descriptive; it is not a visual-quality grade.',
    'Native reason: %s',
    'Owner report: %s',
    'Present reason: %s',
    'ROUTE & DELIVERY',
    'Read-only owner reports. Paused or old frames may leave last-recorded values visible.',
    'Rendering reported, quality unqualified',
    'SCENE & COLOR',
    'SOURCE & OUTPUT',
    # Integrated Vulkan controls follow the existing explicit English fallback policy.
    'NR model capability check failed',
    'Enables a bounded chain of one to ten Neural Rendering passes on D3D12 and Vulkan. Each later pass consumes the fully composed image from the preceding pass and owns an independent model session and temporal history. Cost increases approximately linearly with the selected pass count.',
    'Multipass requires a supported Native or Present adapter.',
    'Neural Rendering Multipass requires a supported adapter.',
    'Vulkan captures start after five seconds and publish after completed GPU work and recording release. HDR PNGs are SDR previews.',
    'Vulkan compares the same active full-chain frame before and after NR. Stage capture saves eight completed samples. Performance and NR-off captures report their missing adapter.',
}
missing.difference_update(approved_english_fallback)
require(not missing, 'visible UI strings localized or approved English fallback: '+repr(sorted(missing)))
menu = source('OptiScaler/menu/menu_common.cpp')
theme = source('OptiScaler/menu/SleekTheme.h')
nr = source('OptiScaler/dlssnr/DlssNr_Menu.cpp')
dx = source('OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp')
require('Present Enhanced requires NR Multipass off' not in present and
        'Present Enhanced does not support Ray Reconstruction' not in present and
        'RR is not supported' not in dx,
        'Enhanced FG/RR/Multipass blanket rejections removed, including RR capture')
require('api != PresentApi::D3D12 || !PresentGuides' not in present and
        'PresentGuides::Instance().MatchMetadata(guideSelection' in present and
        'PresentGuides::Instance().Bind(guideSelection' in present,
        'DX11 can reach actual guide matching and binding without an API-name rejection')
require('Present uses the same Multi-pass settings as Native Temporal, including HDR and supported FG/MFG.' in nr and
        'if (!d3d12 && !presentRoute) ImGui::BeginDisabled();' in nr and
        'Present Enhanced requires DX12 SDR' not in nr and
        'experimental.Allows(ExperimentalPolicy::Guardrail::Multipass)' not in present and
        'Present FG requires NR Multipass off' not in present and
        'experimental.Allows(ExperimentalPolicy::Guardrail::FrameGeneration)' not in present and
        'experimental.Allows(ExperimentalPolicy::Guardrail::Hdr)' not in present,
        'Multipass, HDR and FG use the normal Present path with resource/guide/completion admission')
require('InvalidateHistory("Present compatibility settings changed")' in present and
        'experimentalFlags != g_present.experimentalFlags' in present and
        'LogToFile =' not in present,
        'feature transitions reset history and do not enable file logging')
require(menu.index('const auto& update = ctx.versionStatus;', menu.index('void MenuCommon::RenderGeneralPage')) < menu.index('void MenuCommon::RenderUpscalingPage'), 'Updates remain in General')
require(menu.count('DlssNr::RenderMenu(') == 1 and menu.count('DlssNr::RenderMultipassMenu(') == 0 and
        nr.count('RenderMultipassMenu(config, menuResScale);') == 1,
        'Multipass is a child tab within the Neural Rendering page')
require(nr.count('StageUi::RenderControls(*config, basicOwnsMain, nativeAfterOnly, false)') == 1 and
        nr.count('MenuControls::EmphasizedCheckbox("Enable NR Multipass"') == 1,
        'single route and bounded multipass controls')
require(nr.count('RenderPassCountSelector(config)') == 1 and
        nr.count('ImGui::Combo("Passes"') == 1,
        'pass-count control is rendered only in Multipass')
require('c[ImGuiCol_TabSelected] = AccentStrong();' in theme and
        'c[ImGuiCol_TabDimmedSelected] = AccentMed(0.90f);' in theme,
        'parent and pass tabs share a visibly stronger selected color')
require('renderSecondLayerControls' not in nr and
        'Enable second neural-rendering layer' not in nr and
        'page == MenuPage::Multipass' in nr,
        'legacy two-layer editor removed and Multipass owns one child page')
require('##DlssNrMultipassInactiveWarning' not in nr and
        'Additional passes require Enable NR Multipass on a compatible route.' in nr,
        'inactive pass reminder removed; requirement stays in the tooltip')
require('one to ten Neural Rendering passes' in nr and 'independent model session and temporal history' in nr,
        'independent pass-chain behavior and cost are disclosed')
require('const bool d3d12 = IsVulkanInput() || State::Instance().api == API::DX12;' in nr and
        'if (!d3d12 && !presentRoute) ImGui::BeginDisabled();' in nr,
        'Native Multipass admits implemented D3D12/Vulkan adapters; Present routes remain selectable')
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
prefg = source('OptiScaler/dlssnr/PreFg.h')
streamline_prefg = source('OptiScaler/dlssnr/StreamlinePreFg.h')
gpu_safety = source('OptiScaler/dlssnr/NrGpuSafety.cpp')
ngx_dx12 = source('OptiScaler/inputs/NVNGX_DLSS_Dx12.cpp')
require('nativeRayReconstructionActive' not in present[present.index('if (preFgFrame && !preFgFrame->valid'):present.index('// Combination changes invalidate history')],
        'native RR no longer triggers the combined pre-FG blanket refusal')
require('PreFg::ReserveCompletion(preFgFrame->outputResource, *preFgFrame)' in present and
        'CommitCompletion(frame.completionReservation' in streamline_prefg and
        'State().completions.Reset();' in streamline_prefg,
        'pre-FG copyback reserves, publishes and invalidates exact-backbuffer completion handoffs')
require('ClaimCompletion(nativeBackbuffer' in ngx_dx12 and
        'GpuSafety::BindExternalWait' in ngx_dx12 and
        'nr-fg-handoff-refused' in ngx_dx12,
        'native DLSSG claims the matching NR completion and fails closed when it cannot bind')
require('queue->Wait(dependency->fence.Get(), dependency->value)' in gpu_safety and
        'nr-fg-wait-applied' in gpu_safety and 'externalWaitGuid' in gpu_safety,
        'provider submission applies the explicit GPU wait and command-list lifetime cookie')
require('!identity.copybackSubmitted' in streamline_prefg and
        'unsafeHandoff ? E_FAIL : forward()' in streamline_prefg,
        'submitted copybacks cannot be canceled or forwarded without a published dependency')
inactive = streamline_prefg[streamline_prefg.index('if (!requested)'):streamline_prefg.index('owner->presentPolicyActive = true;')]
require('runtime.enabled && (route == 1 || route == 2)' in streamline_prefg and
        'if (owner->presentPolicyActive)' in inactive and 'CloseCompletionAdmission();' in inactive and
        'PresentGuides::Instance().Enable(false);' in inactive and 'return forward();' in inactive and
        streamline_prefg.index('owner->presentPolicyActive = true;') < streamline_prefg.index('auto frame = Claim(fg);'),
        'Native and NR Off exit before Present admission with one-time fenced cleanup and guide shutdown')
require('ComPtr<ID3D12Fence> completionFence' in source('OptiScaler/dlssnr/DlssNr_Present.h'),
        'completion fence ownership crosses the Present mutex boundary')
require(present.index('copyback command list could not close') <
        present.index('!preFgFrame->prepareInputs(*preFgFrame)') <
        present.index('queue->ExecuteCommandLists(1, compositeLists)'),
        'game FG tags change only after copyback recording and Close succeed')
require('NativeFeatureRegistry<NVSDK_NGX_Feature>' in ngx_dx12 and
        'HandleToFeature.Released(handleId, featureSnapshot, result == NVSDK_NGX_Result_Success)' in ngx_dx12,
        'native feature classification is synchronized and failed releases retain their identity')
require('dlssnr_call_probe_d3d12' in source('OptiScaler/dlssnr/forwarder/dlssnr_forwarder.cpp'), 'direct Feature 18 capability probe retained')
print('PASS: integration source contracts; runtime evidence remains required')
hdr = source('OptiScaler/dlssnr/HdrObservation.h')
readiness = source('OptiScaler/dlssnr/StartupReadiness.h')
require('uint64_t hdrIdentity = 0;' in readiness and
        'key.hdrIdentity = hdrObservation.identityGeneration;' in present and
        'currentHdr.transitioning || currentHdr.identityGeneration != preFgFrame->readinessIdentity.hdrIdentity' in present,
        'HDR structural identity binds readiness and is rechecked after model evaluation')
require('identityGeneration = entry.snapshot.observationSequence' in hdr and
        'snapshot.identityGeneration = NextSequence();' in hdr,
        'HDR registration and resize identities use the registry-wide monotonic sequence')
require(present.index('if (!presentRequested)') < present.index('HdrObservation::Registry::Instance().Read(swapChain)'),
        'Native Temporal and NR Off remain before HDR Present admission')

# Each source must be compiled once after donor integration.
import xml.etree.ElementTree as ET
from collections import Counter
project_sources = Counter(node.attrib['Include'].replace('\\', '/').casefold()
    for node in ET.parse(ROOT / 'OptiScaler/OptiScaler.vcxproj').iter()
    if node.tag.endswith('}ClCompile') and 'Include' in node.attrib)
duplicate_sources = [name for name, count in project_sources.items() if count != 1]
assert not duplicate_sources, f'Duplicate product compilation: {duplicate_sources}'
print('PASS each merged product source has one compilation owner')
