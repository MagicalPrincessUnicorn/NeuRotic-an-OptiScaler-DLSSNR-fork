"""Canonical current UI inventory. IDs are persisted; English is editable data.

seed discovers candidates from current presentation/provider source. audit reports
unmapped candidates; it does not mistake source extraction for rendered coverage.
generate validates canonical data and emits the same embedded/template inventory.
"""
import ast
import hashlib
import json
import pathlib
import re
import sys

ROOT=pathlib.Path(__file__).resolve().parents[2]
DEST=ROOT/'localization'
CONSUMER_OUTPUTS={'runtime':pathlib.Path('OptiScaler/menu/localization'),
                  'desktop':pathlib.Path('apps/NeuRoticHub/shipping-localization/OptiScaler/menu/localization')}
# Desktop-only lifecycle/bootstrap labels; native and App selections remain explicit.
DESKTOP_ONLY_IDS=frozenset('desktop.closing.'+name for name in
    ('anything','artwork','bundle','diagnostics','discovery','installer','preflight','resources','title','update'))|{'desktop.startup.loading'}
def consumer_entries(entries,consumer):
    if consumer not in CONSUMER_OUTPUTS:raise ValueError('Unknown catalog consumer: '+consumer)
    return [e for e in entries if consumer=='desktop' or e['id'] not in DESKTOP_ONLY_IDS]
