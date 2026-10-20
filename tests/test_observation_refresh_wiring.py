from pathlib import Path
root = Path(__file__).resolve().parents[1]
menu = (root/'OptiScaler/dlssnr/DlssNr_Menu.cpp').read_text(encoding='utf-8-sig')
source = (root/'OptiScaler/nr/diagnostics/capability/ObservationRefresh.cpp').read_text()
button = root/'OptiScaler/dlssnr/ObservationRefreshButton.h'
shell = (root/'OptiScaler/menu/SleekShell.h').read_text()
assert 'ObservationActions(Capability::ObservationRefresh::Busy())' in menu
assert 'ObservationRefreshButton("Refresh observations",refreshBusy,refresh)' in shell, 'Refresh must draw the busy spinner button'
assert 'ObservationRefresh::Update' in menu and '!actions.copyClicked' in menu
copy = menu.split('if(actions.copyClicked)', 1)[1].split('    return;', 1)[0]
assert 'completed->utf8.c_str()' in copy
assert 'CollectStatus' not in copy and 'ExportDiagnostic' not in copy and 'Update(' not in copy
assert 'BeginDisabled(busy)' in button.read_text()
assert 'PathArcTo' in button.read_text() and 'Refresh observations' in shell
assert 'CurrentOwner(' in menu and 'depthSupported' not in menu and 'mayAdmit' not in menu
assert 'nativeOperational && input.jitterSupplied && input.jitter.has_value()' in menu
assert 'nativeOperational && input.preExposureSupplied && input.preExposure.has_value()' in menu
assert 'ObservationRefresh::RequestClose()' in (root/'OptiScaler/dllmain.cpp').read_text(encoding='utf-8-sig')
assert 'FreeLibraryAndExitThread(module,0)' in source
assert 'PrepareObservationReport(*report)' in source
print('actual refresh/copy/spinner/owner-status/shutdown wiring passed')
