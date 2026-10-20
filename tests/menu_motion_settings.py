"""Compile and exercise production Config animation preference seams, without a game."""
from pathlib import Path
import re
import subprocess

root = Path(__file__).resolve().parents[1]
header = (root / 'OptiScaler/Config.h').read_text(encoding='utf-8')
source = (root / 'OptiScaler/Config.cpp').read_text(encoding='utf-8')
field = re.search(r'CustomOptional<bool> MenuReduceMotion\s*\{ false \};', header)
load = re.search(r'MenuReduceMotion\.set_from_config\(readBool\("Menu", "ReduceMotion"\)\);', source)
save = re.search(r'ini\.SetValue\("Menu", "ReduceMotion", GetBoolValue\(Instance\(\)->MenuReduceMotion\.value_for_config\(\)\)\.c_str\(\)\);', source)
assert field and load and save, 'Animation preference must have a default and actual load/save seams'

def method(signature):
    start = source.index(signature)
    body = source.index('{', start)
    depth, end = 1, body + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end].replace('Config::', '')

out = root / 'builds/hub/hotkey-chord'
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
''' + field.group() + '\n' + method('std::optional<std::string> Config::readString') + '\n' + method('std::optional<bool> Config::readBool') + '\n' + method('std::string GetBoolValue') + '''
void require(bool ok) { if (!ok) throw std::runtime_error("animation preference round-trip mismatch"); }
void loadPreference() { ''' + load.group() + ''' }
void savePreference() { ''' + save.group().replace('Instance()->', '') + ''' }
int main() {
  try {
    for (const char* value : {"<missing>", "auto", "false", "true", "TRUE", "invalid", ""}) {
      ini.Reset(); MenuReduceMotion.reset();
      std::string text="[Menu]\\nLightTheme=true\\nShortcutKey=375\\nUnknown=untouched\\n";
      if (std::string(value)!="<missing>") text += "ReduceMotion="+std::string(value)+"\\n";
      text += "[DLSSNR]\\nEnabled=false\\nRoute=2\\n";
      require(ini.LoadData(text.data(),text.size())==SI_OK);
      loadPreference();
      const bool expected=std::string(value)=="true" || std::string(value)=="TRUE";
      require(MenuReduceMotion.value_or_default()==expected);
      savePreference();
      // CustomOptional normalizes the default false to auto when saving.
      require(std::string(ini.GetValue("Menu","ReduceMotion",""))==(expected?"true":"auto"));
      for (bool disabled : {false,true}) {
        MenuReduceMotion=disabled; savePreference();
        std::string saved; require(ini.Save(saved)==SI_OK);
        ini.Reset(); require(ini.LoadData(saved.data(),saved.size())==SI_OK);
        MenuReduceMotion.reset(); loadPreference();
        require(MenuReduceMotion.value_or_default()==disabled);
        require(std::string(ini.GetValue("Menu","LightTheme",""))=="true");
        require(std::string(ini.GetValue("Menu","ShortcutKey",""))=="375");
        require(std::string(ini.GetValue("Menu","Unknown",""))=="untouched");
        require(std::string(ini.GetValue("DLSSNR","Route",""))=="2");
      }
    }
    std::puts("PASS animation default, missing/auto/invalid inputs, explicit off/on reopen, automatic token, saved theme/key/unknown-setting preservation");
  } catch(const std::exception& e) { std::fprintf(stderr,"FAIL: %s\\n",e.what()); return 1; }
}
'''
cpp = out / 'motion_fixture.cpp'
cpp.write_text(fixture, encoding='utf-8')
subprocess.run(['cl', '/nologo', '/std:c++20', '/EHsc', '/W3', '/I'+str(root), str(cpp), '/Fe:'+str(out/'motion-test.exe'), '/Fo:'+str(out/'motion_fixture.obj')], cwd=root, check=True)
subprocess.run([str(out/'motion-test.exe')], cwd=root, check=True)
