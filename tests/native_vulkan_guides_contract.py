"""Connected native acquisition contract; runtime fixture covers Vulkan bytes."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]

class NativeCaptureContract(unittest.TestCase):
    def test_connected_completed_transport(self):
        path = ROOT / 'OptiScaler/dlssnr/NativeVulkanGuides.cpp'
        self.assertTrue(path.exists(), 'Native capture-to-processor owner is missing')
        source = path.read_text()
        for required in ['vkCmdCopyImageToBuffer', 'vkWaitForFences', 'NativeGuides::Process',
                         'Outcome::Delivered', 'vkCmdCopyBufferToImage', 'SingleUsePrimary', 'ProducerProof']:
            self.assertIn(required, source)
        self.assertNotIn('#include <reshade', source.lower())

if __name__ == '__main__':
    unittest.main()
