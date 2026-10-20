from pathlib import Path
import re
root=Path(__file__).resolve().parents[2]
out=root/'builds/hub/rr-optional'
out.mkdir(parents=True,exist_ok=True)
source=(root/'OptiScaler/Config.h').read_text()
fields=re.findall(r'^\s*(CustomOptional<uint32_t(?:, \w+)?> DLSSDRenderPreset(?:ForAll|DLAA|UltraQuality|Quality|Balanced|Performance|UltraPerformance) \{ 0 \};)',source,re.M)
assert len(fields)==7
(out/'RrPresetFields.h').write_text('struct RrPresetFields {\n'+'\n'.join(fields)+'\n};\n')
