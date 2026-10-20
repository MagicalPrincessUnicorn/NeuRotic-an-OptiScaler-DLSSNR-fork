"""One-time migration of UI-source char-pointer arrays; never a runtime spelling map.

Original pointers/control IDs and option values stay unchanged. This also keeps
technical values bindable when a UI uses the same array for its choice caption.
"""
import ast,hashlib,json,re
import inventory
from presentation_arguments import mask,arguments
def migrate():
    entries=inventory.load();rows=[]
    for path in inventory.paths():
        source=path.read_text(encoding='utf-8-sig');code=mask(source);bound=[];edits=[]
        for m in re.finditer(r'Neurotic::Ui(?:Literal|Message|Options)\s*\(',code):
            _,end=arguments(code,code.index('(',m.start()));bound.append((m.start(),end))
        for m in re.finditer(r'\bconst\s+char\s*\*\s*(?:const\s+)?\w+\s*\[[^\]]*\]\s*=\s*\{',code):
            if re.search(r'constexpr\s*$',code[max(0,m.start()-30):m.start()]):continue
            _,end=arguments(code,code.index('{',m.start()))
            for literal in inventory.TOKEN.finditer(source,m.start(),end):
                if not literal[0].startswith('"') or any(a<=literal.start()<b for a,b in bound):continue
                text=ast.literal_eval(literal[0]);surface='desktop' if '/apps/' in path.as_posix() else 'in_game'
                if not text or text.startswith(('##','https://','http://')) or not re.search('[A-Za-z]{2}',text) or '\0' in text:continue
                entry=next((e for e in entries if e['surface']==surface and e['english']==text),None)
                if not entry:
                    entry=dict(id=('desktop.' if surface=='desktop' else 'ingame.')+'option.'+hashlib.sha256(text.encode()).hexdigest()[:12],surface=surface,section='options',control='label',english=text,context='UI-source option array: '+path.relative_to(inventory.ROOT).as_posix(),placeholders=inventory.placeholders(text),preserve=[],line_breaks=False,revision=1);entries.append(entry)
                edits.append((literal.start(),literal.end(),'Neurotic::UiLiteral('+json.dumps(entry['id'])+', '+literal[0]+')'))
                rows.append(dict(path=path.relative_to(inventory.ROOT).as_posix(),line=source.count('\n',0,literal.start())+1,id=entry['id'],english=text))
        for a,b,text in sorted(set(edits),reverse=True):source=source[:a]+text+source[b:]
        if edits:path.write_text(source,encoding='utf-8')
    inventory.validate(entries);(inventory.DEST/'english.json').write_text(json.dumps(sorted(entries,key=lambda e:e['id']),ensure_ascii=False,indent=2)+'\n',encoding='utf-8');inventory.generate()
    (inventory.ROOT/'docs/implementation-evidence/community-languages/ARRAY_BINDINGS.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print('Bound previously raw UI-source array entries:',len(rows))
if __name__=='__main__':migrate()
