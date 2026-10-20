"""Read-only presentation audit with balanced calls and complete C++ literals.

Emits actual bindings separately from candidate inventory and technical literals.
Raw runtime strings (user titles, filenames, API names) are not translated by
spelling; providers require stable IDs or complete typed messages.
"""
import ast,json,re,sys
from pathlib import Path
import inventory
from presentation_arguments import mask,arguments

def configuration_array(source,code,relative,name):
    """Only the two reviewed arrays with separate labels and one setter use."""
    if relative!='apps/NeuRoticHub/ui/InstallDefaultsView.h':return False
    pair={'presetValues':('presetLabels','modelPreset'),'mfgValues':('mfgLabels','mfgUnlock')}.get(name)
    if pair is None or len(re.findall(r'\b'+re.escape(name)+r'\b',code))!=2:return False
    labels,key=pair;label_use=False;setter_use=False
    for call in re.finditer(r'\b(?:choose|SetInstallDefault)\s*\(',code):
        args,_=arguments(code,code.index('(',call.start()))
        values=[source[a:b].strip() for a,b in args]
        if call[0].startswith('choose') and len(values)>2:
            label_use|=values[1]=='"'+key+'"' and values[2]==labels
        elif len(values)>1:
            setter_use|=values[0]=='"'+key+'"' and bool(re.fullmatch(re.escape(name)+r'\s*\[[^\]]+\]',values[1]))
    return label_use and setter_use

def audit(output=None):
    result=[];missing=[];bindings=[];arrays=[];technical_arrays=[]
    calls=re.compile(r'(?:ImGui::(?:Text\w*|SetTooltip|TableSetupColumn|SmallButton|MenuItem|Button|Checkbox|Selectable|BeginCombo|Combo|CollapsingHeader|BeginTabItem|SeparatorText|InputText\w*|Slider\w*|Drag\w*|RadioButton)|DlssNr::StatusPanel::Linef|ui::(?:Button|ActionButton|IconButton|Toggle)|(?:ContentCard|BeginNrCard|ControlLabel|HelpMarker|ComponentHeading|WindowSectionHeader|AdvisorSignal|AnythingCard|FieldLabel|Unavailable|Muted|Notice|OperationAction|PopulateCombo|ShowResetButton|Description))\s*\(')
    for path in inventory.paths():
        source=path.read_text(encoding='utf-8-sig');code=mask(source);bound=[]
        for match in re.finditer(r'\b(?:Neurotic::)?Ui(?:Literal|Options|Message)\s*\(',code):
            args,end=arguments(code,code.index('(',match.start()));bound.append((match.start(),end))
            if len(args)>=2:
                # Conditional surface IDs are both real bindings. Keep schema
                # identifiers and literal captions separate from this argument.
                for literal in inventory.TOKEN.finditer(source,args[0][0],args[0][1]):
                    if not literal[0].startswith('"'):continue
                    id=ast.literal_eval(literal[0])
                    bindings.append(dict(path=path.relative_to(inventory.ROOT).as_posix(),line=source.count('\n',0,match.start())+1,id=id))
        machine_arguments=[]
        for match in re.finditer(r'\.(?:value|contains|at|find)\s*\(',code):
            args,_=arguments(code,code.index('(',match.start()))
            if args:machine_arguments.append(args[0])
        for match in calls.finditer(code):
            args,end=arguments(code,code.index('(',match.start()))
            call=match[0].split('(')[0].strip().split('::')[-1]
            label_only=call in {'Selectable','Button','SmallButton','Checkbox','BeginCombo','CollapsingHeader','BeginTabItem','TableSetupColumn','RadioButton','InputText','SliderFloat','SliderInt','DragFloat','DragInt'}
            for literal in inventory.TOKEN.finditer(source,match.start(),end):
                if not literal[0].startswith('"') or any(a<=literal.start()<b for a,b in bound):continue
                if label_only and args and not(args[0][0]<=literal.start()<args[0][1]):continue
                if any(a<=literal.start()<b for a,b in machine_arguments):continue
                try:text=ast.literal_eval(literal[0])
                except (SyntaxError,ValueError):continue
                if text.startswith('X##'):reason='close/clear icon with hidden identity; accessible tooltip is catalogued'
                elif text==' w/Dx12':reason='API/compatibility marker; technical identifier'
                elif text.startswith(('https://','http://')):reason='URL; technical destination'
                elif not text or text.startswith('##'):reason='hidden control identity'
                elif not re.search('[A-Za-z]{2}',text):reason='numeric value, unit or format-only presentation'
                elif text in ('%s','%ls','%.2f','%.1f','%d','%u','%zu'):reason='argument-only presentation; inspect provider separately'
                elif re.fullmatch(r'[A-Za-z0-9_./\\*:%+\-]+\.(?:dll|exe|ini|json|nrlang|png|zip)',text):reason='filename or file pattern'
                else:reason='REVIEW visible literal'
                row=dict(path=path.relative_to(inventory.ROOT).as_posix(),line=source.count('\n',0,literal.start())+1,text=text,reason=reason)
                result.append(row)
                if reason.startswith('REVIEW'):missing.append(row)
        # This visible provider returns a copied caption, so a pointer binding
        # is insufficient. OS key names remain external data; Unbound is ours.
        for match in re.finditer(r'return\s+"Unbound"\s*;',source):
            row=dict(path=path.relative_to(inventory.ROOT).as_posix(),line=source.count('\n',0,match.start())+1,text='Unbound',reason='REVIEW copied key-binding provider caption')
            result.append(row);missing.append(row)
        for match in re.finditer(r'\bconst\s+char\s*\*\s*(?:const\s+)?(\w+)\s*\[[^\]]*\]\s*=\s*\{',code):
            if re.search(r'constexpr\s*$',code[max(0,match.start()-30):match.start()]):continue
            _,end=arguments(code,code.index('{',match.start()))
            for literal in inventory.TOKEN.finditer(source,match.start(),end):
                if not literal[0].startswith('"') or any(a<=literal.start()<b for a,b in bound):continue
                value=ast.literal_eval(literal[0])
                if not value or value.startswith(('##','https://','http://')) or not re.search('[A-Za-z]{2}',value) or '\0' in value:continue
                relative=path.relative_to(inventory.ROOT).as_posix()
                technical=configuration_array(source,code,relative,match[1])
                row=dict(path=relative,line=source.count('\n',0,literal.start())+1,text=value,reason='stored configuration value; separately bound option labels' if technical else 'REVIEW unbound option-array value')
                (technical_arrays if technical else arrays).append(row)
    out=Path(output) if output else inventory.ROOT/'docs/implementation-evidence/community-languages'
    out.mkdir(parents=True,exist_ok=True)
    (out/'SOURCE_BINDINGS.json').write_text(json.dumps(bindings,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    (out/'UNBOUND_PROSE_REVIEW.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    (out/'ARRAY_REVIEW.json').write_text(json.dumps(arrays,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    (out/'TECHNICAL_ARRAY_VALUES.json').write_text(json.dumps(technical_arrays,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    missing+=arrays
    known={e['id'] for e in inventory.load()}
    invalid=[r for r in bindings if any(id and id not in known for id in r['id'].split('|'))]
    if invalid:raise ValueError('Bindings absent from canonical inventory: '+json.dumps(invalid))
    print(json.dumps(missing,ensure_ascii=False,indent=2))
    print(f'{len(bindings)} actual binding calls; {len(missing)} visible-literal review candidates (provider audit separate)')
    return missing
if __name__=='__main__':raise SystemExit(bool(audit(sys.argv[1] if len(sys.argv)>1 else None)))