TOKEN=re.compile(r'//[^\n]*|/\*[\s\S]*?\*/|(?:u8|[LuU])?"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'')
PRINTF=re.compile(r'%(?:[-+#0]*(?:\d+|\*)?(?:\.(?:\d+|\*))?(?:hh|ll|[hljztL]|I64)?[diuoxXfFeEgGaAcsp]|%)')
NAMED=re.compile(r'\{([A-Za-z_][A-Za-z0-9_]*)\}')

def load():return json.loads((DEST/'english.json').read_text(encoding='utf-8'))

def placeholders(text):
    result=[dict(name=name,type='text') for name in dict.fromkeys(NAMED.findall(text))]
    for token in (t for t in PRINTF.findall(text) if t!='%%'):
        for _ in range(token.count('*')):result.append(dict(name=f'arg{1+sum(p["name"].startswith("arg") for p in result)}',type='integer',token=token))
        result.append(dict(name=f'arg{1+sum(p["name"].startswith("arg") for p in result)}',type='integer' if token[-1] in 'diuoxX' else 'number' if token[-1] in 'fFeEgGaA' else 'text',token=token))
    return result

def validate(entries):
    ids=set()
    for e in entries:
        required={'id','surface','section','control','english','context','placeholders','preserve','line_breaks','revision'}
        if not required<=e.keys():raise ValueError(f'Missing fields: {required-e.keys()}')
        if e['id'] in ids:raise ValueError('Duplicate ID: '+e['id'])
        ids.add(e['id'])
        if e['surface'] not in ('desktop','in_game'):raise ValueError('Invalid surface')
        if not e['id'].startswith('desktop.' if e['surface']=='desktop' else 'ingame.'):raise ValueError('ID surface mismatch')
        if not isinstance(e['english'],str) or not e['english'].strip():raise ValueError('Missing English')
        if not isinstance(e['revision'],int) or e['revision']<1:raise ValueError('Invalid semantic revision')
        if [(p['name'],p['type']) for p in e['placeholders']] != [(p['name'],p['type']) for p in placeholders(e['english'])]:raise ValueError('Placeholder metadata differs: '+e['id'])
        for term in e['preserve']:
            if term not in e['english']:raise ValueError('Preserved term missing from English')
        if '\0' in e['english']:raise ValueError('NUL in canonical text')

def paths():
    result=list((ROOT/'OptiScaler/menu').glob('*.cpp'))+list((ROOT/'OptiScaler/menu').glob('*.h'))
    result+=list((ROOT/'OptiScaler/menu').glob('*.inl'))
    result+=list((ROOT/'OptiScaler/dlssnr').glob('*Menu*'))+list((ROOT/'OptiScaler/dlssnr').glob('*Status*.h'))
    result+=list((ROOT/'OptiScaler/dlssnr').glob('NrDiagnostics*.h'))
    result += [p for p in (ROOT/'OptiScaler/dlssnr').glob('*.h') if 'ImGui::' in p.read_text(encoding='utf-8-sig')]
    result+=list((ROOT/'OptiScaler/nr/semantic').rglob('*Menu.h'))
    result+=[ROOT/'OptiScaler/nr/semantic/object_rules/ObjectRuleEditor.h',ROOT/'OptiScaler/dlssnr/NativeFgPresentation.h']
    result+=list((ROOT/'apps/NeuRoticHub').rglob('*.cpp'))+list((ROOT/'apps/NeuRoticHub').rglob('*.h'))
    # These are presentation providers/views despite living next to catalog data.
    visible_shared={'LanguageRuntime.cpp','FontNoticesView.h','LanguageFiles.h'}
    result += [ROOT/'OptiScaler/menu/localization'/name for name in visible_shared]
    return sorted(set(p for p in result if p.exists() and p.suffix in ('.h','.cpp','.inl') and ('/localization/' not in p.as_posix() or p.name in visible_shared) and p.name not in ('Localization.cpp','Localization.h','HubMain.cpp')))

def candidates():
    from presentation_arguments import mask,arguments
    found={}
    for path in paths():
        source=path.read_text(encoding='utf-8-sig');surface='desktop' if 'apps/NeuRoticHub/' in path.as_posix() else 'in_game'
        relative=path.relative_to(ROOT).as_posix();machine_arguments=[];binding_surfaces=[];code=mask(source)
        # Shared providers and reused captions can have a catalog surface
        # different from their file location. The explicit ID is authoritative.
        for call in re.finditer(r'\b(?:Neurotic::)?Ui(?:Literal|Options|Message)\s*\(',code):
            args,_=arguments(code,code.index('(',call.start()))
            if len(args)<2:continue
            declared=set()
            for token in TOKEN.finditer(source,args[0][0],args[0][1]):
                if not token[0].startswith('"'):continue
                for id in ast.literal_eval(token[0]).split('|'):
                    if id.startswith('desktop.'):declared.add('desktop')
                    elif id.startswith('ingame.'):declared.add('in_game')
            if declared:binding_surfaces.append((*args[1],declared))
        # These reviewed views mix captions and protocol/persistence values on
        # one source line. Exempt only the technical argument, never the line
        # or a spelling that may also occur as a real caption elsewhere.
        if relative in ('apps/NeuRoticHub/anything/AnythingView.cpp','apps/NeuRoticHub/ui/InstallDefaultsView.h'):
            for call in re.finditer(r'\.(?:value|contains|at|find)\s*\(|\b(?:capability|SetInstallDefault)\s*\(',code):
                args,_=arguments(code,code.index('(',call.start()))
                if args:machine_arguments.append(args[0])
        tokens=list(TOKEN.finditer(source));groups=[];skip=0
        for i,match in enumerate(tokens):
            if match.start()<skip:continue
            if not match.group().startswith('"'):groups.append((match,match.group()));continue
            end=match.end();group=[match.group()]
            for other in tokens[i+1:]:
                if other.group().startswith('"') and not source[end:other.start()].strip():group.append(other.group());end=other.end()
                else:break
            skip=end;groups.append((match,''.join(group)))
        for match,raw in groups:
            if raw.startswith(('//','/*',"'")):continue
            try:value=''.join(ast.literal_eval(m.group()) for m in TOKEN.finditer(raw))
            except (SyntaxError,ValueError):continue
            line=source.count('\n',0,match.start())+1
            context=source.splitlines()[line-1]
            prefix=source[max(0,match.start()-80):match.start()]
            if any(a<=match.start()<b for a,b in machine_arguments):continue
            if (relative=='apps/NeuRoticHub/anything/AnythingView.cpp'
                and value=='fullscreen-pointer-source-view' and re.search(r'==\s*$',prefix)):continue
            # Schema/control discriminants remain canonical even on a line
            # that also draws a label. They are never presentation arguments.
            if re.search(r'\b(?:entry\.control|status)\s*==\s*$',source[max(0,match.start()-80):match.start()]):continue
            if re.search(r'\b(?:LOG_[A-Z]+|LOG|spdlog::\w+)\s*\(',context):continue
            for text in value.split('\0'):
                text=text.split('##')[0].strip()
                if not text or not re.search(r'[A-Za-z]{2}',text) or len(text)>32768:continue
                if re.fullmatch(r'(?:desktop|ingame)\.[A-Za-z0-9_.-]+',text):continue
                if text == 'NeuRotic Bold':continue # Internal font debug identity, never product prose.
                if text.startswith(('http://','https://','C:/','C:\\')):continue
                # Prose and explicit presentation labels. Machine field names,
                # raw configuration/API identifiers and filenames are exempt.
                ui=bool(re.search(r'ImGui::(?:Text|SetTooltip|Button|Checkbox|Selectable|BeginCombo|Combo|CollapsingHeader|BeginTabItem|SeparatorText|InputText|Slider|Drag|RadioButton)|ui::(?:Button|ActionButton|IconButton|Toggle)|(?:Description|ContentCard|ComponentHeading|WindowSectionHeader|AdvisorSignal|AnythingCard|FieldLabel|Unavailable|Muted|Notice|OperationAction|PopulateCombo|ShowResetButton|AddResourceBarrier|AddDLSSRenderPreset|AddDLSSDRenderPreset|Keybind)\s*\(',context))
                if re.search(r'(?:ContentCard|const char\* (?:inputs|outputs)\[\])',context):ui=True
                prose=bool(re.search(r'[A-Za-z]{2}[ :â€”Â·]+[A-Za-z]{2}',text))
                if not prose and not ui:continue
                if re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]*(?:\.(?:dll|exe|ini|json|png|zip))?',text) and not ui:continue
                declared=set().union(*(s for a,b,s in binding_surfaces if a<=match.start()<b))
                for target in declared or {surface}:
                    found.setdefault((target,text),[]).append(dict(path=relative,line=line,kind='presentation' if ui else 'provider'))
    # Shared backend messages and setting descriptions are surfaced by the App,
    # whereas the CMD path continues using their canonical English.
    for path in (ROOT/'tools/packaging/customer/support').glob('NeuRotic-*.ps1'):
        if path.name in ('NeuRotic-HubHistory.ps1','NeuRotic-HubSanitize.ps1','NeuRotic-HubDumbfire.ps1'):continue
        for n,line in enumerate(path.read_text(encoding='utf-8-sig').splitlines(),1):
            if not re.search(r'New-HubSetting|reason\s*=|throw\s|message\s*=|Write-.*reason',line,re.I):continue
            for match in re.finditer(r"'((?:[^']|'')*)'",line):
                text=match[1].replace("''", "'")
                if re.search(r'[A-Za-z]{2}\s+[A-Za-z]{2}',text):found.setdefault(('desktop',text),[]).append(dict(path=path.relative_to(ROOT).as_posix(),line=n,kind='backend'))
    return found

