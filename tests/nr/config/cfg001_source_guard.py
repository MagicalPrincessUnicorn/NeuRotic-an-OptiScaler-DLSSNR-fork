"""NR-CFG-001 source and stacking guard. Runs without GPU/provider access."""
from pathlib import Path
import hashlib
import re

root = Path(__file__).resolve().parents[3]
header_path = root / 'OptiScaler/nr/config/IntentSemantics.h'
runner_path = root / 'tests/Run-NrCfg001.cmd'
config_cpp = (root / 'OptiScaler/Config.cpp').read_text(encoding='utf-8')
config_h = (root / 'OptiScaler/Config.h').read_text(encoding='utf-8')
snapshot_h = (root / 'OptiScaler/NrConfigSnapshot.h').read_text(encoding='utf-8')
root_ini = (root / 'OptiScaler.ini').read_bytes()
integration_ini = (root / 'integration/OptiScaler.ini').read_bytes()
header = header_path.read_text(encoding='utf-8')
runner = runner_path.read_text(encoding='utf-8')

def require(condition, label):
    assert condition, label
    print('PASS:', label)

require('if (lower == "auto")' in config_cpp and 'return std::nullopt;' in config_cpp,
        'Alpha parser auto remains unset/default at the exact read seam')
require('PerformanceMode' in config_cpp and 'RunBeforeSR' in config_cpp and 'RenderingMode' in config_cpp,
        'legacy placement aliases remain visible to the candidate audit')
require('!multipassEnabled.has_value() && DlssNrSecondLayer.value_or_default()' in config_cpp,
        'SecondLayer-only legacy serial Multipass compatibility remains source-grounded')
require('!DlssNrWhitePointSource.has_value() && DlssNrWhitePointFromExposure.has_value()' in config_cpp,
        'white-point alias remains absence-sensitive')
require('NrConfigSynchronization::Transaction {}' in snapshot_h and
        'BasicMultipass::Derive(*result' in snapshot_h,
        'immutable detached NR snapshot seam remains source-grounded')
require(hashlib.sha256(root_ini).hexdigest() != hashlib.sha256(integration_ini).hexdigest(),
        'root and integration INIs remain distinct source artifacts')

for token in ('#include <windows.h>', 'ID3D', 'IDXGI', 'NVSDK', 'QueryInterface(',
              'ExecuteCommandLists(', 'RoutingDecision', 'PlanCommit', 'SelectRoute(',
              'IsNativeSupported(', 'CommitRoute(', 'ProviderOffer'):
    require(token not in header, 'config semantic helper excludes operational token '+token)
require('SemanticClass::CapabilityFact' not in header and
        'SemanticClass::VolatileRuntimeState' not in header and
        'SemanticClass::DerivedEffectiveValue' not in header,
        'persisted parser never emits capability, volatile-runtime or operational effective classes')
require('ValueState::AutoUnset' in header and 'future Orchestrator Auto' in header,
        'parser auto preservation is explicit and cannot become routing Auto')
require('MigrationDisposition::RefuseUnsupportedFutureSchema' in header and
        'MigrationDisposition::RefuseNonRepresentable' in header,
        'future schema and downgrade refusal are explicit')
require('classification.semantic != SemanticClass::UnknownFutureValue && Detail::IsAuto' in header and
        'snapshot.status != AnalysisStatus::UnsupportedFutureSchema' in header,
        'unknown auto and unsupported future schema are not interpreted through Alpha semantics')
require('NewerThan(targetSchema, CurrentSchema)' in header,
        'unimplemented future target schemas are refused')
require('CommitOutcome::Indeterminate' in header and 'AuthorityState::Unknown' in header and
        'std::optional<std::string_view> authoritative()' in header,
        'commit ambiguity and failed recovery cannot manufacture file authority')
require('OriginalAuthoritative' in header and 'BackupConfirmed' in header and
        'Staged' in header and 'Validated' in header and 'Committed' in header and 'RolledBack' in header,
        'migration transaction models backup, staging, validation, commit and restore')
require('vcvars64.bat' not in runner and 'C:\\OptiScaler-NR-Dev' not in runner,
        'CFG runner uses caller-verified toolchain and no fixed machine path')
require('if "%~1"==""' in runner and 'VSCMD_ARG_TGT_ARCH' in runner and
        'set "nrCfgOutput=%~f1"' in runner,
        'CFG runner requires explicit evidence output and verified x64 environment')
require('DlssNrRoute { 2 }' in config_h and 'DlssNrRenderingMode { 1 }' in config_h and
        'DlssNrMultipassEnabled { false }' in config_h,
        'existing Alpha route/rendering/Multipass defaults remain unchanged')
print('PASS: NR-CFG-001 source and stacking guard')
