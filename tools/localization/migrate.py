"""One-time stable-ID binding migration; audit records every reviewed literal.

Only current product providers participate. Original literal bytes remain the
ImGui label/identity; formatting and drawing resolve their explicit catalog ID.
The resulting source, rather than this migration, is the runtime lookup map.
"""
from pathlib import Path
import ast,importlib.util,json,re
ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('inventory',ROOT/'tools/localization/inventory.py');inv=importlib.util.module_from_spec(spec);spec.loader.exec_module(inv)
def apply():
 entries=inv.load();by={(e['surface'],e['english']):e for e in entries};report=[]
 for p in inv.paths():
  if p.name=='LanguageStore.cpp':continue
  source=p.read_text(encoding='utf-8-sig');surface='desktop' if 'apps/NeuRoticHub/' in p.as_posix() else 'in_game';edits=[];wrapped=False
  tokens=list(inv.TOKEN.finditer(source));skip_until=0
  # Only the enclosing binding is exempt. One compressed source line may have
  # several separate controls; a bound sibling does not cover them.
  from presentation_arguments import mask,arguments
  code=mask(source);ranges=[]
  for call in re.finditer(r'Neurotic::Ui(?:Literal|Options|Message)\s*\(',code):
   _,stop=arguments(code,code.index('(',call.start()));ranges.append((call.start(),stop))
  for i,m in enumerate(tokens):
   if m.start()<skip_until:continue
   raw=m.group();line=source.count('\n',0,m.start())+1;line_text=source.splitlines()[line-1];reason=''
   if not raw.startswith('"'):
    continue
   # Consolidate adjacent C++ literals before wrapping so concatenation stays valid.
   end=m.end();group=[raw]
   for n in tokens[i+1:]:
    if n.group().startswith('"') and not source[end:n.start()].strip():group.append(n.group());end=n.end()
    else:break
   skip_until=end
   try:value=''.join(ast.literal_eval(s) for s in group)
   except (ValueError,SyntaxError):continue
   if line_text.lstrip().startswith('#'):reason='preprocessor/include'
   elif re.search(r'\b(?:LOG_[A-Z]+|spdlog::\w+)\s*\(',line_text):reason='developer log'
   elif any(a<=m.start()<b for a,b in ranges):reason='already bound or stable ID'
   elif re.search(r'\b(?:static_assert|static constexpr.*(?:array|char))',line_text) and p.name=='CharacterCohort.h':reason='immutable cohort identity'
   visible=value.split('##')[0].strip();entry=by.get((surface,visible))
   if not reason and entry and '\0' not in value:
    expression='Neurotic::UiLiteral('+json.dumps(entry['id'])+', '+source[m.start():end]+')';edits.append((m.start(),end,expression));wrapped=True
    for hint,control in [('Button','button'),('Checkbox','checkbox'),('Toggle','checkbox'),('Header','header'),('SetTooltip','tooltip'),('TextColored','warning')]:
     if hint+'(' in line_text:entry['control']=control;break
    report.append(dict(path=p.relative_to(ROOT).as_posix(),line=line,text=visible,id=entry['id'],kind='binding'));continue
   if not reason and '\0' in value:
    segments=value.split('\0');ids=[]
    for segment in segments:
     e=by.get((surface,segment.strip()));ids.append(e['id'] if e else '')
    if any(ids):edits.append((m.start(),end,'Neurotic::UiOptions('+json.dumps('|'.join(ids))+', '+source[m.start():end]+')'));wrapped=True;report.append(dict(path=p.relative_to(ROOT).as_posix(),line=line,text=value,kind='option bindings'));continue
   if not reason:reason='raw configuration/API key, filename, numeric unit or user data; not an authored prose template' if not entry else 'provider requires typed message wiring'
   report.append(dict(path=p.relative_to(ROOT).as_posix(),line=line,text=value,kind='exemption',reason=reason))
  for start,end,text in reversed(edits):source=source[:start]+text+source[end:]
  if wrapped:
   include='#include <menu/Localization.h>' if surface=='in_game' else '#include "../../../OptiScaler/menu/Localization.h"'
   # Nested App provider headers have differing depth; use project include root.
   if surface=='desktop':include='#include <menu/Localization.h>'
   if include not in source:source=include+'\n'+source
   source=re.sub(r'\binline constexpr (?=(?:const )?char\*)','inline const ',source)
   source=re.sub(r'\b(?:static )?constexpr (?=(?:const )?char\*)','const ',source)
   p.write_text(source,encoding='utf-8')
 (ROOT/'localization/english.json').write_text(json.dumps(entries,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');inv.generate()
 (ROOT/'docs/implementation-evidence/community-languages/SOURCE_BINDINGS.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
 print('PASS source migration',sum(r['kind']=='binding' for r in report),'explicit bindings; exemptions require state review')
if __name__=='__main__':apply()
