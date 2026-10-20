"""Run extracted production UI against real ImGui; no renderer or provider executes."""
from pathlib import Path
import subprocess, sys

root = Path(__file__).resolve().parents[1]
out = root / 'builds/ui-release/graph-compatibility'
out.mkdir(parents=True, exist_ok=True)
menu = (root / 'OptiScaler/menu/menu_common.cpp').read_text(encoding='utf-8')
graphs = menu[menu.index('void MenuCommon::RenderMainMenuGraphs('):menu.index('void MenuCommon::RenderMainMenuBottomBar(')]
# Instrument the submitted widget boundary, preserving the actual ImGui plot and dimensions.
graphs = graphs.replace('ImGui::PlotLines(', 'MeasuredPlotLines(')
graphs = graphs.replace('DlssNr::StatusPanel::Line(', 'MeasuredGraphLine(').replace('DlssNr::StatusPanel::Linef(', 'MeasuredGraphLinef(')
start = menu.index('    const auto ink = ImGui::GetStyleColorVec4(ImGuiCol_Text);', menu.index('##NativeMfgCompatibility'))
end = menu.index('\n\n    }', start)
statuses = menu[start:end]
# The observer records the actual strings/row rectangles after production formats its facts.
statuses = statuses.replace('DlssNr::StatusPanel::Line(', 'MeasuredLine(').replace('DlssNr::StatusPanel::Linef(', 'MeasuredLinef(')
template = (root / 'tests/menu_graph_compatibility_geometry.cpp.in').read_text(encoding='utf-8')
(out/'probe.cpp').write_text(template.replace('@@GRAPHS@@', graphs).replace('@@STATUSES@@', statuses), encoding='utf-8')
batch = '''@echo off
call "C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Auxiliary\\Build\\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /utf-8 /W3 /DNOMINMAX /I tests\\opticlip_stubs /I OptiScaler /I OptiScaler\\include /I external\\freetype builds\\ui-release\\graph-compatibility\\probe.cpp OptiScaler\\menu\\Localization.cpp /I external\\nlohmann OptiScaler\\menu\\localization\\LanguageRuntime.cpp OptiScaler\\menu\\localization\\LanguagePack.cpp OptiScaler\\menu\\localization\\LanguageCatalog.cpp OptiScaler\\menu\\localization\\LanguageBundles.cpp OptiScaler\\include\\imgui\\imgui.cpp OptiScaler\\include\\imgui\\imgui_draw.cpp OptiScaler\\include\\imgui\\imgui_widgets.cpp OptiScaler\\include\\imgui\\imgui_tables.cpp OptiScaler\\include\\imgui\\misc\\freetype\\imgui_freetype.cpp /Fe:builds\\ui-release\\graph-compatibility\\probe.exe /Fo:builds\\ui-release\\graph-compatibility\\ /link /INCREMENTAL:NO user32.lib shell32.lib external\\freetype\\freetype.lib
exit /b %errorlevel%
'''
(out/'build.cmd').write_text(batch)
build = subprocess.run(['cmd', '/c', str(out/'build.cmd')], cwd=root, capture_output=True, text=True)
(out/'build.txt').write_text(build.stdout+build.stderr)
if build.returncode:
    print(build.stdout+build.stderr)
    sys.exit(build.returncode)
run = subprocess.run([str(out/'probe.exe')], cwd=root, capture_output=True, text=True)
(out/'result.txt').write_text(run.stdout+run.stderr)
print(run.stdout+run.stderr)
sys.exit(run.returncode)
