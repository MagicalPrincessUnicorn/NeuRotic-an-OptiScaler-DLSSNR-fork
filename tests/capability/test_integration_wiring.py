"""Safety invariants spanning capability retirement and the flagship renderer."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


class IntegrationWiringTests(unittest.TestCase):
    def test_retired_capability_probe_cannot_enter_provider(self):
        source = (ROOT / 'OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp').read_text()
        self.assertNotIn('ProbeProxyDispatch(', source)


if __name__ == '__main__':
    unittest.main()
