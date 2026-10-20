"""One shared platform tick per menu frame, independent of renderer and NR state."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]

def code(path):
    text = (ROOT / path).read_text(encoding="utf-8")
    return re.sub(r"//[^\n]*|/\*[\s\S]*?\*/", "", text)

class MenuFrameClockTests(unittest.TestCase):
    def test_platform_tick_has_one_owner(self):
        owners = []
        for path in (ROOT / "OptiScaler/menu").glob("menu*.cpp"):
            text = code(path.relative_to(ROOT))
            owners.extend([path.name] * len(re.findall(r"\bImGui_ImplWin32_NewFrame\b", text)))
        self.assertEqual(owners, ["menu_common.cpp"],
                         "Renderer-local ticks overwrite elapsed time before the shared UI frame")

    def test_shared_tick_precedes_input_and_ui_frame(self):
        text = code("OptiScaler/menu/menu_common.cpp")
        start = text.index("void MenuCommon::BeginMenuFrameIfNeeded(")
        body = text[start:text.index("void MenuCommon::RenderSplashWindow(", start)]
        self.assertEqual(body.count("PollMenuPlatform(ImGui_ImplWin32_NewFrame)"), 1)
        self.assertEqual(body.count("ImGui_ImplUwp_NewFrame(displaySize)"), 1)
        self.assertLess(body.index("PollMenuPlatform("), body.index("OptiInput::FeedImGui("))
        self.assertLess(body.index("OptiInput::FeedImGui("), body.index("ImGui::NewFrame()"))
        self.assertIn("if (!_isUWP)", body)

if __name__ == "__main__":
    unittest.main()
