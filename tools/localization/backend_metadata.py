"""Persist explicit metadata IDs for the current saved-setting provider.

The runtime joins by section/key (machine identity), never English spelling.
Manual CMD continues to consume the original English fields.
"""
from pathlib import Path
import json,re,hashlib,importlib.util
ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('inventory',ROOT/'tools/localization/inventory.py');inv=importlib.util.module_from_spec(spec);spec.loader.exec_module(inv)
def build():
 entries=inv.load();known={(e['surface'],e['english']):e['id'] for e in entries};metadata={}
 def entry(text,section,control='label'):
  text=text.strip()
  if not text:return ''
  if ('desktop',text) in known:return known['desktop',text]
  id='desktop.settings.'+section.lower()+'.'+hashlib.sha256(text.encode()).hexdigest()[:10]
  entries.append(dict(id=id,surface='desktop',section='saved-settings',control=control,english=text,context='Saved next-launch preference: '+section,placeholders=inv.placeholders(text),preserve=[term for term in ('NeuRotic','DLSS','NR Anything','OptiScaler.ini','ReShade') if term in text],line_breaks=control=='paragraph',revision=1,sources=[dict(path='tools/packaging/customer/support/NeuRotic-HubSettings.ps1',line=0,kind='typed metadata')]))
  known['desktop',text]=id;return id
 p=ROOT/'tools/packaging/customer/support/NeuRotic-HubSettings.ps1';s=p.read_text(encoding='utf-8-sig')
 for line in s.splitlines():
  m=re.match(r"\s*New-HubSetting\s+(\S+)\s+(\S+)\s+'((?:[^']|'')*)'",line)
  if not m:continue
  section,key,label=m.groups();row=dict(labelId=entry(label.replace("''","'"),section+'/'+key),descriptionId='',valueLabelIds={})
  desc=re.search(r'-Description\s+(?:\x27((?:[^\x27]|\x27\x27)*)\x27|"([^"$]*)")',line)
  if desc:row['descriptionId']=entry((desc[1] if desc[1] is not None else desc[2]).replace("''","'"),section+'/'+key,'paragraph')
  values=re.search(r'-ValueLabels\s+@\{([^}]*)\}',line)
  if values:
   for v in re.finditer(r"'([^']*)'\s*=\s*'((?:[^']|'')*)'",values[1]):row['valueLabelIds'][v[1]]=entry(v[2].replace("''","'"),section+'/'+key+'/value')
  metadata[section+'/'+key]=row
 region=['# BEGIN GENERATED LANGUAGE METADATA (backend_metadata.py)','$script:HubSettingLanguageMetadata=@{']
 for key,row in metadata.items():region.append(" '"+key+"'=@{labelId='"+row['labelId']+"';descriptionId='"+row['descriptionId']+"';valueLabelIds=@{"+';'.join("'"+k+"'='"+v+"'" for k,v in row['valueLabelIds'].items())+'}}')
 region+=['}','# END GENERATED LANGUAGE METADATA']
 s=re.sub(r'# BEGIN GENERATED LANGUAGE METADATA[\s\S]*?# END GENERATED LANGUAGE METADATA\n?','',s)
 s='\n'.join(region)+'\n'+s
 s=s.replace('[pscustomobject]@{section=$Section;key=$Key;label=$Label;',"$localized=$script:HubSettingLanguageMetadata[$Section+'/'+$Key]\n [pscustomobject]@{section=$Section;key=$Key;labelId=$localized.labelId;descriptionId=$localized.descriptionId;valueLabelIds=$localized.valueLabelIds;label=$Label;")
 s=s.replace('[pscustomobject]@{section=$schema.section;key=$schema.key;label=$schema.label;','[pscustomobject]@{section=$schema.section;key=$schema.key;labelId=$schema.labelId;descriptionId=$schema.descriptionId;valueLabelIds=$schema.valueLabelIds;label=$schema.label;')
 p.write_text(s,encoding='utf-8-sig')
 # The supported result envelope exposes explicit codes for complete messages.
 p=ROOT/'tools/packaging/customer/support/NeuRotic-HubProtocol.ps1';s=p.read_text(encoding='utf-8-sig')
 def reason(m):
  text=m[1].replace("''","'");id=entry(text,'installer-result','paragraph')
  return "reasonCode='"+id+"';messageParameters=@{};reason='"+m[1]+"'"
 s=re.sub(r"(?<![A-Za-z])reason='((?:[^']|'')*)'",reason,s);p.write_text(s,encoding='utf-8-sig')
 inv.validate(entries);(ROOT/'localization/english.json').write_text(json.dumps(sorted(entries,key=lambda e:e['id']),ensure_ascii=False,indent=2)+'\n',encoding='utf-8');inv.generate()
 print('PASS stable setting metadata',len(metadata),'provider identities')
if __name__=='__main__':build()
