"""Production wiring gates complement the compiled Advisor policy/sampling tests."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
menu = (root/'OptiScaler/dlssnr/DlssNr_Menu.cpp').read_text(encoding='utf-8')
dispatch = (root/'OptiScaler/dlssnr/StreamlinePreFg.h').read_text(encoding='utf-8')

def section(text, start, end):
    return text.split(start, 1)[1].split(end, 1)[0]

def require(condition, label):
    assert condition, label
    print('PASS:', label)

require('int stage = AdvisorPolicy::After;' in menu, 'Advisor defaults After without changing INI')
ui = section(menu, 'if (StageUi::SentenceCombo("##AdvisorStage"', 'ImGui::EndDisabled();')
require('advisor.routes = {}' in ui and 'config->' not in ui and 'ConfigureAdvisorRoute' not in ui,
        'scope selection discards stale results without editing live configuration')
require('const int routeCount = AdvisorPolicy::RouteCount(advisor.stage);' in menu and
        menu.count('route < routeCount;') == 2 and
        'advisor.nextRoute < AdvisorPolicy::RouteCount(advisor.stage)' in menu,
        'wide/narrow cards and Test All use the same stage route count')
configure = section(menu, 'void ConfigureAdvisorRoute(', 'void BeginAdvisorRoute(')
require('AdvisorPolicy::SelectPlacement(config, Advisor().stage, route)' in configure and
        'StageUi::SelectResolutionChoice(config, Advisor().resolutionPreference)' in configure,
        'execution explicitly applies both selected placement and resolution')
preflight = section(menu, 'const char* AdvisorRouteRefusal(const Config& config, int route)\n{', 'void StartAdvisorAnalysis(')
require('AdvisorPolicy::Refusal(config.GetDlssNrConfigSnapshot(), Advisor().stage, route,' in preflight,
        'individual and batch preflight share the execution policy')
capture = section(menu, 'void CaptureAdvisorSettings(', 'void RestoreAdvisorSettings(')
restore = section(menu, 'void RestoreAdvisorSettings(', 'void ConfigureAdvisorRoute(')
for field in ('DlssNrUiResolutionPreset', 'DlssNrUiPresentResolutionPreset', 'DlssNrUiEnhancedResolutionPreset',
              'DlssNrRenderingMode', 'DlssNrRunBeforeSr', 'DlssNrRoute', 'DlssNrMultipassEnabled',
              'DlssNrUiManualScale', 'DlssNrUiPresentManualScale', 'DlssNrUiEnhancedManualScale'):
    require(field in capture and field in restore, 'capture and restore '+field)
for start, end in [('void FinishAdvisorRoute(', 'void FailAdvisorRoute('),
                   ('void FailAdvisorRoute(', 'const char* AdvisorRouteRefusal(const Config& config, int route)\n{'),
                   ('void CancelAdvisorAnalysis(Config* config, const char* reason)\n{', 'void RenderMenu(')]:
    require('RestoreAdvisorSettings(' in section(menu, start, end), 'restoration on '+start.split('(')[0])
require(menu.count('advisor.providerGeneration = AdvisorProviderGeneration(present);') >= 1 and
        menu.count('AdvisorProviderGeneration(present) != advisor.providerGeneration') == 1 and
        'return provider.known ? provider.generation : present.cadence.providerGeneration;' in menu,
        'current provider identity seeds and checks a trial even after native fast exits')
native = section(dispatch, 'if (AdvisorSampling::ObserveNativeCadence(runtime.enabled, route, State().swapchains))',
                 'owner->presentPolicyActive = true;')
require('ReportPresentCallTiming(' in native and 'identity.advisorConfigurationGeneration =' in native and
        'provider.known && !provider.enabled && State().swapchains == 1' in native,
        'Advisor Native cadence carries configuration and proven single-chain FG-off provider identity')
require(all(token not in native for token in ('EvaluatePresentImageOnly(', 'GetBuffer(', 'Claim(',
                                               'ExecuteCommandLists(', 'PrepareFullFrame(', 'Signal(')),
        'Native timing observation performs no model admission, buffer queries, GPU claims or signals')
require('owner->previousPresentMs = 0.0;\n        return forward();' in native,
        'ordinary Native/Off retains untimed forwarding and clears stale timing')
tick = section(menu, 'void TickAdvisor(Config* config)', 'void CancelAdvisorAnalysis(Config* config, const char* reason)\n{')
for token in ('!lifecycle.lifecycleOpen', 'lifecycle.lifecycleGeneration != advisor.lifecycleGeneration',
              'State::Instance().isShuttingDown', 'present.resourceGeneration != advisor.resourceGeneration',
              'present.cadence.configurationGeneration == advisor.configurationGeneration',
              'completedObservation > advisor.lastCompletedObservation', 'present.presentGpuValid',
              'advisor.sampling.RejectStall(advisor.phase == AdvisorPhase::Sample',
              'advisor.sampling.StartupExpired(', 'advisor.sampling.WarmupFrame('):
    require(token in tick, 'controller retains gate: '+token)
print('PASS: Advisor stage production wiring; these are source contracts, not a live-game test')
