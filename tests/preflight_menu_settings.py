"""Compile a focused INI fixture from the actual Config load/save/parser seams.

Run with an x64 MSVC environment. No game, provider, or renderer is loaded.
"""
from pathlib import Path
import re
import subprocess

root = Path(__file__).resolve().parents[1]
header = (root / 'OptiScaler/Config.h').read_text(encoding='utf-8')
source = (root / 'OptiScaler/Config.cpp').read_text(encoding='utf-8')
menu = (root / 'OptiScaler/dlssnr/DlssNr_Menu.cpp').read_text(encoding='utf-8')

field = re.search(r'CustomOptional<bool> MenuPreflightExpanded\s*\{ true \};', header)
assert field, 'Checklist must have an expanded default independent of NR settings'
load = re.search(r'MenuPreflightExpanded\.set_from_config\(readBool\("Menu", "PreflightExpanded"\)\);', source)
save = re.search(r'ini\.SetBoolValue\("Menu", "PreflightExpanded", Instance\(\)->MenuPreflightExpanded\.value_or_default\(\)\);', source)
assert load and save, 'Checklist preference must use the production Config load/save boundary'
diagnostics = menu[menu.index('if (page == MenuPage::Diagnostics)'):menu.index('basicOwnsMain = BasicMultipass::Active(config->GetDlssNrConfigSnapshot())')]
assert 'RenderPreflight(config, PreflightView::Signals);' in diagnostics, 'Diagnostics always renders the signal chart'
assert 'Signal Status###NrPreflight' not in diagnostics, 'Signal chart has no accordion'

def method(signature):
    start = source.index(signature)
    body = source.index('{', start)
    depth = 1
    end = body + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end].replace('Config::', '')

out = root / 'builds/ui-release/preflight-settings'
out.mkdir(parents=True, exist_ok=True)
fixture = '''#include <algorithm>
#include <cctype>
#include <cstdio>
#include <format>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <vector>
#include "OptiScaler/CustomOptional.h"
#include "external/simpleini/SimpleIni.h"
CSimpleIniA ini;
std::vector<std::string> _log;
''' + field.group() + '\n' + method('std::optional<std::string> Config::readString') + '\n' + method('std::optional<bool> Config::readBool') + '''
void require(bool ok) { if (!ok) throw std::runtime_error("INI round-trip mismatch"); }
void loadPreference() { ''' + load.group() + ''' }
void savePreference() { ''' + save.group().replace('Instance()->', '') + ''' }
int main() {
  try {
    for (const char* value : {"<missing>", "auto", "false", "true", "FALSE", "invalid", ""}) {
      ini.Reset(); MenuPreflightExpanded.reset();
      std::string text="[Menu]\\nLanguage=de\\nScale=1.3\\nFuturePreference=untouched\\n";
      if (std::string(value)!="<missing>") text += "PreflightExpanded="+std::string(value)+"\\n";
      text += "[DLSSNR]\\nEnabled=false\\nRoute=0\\nPresentResolution=3\\n[Private]\\nFutureKey=keep\\n";
      require(ini.LoadData(text.data(),text.size())==SI_OK);
      loadPreference();
      const bool expected=std::string(value)!="false" && std::string(value)!="FALSE";
      require(MenuPreflightExpanded.value_or_default()==expected);
      for (bool expanded : {expected, false, true}) {
        MenuPreflightExpanded=expanded; savePreference();
        std::string saved; require(ini.Save(saved)==SI_OK);
        ini.Reset(); require(ini.LoadData(saved.data(),saved.size())==SI_OK);
        MenuPreflightExpanded.reset(); loadPreference();
        require(MenuPreflightExpanded.value_or_default()==expanded);
        require(std::string(ini.GetValue("Menu","FuturePreference",""))=="untouched");
        require(std::string(ini.GetValue("Menu","Language",""))=="de");
        require(std::string(ini.GetValue("Menu","Scale",""))=="1.3");
        require(std::string(ini.GetValue("DLSSNR","Enabled",""))=="false");
        require(std::string(ini.GetValue("DLSSNR","Route",""))=="0");
        require(std::string(ini.GetValue("DLSSNR","PresentResolution",""))=="3");
        require(std::string(ini.GetValue("Private","FutureKey",""))=="keep");
      }
    }
    std::puts("PASS: expanded default, missing/auto/invalid inputs, true/false/reopen persistence, explicit NR and unknown INI preservation");
  } catch(const std::exception& e) { std::fprintf(stderr,"FAIL: %s\\n",e.what()); return 1; }
}
'''
cpp = out / 'fixture.cpp'
cpp.write_text(fixture, encoding='utf-8')
subprocess.run(['cl', '/nologo', '/std:c++20', '/EHsc', '/W3', '/I' + str(root), str(cpp), '/Fe:' + str(out / 'test.exe'), '/Fo:' + str(out / 'fixture.obj')], cwd=root, check=True)
subprocess.run([str(out / 'test.exe')], cwd=root, check=True)
print('PASS: always-visible Diagnostics chart and retained legacy checklist preference')
