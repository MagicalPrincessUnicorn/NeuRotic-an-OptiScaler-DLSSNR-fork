"""Focused production-expression and source-wiring checks; no renderer qualification."""
from pathlib import Path
import re, unittest
root=Path(__file__).resolve().parents[2]
class BackendSettingsTests(unittest.TestCase):
 def test_rr_priority_and_auto(self):
  src=(root/'OptiScaler/upscalers/dlssd/DLSSDFeature.cpp').read_text()
  for quality in ['DLAA','UltraQuality','Quality','Balanced','Performance','UltraPerformance']:
   match=re.search(r'RenderPreset'+quality+r' = (.*?);',src[src.index('if (Config::Instance()->DLSSDRenderPresetOverride.value_or_default())',src.index('uint32_t RenderPresetDLAA')):],re.S)
   expression=match[1].replace('Config::Instance()->','').replace('\n','')
   # Execute the production nested value_or expression against controlled optional values.
   class Optional:
    def __init__(self,v): self.v=v
    def value_or(self,fallback): return fallback if self.v is None else self.v
   for per,global_,game,expected in [(None,None,7,7),(None,4,7,4),(9,4,7,9),(0,4,7,0)]:
    env={'DLSSDRenderPreset'+quality:Optional(per),'DLSSDRenderPresetForAll':Optional(global_),'RenderPreset'+quality:game}
    self.assertEqual(eval(expression,{},env),expected,(quality,per,global_))
 def test_external_rr_flag(self):
  src=(root/'OptiScaler/upscalers/dlssd/DLSSDFeature.cpp').read_text()
  self.assertTrue('if (State::Instance().dlssdPresetsOverriddenExternally)' in src, 'RR external override branch uses RR state')
  self.assertFalse('dlssPresetsOverriddenExternally' in src, 'RR branch must not depend on SR state')
 def test_vulkan_persistence(self):
  src=(root/'OptiScaler/Config.cpp').read_text()
  for name in ['Inputs','Output']:
   field='VulkanUseCopyFor'+name; key='UseCopyFor'+name
   self.assertTrue(field+'.set_from_config(readBool("Vulkan", "'+key+'"))' in src, field+' INI load')
   self.assertRegex(src,r'ini.SetValue\("Vulkan", "'+key+r'",\s*GetBoolValue\(Instance\(\)->'+field+r'.value_for_config\(\)\).c_str\(\)\)')
 def test_da_clamp_truth_table(self):
  src=(root/'OptiScaler/shaders/rcas/RCAS_Common.cpp').read_text()
  expr=re.search(r'OutConstants.ClampOutput = (.*?);',src)[1]
  for setting in [None,False,True]:
   for hdr in [False,True]:
    default=not hdr if '!InConstants.IsHdr' in expr else hdr
    value=default if setting is None else setting
    true_,false_=map(int,re.search(r'\?\s*(\d+)\s*:\s*(\d+)',expr).groups())
    self.assertEqual(true_ if value else false_,int(not hdr if setting is None else setting))
  for path in list((root/'OptiScaler/shaders/rcas/precompile').glob('da_*sharpen.hlsl'))+list((root/'OptiScaler/shaders/rcas/shaders').glob('DA_*Shader.h')):
   shader=path.read_text(); self.assertIn('if (ClampOutput > 0)',shader,str(path))
 def test_quirk_reset_and_print_coverage(self):
  src=(root/'OptiScaler/dllmain.cpp').read_text()
  block=src[src.index('if (quirks & GameQuirk::DisableFSR2Inputs &&'):]; block=block[:block.index('\n\n')]
  self.assertIn('quirks.reset(GameQuirk::DisableFSR2Inputs)',block)
  enum=(root/'OptiScaler/misc/Quirks.h').read_text().split('enum class GameQuirk')[1].split('};')[0]
  names=re.findall(r'^\s*(\w+)\s*,',enum,re.M)
  printed=src[src.index('static void printQuirks'):src.index('static void CheckQuirks')]
  self.assertEqual([n for n in names if 'GameQuirk::'+n not in printed],[])
if __name__=='__main__': unittest.main(verbosity=2)