def seed():
    entries=load() if (DEST/'english.json').exists() else [];by_text={(e['surface'],e['english']):e for e in entries}
    for (surface,text),sites in candidates().items():
        if (surface,text) in by_text:by_text[surface,text]['sources']=sites;continue
        section=pathlib.PurePosixPath(sites[0]['path']).stem.replace('_','-').lower()
        slug=re.sub(r'[^a-z0-9]+','_',text.lower()).strip('_')[:48]
        digest=hashlib.sha256((surface+'\0'+text).encode()).hexdigest()[:8]
        entry=dict(id=('desktop.' if surface=='desktop' else 'ingame.')+section+'.'+slug+'_'+digest,surface=surface,section=section,control='paragraph' if len(text)>70 else 'label',english=text,context='Current UI: '+sites[0]['path'],placeholders=placeholders(text),preserve=[t for t in ('NeuRotic','DLSS','NR Anything','OptiScaler.ini','dxgi.dll','ReShade') if t in text],line_breaks=bool('\n' in text or len(text)>100),revision=1,sources=sites)
        entries.append(entry);by_text[surface,text]=entry
    DEST.mkdir(parents=True,exist_ok=True);validate(entries)
    (DEST/'english.json').write_text(json.dumps(sorted(entries,key=lambda e:e['id']),ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    generate()

def generate():
    entries=load();validate(entries)
    for consumer,relative in CONSUMER_OUTPUTS.items():
        selected=consumer_entries(entries,consumer)
        data=json.dumps(selected,ensure_ascii=True,separators=(',',':'))
        rows=['// Generated by tools/localization/inventory.py generate; '+consumer+'.','static const char* const EmbeddedEnglishChunks[] = {']
        rows += ['R"NR_ENGLISH('+data[pos:pos+12000]+')NR_ENGLISH",' for pos in range(0,len(data),12000)]
        rows += ['};\n']
        (ROOT/relative/'EnglishInventory.inc').write_text('\n'.join(rows),encoding='utf-8',newline='\n')
        print(f'PASS {consumer} canonical inventory: {len(selected)} entries')

def bound_entries():
    """Read exact stable IDs from presentation bindings, never invent IDs by text."""
    from presentation_arguments import mask,arguments
    result={}
    for path in paths():
        source=path.read_text(encoding='utf-8-sig');code=mask(source)
        for match in re.finditer(r'\b(?:Neurotic::)?Ui(Literal|Options|Message)\s*\(',code):
            args,_=arguments(code,code.index('(',match.start()))
            if len(args)<2:continue
            ids=[ast.literal_eval(m[0]) for m in TOKEN.finditer(source,args[0][0],args[0][1]) if m[0].startswith('"')]
            literals=[ast.literal_eval(m[0]) for m in TOKEN.finditer(source,args[1][0],args[1][1]) if m[0].startswith('"')]
            if not ids or not literals:continue
            text=''.join(literals)
            pairs=zip(ids[0].split('|'),text.split('\0')) if match[1]=='Options' else ((id,text) for id in ids)
            for id,english in pairs:
                english=english.split('##')[0].strip()
                if not id or not english:continue
                if not id.startswith(('desktop.','ingame.')):raise ValueError('Invalid bound UI ID: '+id)
                entry=result.setdefault(id,dict(english=english,sources=[]))
                if entry['english']!=english:raise ValueError(f'Conflicting English for {id}: {entry["english"]!r} / {english!r}')
                entry['sources'].append(dict(path=path.relative_to(ROOT).as_posix(),line=source.count('\n',0,match.start())+1,kind='presentation-binding'))
    return result

def merge_bound_entries(entries,bindings):
    # Preserve dormant IDs, caller metadata and custom history. Only explicit
    # source bindings can add/change English; semantic changes advance revision.
    merged={entry['id']:dict(entry) for entry in entries}
    for id,binding in bindings.items():
        english=binding['english'];surface='desktop' if id.startswith('desktop.') else 'in_game'
        entry=merged.get(id)
        if entry is None:
            entry=dict(id=id,surface=surface,section=id.split('.')[1],control='paragraph' if len(english)>70 else 'label',context='Current UI presentation binding',revision=1)
        elif entry['english']!=english:entry['revision']+=1
        else:
            entry['sources']=binding['sources'];merged[id]=entry;continue
        preserved=[term for term in entry.get('preserve',[]) if term in english]
        preserved+= [term for term in ('NeuRotic','DLSS','NR Anything','OptiScaler.ini','dxgi.dll','ReShade') if term in english and term not in preserved]
        entry.update(english=english,placeholders=placeholders(english),preserve=preserved,line_breaks=bool(entry.get('line_breaks',False) or '\n' in english or len(english)>100),sources=binding['sources'])
        merged[id]=entry
    return sorted(merged.values(),key=lambda entry:entry['id'])

def sync_bindings():
    entries=merge_bound_entries(load(),bound_entries());validate(entries)
    (DEST/'english.json').write_text(json.dumps(entries,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    generate()

def audit():
    entries=load();validate(entries);known={(e['surface'],e['english']) for e in entries};missing={k:v for k,v in candidates().items() if k not in known}
    for (surface,text),sites in missing.items():print('UNMAPPED',surface,sites[0]['path'],sites[0]['line'],text)
    if missing:raise ValueError(f'{len(missing)} newly visible candidates bypass the catalog; review and assign an ID or a documented exemption.')
    print('PASS source candidates mapped; rendered/state review remains required')

if __name__=='__main__':{'seed':seed,'generate':generate,'audit':audit,'sync-bindings':sync_bindings}[sys.argv[1]]()
