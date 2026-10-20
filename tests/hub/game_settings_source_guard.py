"""Check the expanded allowlist against actual Config reader/writer seams."""
from pathlib import Path
import json,re
r=Path(__file__).resolve().parents[2]
schema=json.loads((r/'builds/hub/settings-mirror/schema.json').read_text(encoding='utf-8-sig'))
c=(r/'OptiScaler/Config.cpp').read_text(encoding='utf-8')
character=(r/'OptiScaler/nr/semantic/character/CharacterInspectorSettings.h').read_text()
basic=(r/'OptiScaler/dlssnr/DlssNr_BasicMultipass.h').read_text()
present_resolution=(r/'OptiScaler/dlssnr/DlssNr_PresentResolution.h').read_text()
present_input=(r/'OptiScaler/dlssnr/DlssNr_PresentInputPolicy.h').read_text()
errors=[]
for field in schema:
    section,key=field['section'],field['key']
    if section.startswith('DlssNrLayer'):
        # All additional profiles use the same loader/saver keys as layer 2.
        section='DlssNrLayer2'
    if section=='CharacterInspector':
        assert f'"CharacterInspector","{key}"' in character,(section,key)
        continue
    if section=='DlssNrBasic':
        assert f'"{key}"' in basic and f'"DlssNrBasic", "{key}"' in basic,(section,key)
        continue
    if section=='DlssNr' and key in ('PresentResolution','PresentCustomScale','EnhancedResolution','EnhancedCustomScale'):
        assert f'read("{key}")' in present_resolution and f'ini.SetLongValue("DlssNr", "{key}"' in present_resolution
        continue
    if section=='DlssNr' and key=='PresentInputPolicy':
        assert 'readUInt("DlssNr", "PresentInputPolicy")' in c and 'ini.SetLongValue("DlssNr", "PresentInputPolicy"' in present_input
        continue
    # Explicit key occurrences include the custom Present policy/resolution,
    # MFG compatibility, enabled-state, and enum readers in this same owner.
    read_key=re.search(r'"'+re.escape(section)+r'",\s*"'+re.escape(key)+r'"',c[:c.index('bool Config::SaveIni(')])
    write_key=re.search(r'ini\.Set(?:Bool|Value|Long|Double)\w*\("'+re.escape(section)+r'",\s*"'+re.escape(key)+r'"',c[c.index('bool Config::SaveIni('):])
    if not read_key or not write_key:errors.append(f'{section}/{key}: reader={bool(read_key)} writer={bool(write_key)}')
    # Direct boolean readers must have a bounded boolean allowlist.
    boolean=re.search(r'readBool\("'+re.escape(section)+r'",\s*"'+re.escape(key)+r'"\)',c)
    if boolean and (set(field['values'])-{'auto','true','false'} or not field['values']):
        errors.append(f'{section}/{key}: boolean type mismatch')
    float_read=re.search(r'readFloat\("'+re.escape(section)+r'",\s*"'+re.escape(key)+r'"\)',c)
    if float_read and field['type']=='integer':errors.append(f'{section}/{key}: integer UI mismatches float reader')
assert not errors,'\n'.join(errors)
assert len(schema)<=512
for section,last in [('DLSS',15),('DLSSD',6)]:
    for field in schema:
        if field['section']==section and field['key'].startswith('RenderPreset') and field['key']!='RenderPresetOverride':
            assert field['values']==['auto']+[str(i) for i in range(last+1)]+['16777215']
for field in schema:
    if field['section'].startswith('DlssNrLayer') and field['key']=='ReversibleMode':
        assert field['values']==['auto','0','1','2']
print(f'PASS: {len(schema)} unique editable fields have actual Config readers and writers, matching direct boolean/float types, bounded presets and per-pass proxy enums')
view=(r/'apps/NeuRoticHub/ui/GameSettingsView.h').read_text(encoding='utf-8')
disabled={(m[1],m[2]) for m in re.finditer(r'\{\d,\d,"([^"]+)","([^"]+)","[^"]+","[^"]+"\}',view)}
editable={(f['section'],f['key']) for f in schema}
assert not editable.intersection(disabled),'A disabled control also has an editable writer'
prior=json.loads((r/'docs/implementation-evidence/app-refinement-20261004/overlay-app-unmapped-editable-candidates.json').read_text())
assert all((f['section'],f['key']) in editable|disabled for f in prior),'A previously identified saved control is not accounted for'
assert 'INI editor' not in view
assert 'SavedSettingsUnavailable(' not in view and 'RenderSavedSettingsRuntime(' not in view
assert 'RenderSavedNrObservations(' not in view
assert 'DrawObjectRuleHost(field,model.settings)' in view
print(f'PASS: all {len(prior)} previously identified saved-control gaps are editable or intentionally preserved; {len(disabled)} runtime-dependent saved keys remain untouched without disabled desktop controls')
