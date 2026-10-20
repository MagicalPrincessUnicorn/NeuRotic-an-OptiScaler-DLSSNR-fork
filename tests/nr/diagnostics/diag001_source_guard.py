"""Authored NR-DIAG-001 source guard. This candidate file has not been executed."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[3]
header = (root / 'OptiScaler/nr/diagnostics/ContractObservation.h').read_text(encoding='utf-8')
frame_trace = (root / 'OptiScaler/dlssnr/FrameTrace.h').read_text(encoding='utf-8')
runner = (root / 'tests/Run-NrDiag001.cmd').read_text(encoding='utf-8')
modified = [
    root / 'OptiScaler/inputs/NVNGX_DLSS_Dx12.cpp',
    root / 'OptiScaler/dlssnr/DlssNr_Present.cpp',
    root / 'OptiScaler/hooks/FG_Hooks.cpp',
    root / 'OptiScaler/wrapped/wrapped_swapchain.cpp',
]

def require(condition, label):
    assert condition, label
    print('PASS:', label)

prohibited = [
    r'\bspdlog::', r'\bLOG_[A-Z]+\s*\(', r'\bQueryInterface\s*\(', r'\bAddRef\s*\(',
    r'\bRelease\s*\(', r'\bGetDesc\s*\(', r'\bExecuteCommandLists\s*\(', r'\bSignal\s*\(',
    r'\bWaitFor\w*\s*\(', r'\bDispatch\s*\(', r'\bMap\s*\(', r'\bCreateCommittedResource\s*\(',
    r'\bstd::mutex\b', r'\bstd::thread\b', r'\bstd::condition_variable\b', r'\bstd::vector\b',
]
for pattern in prohibited:
    require(re.search(pattern, header) is None, 'M0 candidate excludes producer/runtime token '+pattern)

for name in [
    'DropCounters.queueFull', 'DropCounters.producerContention', 'DropCounters.validationRejected',
    'DropCounters.unsupportedEventEncoding', 'DropCounters.consumerExportDropped',
    'DropCounters.consumerSerializationFailed', 'DropCounters.transportUnavailableDrop',
    'CoverageCounters.observedEvents', 'CoverageCounters.ownerPublicationUnavailable',
    'CoverageCounters.unsupportedReceiptType', 'CoverageCounters.missingAssociation',
    'CoverageCounters.missingGeneration', 'CoverageCounters.missingContentRevision',
    'CoverageCounters.observerDisabled', 'CoverageCounters.transportUnavailable',
    'CoverageCounters.partialExport', 'coverage.overall', 'coverage.ownerFacts',
    'coverage.association', 'coverage.generations', 'coverage.contentFreshness', 'coverage.transport',
    'observationMode', 'queueCapacity', 'attemptedPublications', 'successfullyEnqueued',
    'drainedEvents', 'exportedEvents', 'exportTermination',
]:
    require(name in header, 'frozen M0 field present: '+name)

require('for (unsigned int attempt' not in header,
        'producer contention accounting performs no retry loop')
require('Evidence(draft.source.resourceIdentity->record)' in header and
        'Evidence(draft.source.ownerReceipt->record)' in header,
        'known owner references retain their own owner-published provenance')
require('source.fields.Size() != 0' in header and 'ValidOwnerMetadataReference' in header,
        'malformed or diagnostics-authored owner-reference payloads are rejected')
require('async_overflow_policy' not in header, 'M0 transport does not inherit spdlog overflow policy')
require('std::optional<GlobalObserver> globalObserver' in header and
        'globalObserver.emplace(Mode::Shadow)' in header and
        'inline GlobalObserver globalObserver' not in header,
        'Off mode does not construct or start the fixed M0 queue')
require('return "NoConsumer"' not in header and
        'output.snapshot.coverage.transportUnavailable = 1' in header,
        'terminal no-consumer state uses frozen transportUnavailable vocabulary')
require('OwnerReferences' in header and
        'source.owner == C::OwnerDomain::Unspecified || source.owner == C::OwnerDomain::Diagnostics' in header,
        'owner-receipt references require an explicit non-diagnostics owner domain')
require('NEUROTIC_CONTRACT_OBSERVATION' in frame_trace, 'internal process-scoped Off/Shadow control is wired')
require('ObserveContractBoundary' in frame_trace and 'WithM0Publisher' in frame_trace,
        'FrameTrace exposes the narrow integration seam')
require('LogEvent(_nrFrameTraceKind, __VA_ARGS__)' in frame_trace,
        'existing variadic FrameTrace payload remains lazy when trace logging is off')
require('vcvars64.bat' not in runner and 'C:\\OptiScaler-NR-Dev' not in runner,
        'DIAG runner uses the verified caller toolchain and no fixed machine path')
require('if "%~1"==""' in runner and 'VSCMD_ARG_TGT_ARCH' in runner and 'set "nrDiagOutput=%~f1"' in runner,
        'DIAG runner requires an explicit evidence directory and verified x64 environment')
for path in modified:
    text = path.read_text(encoding='utf-8')
    require(text.count('NR-DIAG-001 BEGIN') == 1 and text.count('NR-DIAG-001 END') == 1,
            'one bounded integration block in '+path.as_posix())
    block = text.split('NR-DIAG-001 BEGIN', 1)[1].split('NR-DIAG-001 END', 1)[0]
    require(':p' not in block and 'QueryInterface' not in block and 'GetDesc(' not in block,
            'integration block copies scalars only in '+path.as_posix())
print('PASS: authored source guard; this output exists only when the guard is later executed')
