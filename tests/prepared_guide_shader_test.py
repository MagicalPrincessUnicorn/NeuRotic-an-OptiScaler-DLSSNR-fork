"""Contract checks and independent math references; these are not GPU qualification."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
SHADERS = ROOT / "addons/prepared-guides/shaders"


def patch_error(current, previous):
    mc, mp = sum(current) / 9, sum(previous) / 9
    return sum(abs((c - mc) - (p - mp)) for c, p in zip(current, previous)) / 9


class PreparedGuideShaderTests(unittest.TestCase):
    def source(self):
        return (SHADERS / "NeuRotic_PreparedGuides.fx").read_text(encoding="utf-8")

    def test_complete_camera_exclusion(self):
        source = self.source()
        for forbidden in ("GEOM_", "FitSamples", "FitSolve", "PredictMV", "Cam0", "DLSS5_BASIS", "synthetic_jitter"):
            self.assertNotIn(forbidden, source)

    def test_host_defaults_cannot_accept_history(self):
        source = self.source()
        for declaration in ("uniform bool NRPG_HostEnabled = false;", "uniform bool NRPG_HistoryValid = false;", "uniform bool NRPG_Reset = true;"):
            self.assertIn(declaration, source)
        self.assertIn("NRPG_HostFrame == NRPG_RuntimeFrame", source)
        self.assertIn("stamp.x == float(NRPG_PreviousCapture)", source)
        self.assertIn("stamp.y == float(NRPG_Generation)", source)
        self.assertIn("!NRPG_Reset && NRPG_HistoryValid", source)

    def test_output_semantics_and_provider_interfaces(self):
        source = self.source()
        for name, fmt in (("NRPG_Motion", "RG16F"), ("NRPG_RawDepth", "R32F"), ("NRPG_Distrust", "R8"), ("NRPG_Status", "RGBA8")):
            self.assertRegex(source, rf"texture\s+{name}\s*\{{[^}}]*Format\s*=\s*{fmt}")
        for name in ("texMotionVectors", "MotVectTexVort", "Deferred::MotionVectorsTex", "Kernel::tFlow", "QuantMotion::tFlow", "RenderTargetWriteMask = 4"):
            self.assertIn(name, source)
        self.assertIn("mv * float2(BUFFER_WIDTH, BUFFER_HEIGHT)", source)
        self.assertIn("if (!history_ok)", source)
        self.assertIn("distrust = 1.0;", source)

    def test_patch_appearance_invariance(self):
        current = [0.1, 0.3, 0.2, 0.8, 0.4, 0.7, 0.5, 0.6, 0.9]
        self.assertAlmostEqual(patch_error(current, [x + 0.15 for x in current]), 0, places=12)
        self.assertGreater(patch_error(current, list(reversed(current))), 0.1)

    def test_pixel_units_and_outside(self):
        uv_motion = (-8 / 1920, 3 / 1080)
        self.assertEqual(tuple(round(v * size) for v, size in zip(uv_motion, (1920, 1080))), (-8, 3))
        self.assertLess(0.001 + uv_motion[0], 0)

    def test_builtin_flow_has_one_pixel_to_uv_conversion(self):
        source = self.source()
        self.assertIn("NRPG_BUILTIN_MOTION", source)
        self.assertIn("tex2Dlod(sNRPG_ProviderMV, c).xy * BUFFER_PIXEL_SIZE", source)
        self.assertIn("max(distrust, tex2Dlod(sNRPG_BuiltinDistrust", source)

    def test_static_hysteresis_is_hard_decision(self):
        # First structured static win retains motion and distrusts history.
        def decision(won, previous):
            return (0.0 if won and previous else 2.0, 0.35 if won and not previous else 0.0)
        self.assertEqual(decision(True, False), (2.0, 0.35))
        self.assertEqual(decision(True, True), (0.0, 0.0))
        source = self.source()
        self.assertIn("won_before <= 0.5", source)
        self.assertIn("mv = zero_vector ? float2(0.0, 0.0) : flow;", source)

    def test_optional_color_is_sdr_and_same_capture_only(self):
        source = (SHADERS / "NeuRotic_SourceGuidedColor.fx").read_text(encoding="utf-8")
        self.assertIn("BUFFER_COLOR_SPACE == 1 && BUFFER_COLOR_BIT_DEPTH == 8", source)
        self.assertIn("NRSG_HostFrame == NRSG_Frame", source)
        self.assertIn("source.a != float(NRSG_Capture)", source)
        self.assertIn("NRSG_ProcessedCapture != NRSG_Capture", source)
        self.assertIn("NRSG_ProcessedGeneration != NRSG_Generation", source)
        self.assertIn("stamp.z != float(NRSG_Frame)", source)
        self.assertIn("for (int dy = -4; dy <= 4; ++dy)", source)


if __name__ == "__main__":
    unittest.main()
