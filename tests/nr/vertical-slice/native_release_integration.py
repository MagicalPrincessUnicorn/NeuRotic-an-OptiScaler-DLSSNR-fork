"""Source preservation checks only; not compilation or GPU/runtime proof."""
from pathlib import Path
import unittest
import subprocess
ROOT=Path(__file__).resolve().parents[3]
class ReleasedNativeIntegration(unittest.TestCase):
    def test_released_safety_owners_and_invocation_callers_unchanged(self):
        paths=('OptiScaler/dlssnr/NativeTemporalInputFacts.h',
               'OptiScaler/dlssnr/NativeTemporalInputs.h',
               'OptiScaler/dlssnr/NativeTextureContract.h',
               'OptiScaler/dlssnr/NativeSharedListInvocation.h',
               'OptiScaler/nr/d3d12/NativeRecordingState.h',
               'OptiScaler/nr/d3d12/NativeRecordingHooks.h',
               'OptiScaler/nr/d3d12/NativeIndirectSignatures.h',
               'OptiScaler/hooks/D3D12_Hooks.cpp',
               'OptiScaler/hooks/D3D12_Hooks.h',
               'OptiScaler/inputs/NVNGX_DLSS_Dx12.cpp')
        for path in paths:
            released=subprocess.check_output(['git','-c',f'safe.directory={ROOT.as_posix()}',
                '-C',str(ROOT),'show',f'7132a103:{path}'])
            self.assertTrue(released.replace(b'\r\n',b'\n')==(ROOT/path).read_bytes().replace(b'\r\n',b'\n'),
                            f'released safety owner/caller changed: {path}')
    def test_shader_is_conflict_free(self):
        text=(ROOT/'OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp').read_text(encoding='utf-8-sig')
        for marker in ('<<<<<<<','=======','>>>>>>>'):
            self.assertFalse(marker in text, f'unresolved marker {marker}')
    def test_released_scalar_and_view_guard_consumers_survive(self):
        text=(ROOT/'OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp').read_text(encoding='utf-8-sig')
        for anchor in ('NativeTemporalInputFacts::Resolve(', 'NativeTemporalInputFacts::Finite(',
                       'NativeTextureContract::ViewRefusal(', 'NativeTextureContract::ComposeSrvUse(',
                       'NativeTextureContract::ComposeUavUse(', 'frame.NativeScalarFacts = scalarFacts;',
                       'frame.NativeFlagFacts = flagFacts;', 'M::SelectNativeBindings(',
                       'frame.JitterSupplied = scalarFacts.JitterComplete();'):
            self.assertTrue(anchor in text, f'missing combined consumer {anchor}')
    def test_native_and_present_metadata_coexist(self):
        text=(ROOT/'OptiScaler/shaders/dlssnr/DlssNr_Common.h').read_text(encoding='utf-8-sig')
        for anchor in ('NativeScalarFacts','NativeFlagFacts','PresentNativeGuides','PreExposureSupplied','NativeFeatureGeneration'):
            self.assertTrue(anchor in text, f'missing frame metadata {anchor}')
if __name__=='__main__': unittest.main()
