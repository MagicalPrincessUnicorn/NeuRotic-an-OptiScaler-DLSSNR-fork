"""Local producer adapters, prepared for NR-WO-0041's exclusive build owner.

Imports and parser calls are inert. Running a tool is an explicit CLI operation.
These records are producer edges, not whole-target build receipts. Observed
tool/task-module closure and native tracking must be joined by BuildEvidence.
No file-presence reuse, generated-directory exemption, or security signature.
"""
from pathlib import Path, PureWindowsPath
import argparse, hashlib, json, os, re, subprocess, sys, time, uuid

def require(ok, message):
    if not ok: raise ValueError(message)

def key(p):return str(Path(p).resolve()).casefold()

def row(p,kind='source'):
    p=Path(p);data=p.read_bytes()
    return dict(path=str(p.absolute()),bytes=len(data),sha256=hashlib.sha256(data).hexdigest(),kind=kind)

def unchanged(r,kind):
    p=Path(r['path'])
    require(p.is_absolute(),'Absolute '+kind+' path required')
    require(p.is_file() and p.stat().st_size==r['bytes'] and row(p)['sha256']==r['sha256'],
            'Changed '+kind+': '+str(p))

def depfile_paths(text, expected, cwd):
    """FFX_SC's actual GCC writer emits absolute paths, without space escaping.

    Reject its ambiguous space-containing form instead of guessing a closure.
    Drive colons and standard backslash-newline continuation are preserved.
    """
    text=re.sub(r'\\\r?\n',' ',text).strip()
    split=re.search(r':(?=\s|$)',text)
    require(split is not None,'Malformed SDK depfile')
    norm=lambda p:str(PureWindowsPath(p)).casefold()
    require(norm(text[:split.start()].strip())==norm(expected),'Depfile target differs')
    result=text[split.end():].split()
    require(result,'Empty SDK dependency closure')
    for p in result:
        require(PureWindowsPath(p).is_absolute() or Path(p).is_absolute(),
                'Ambiguous SDK depfile dependency: '+p)
    return list(dict.fromkeys(result))

def direct_shader_inputs(source):
    """Only the four current self-contained retained worker shaders use this.

    Any directive capable of external reads requires a dependency adapter.
    Reject splicing/digraphs too; never silently expand this bounded mode.
    """
    p=Path(source);text=p.read_text(encoding='utf-8-sig')
    text=re.sub(r'/\*.*?\*/',' ',text,flags=re.S)
    require(not re.search(r'\\\r?\n|%:|\?\?=|^\s*#\s*(include|import|embed)',text,re.M),
            'Shader external dependency requires observed include capture: '+str(p))
    return [str(p)]

def permutation_outputs(primary, shader_name):
    p=Path(primary);text=p.read_text(encoding='utf-8')
    names=re.findall(r'^\s*#\s*include\s+"([^"\r\n]+)"',text,re.M)
    require(names,'Permutation header has no named children')
    result=[str(p)]
    for name in names:
        require(re.fullmatch(re.escape(shader_name)+r'_[0-9a-fA-F]{32}\.h',name),
                'Unmodeled permutation child: '+name)
        child=p.parent/name;require(child.is_file(),'Missing permutation child: '+str(child))
        require(not re.search(r'^\s*#\s*(include|import)',child.read_text(encoding='utf-8'),re.M),
                'Nested permutation child dependency requires capture')
        result.append(str(child))
    require(len(set(result))==len(result),'Duplicate permutation child')
    return result

def _frozen_inputs(pins):
    known={key(r['path']):r for r in pins}
    require(len(known)==len(pins),'Duplicate freeze input')
    return known

def validate_action(plan, record, pins, produced=()):
    return _validate_action(plan,record,_frozen_inputs(pins),produced)

def _validate_action(plan, record, known, produced=()):
    require(record.get('scope') in ('FIXTURE_POLICY_ONLY','LOCAL_PRODUCER_CAPTURE_ONLY'),
            'Producer capture scope required')
    for field,diagnostic in [('id','Action differs'),('configuration','Configuration differs'),
          ('cwd','Working directory differs'),('command','Command differs'),('environment','Environment differs')]:
        require(record.get(field)==plan.get(field),diagnostic)
    require(record.get('exit_code')==0,'Producer command failed')
    made={key(r['path']):r for r in produced}
    require(plan.get('inputs') and plan.get('tools') and plan.get('outputs'),'Empty producer offerings')
    for group,kind in [('inputs','source'),('tools','tool')]:
        actual={key(r['path']):r for r in record.get(group,[])}
        required={key(p) for p in plan[group]}
        require(required.issubset(actual),'Declared '+kind+' observation missing')
        if plan['kind'] not in ('ffx-depfile','msvc-showincludes'):require(required==set(actual),'Unexpected '+kind+' observation')
        for k,r in actual.items():
            require(k in known or (group=='inputs' and k in made),'Unfrozen '+kind+': '+r['path'])
            baseline=known.get(k) or made[k]
            require(key(baseline['path'])==k,'Frozen '+kind+' input path changed: '+r['path'])
            unchanged(r,kind);unchanged(baseline,kind)
            require(r['bytes']==baseline['bytes'] and r['sha256']==baseline['sha256'],
                    'Identity differs for '+kind+': '+r['path'])
    outputs=record.get('outputs',[]);seen={key(r['path']) for r in outputs}
    require(len(seen)==len(outputs),'Duplicate produced output')
    expected={key(p) for p in plan['outputs']}
    if plan['kind']=='ffx-depfile':
        primary=plan['outputs'][0]
        children=permutation_outputs(primary,plan['shader_name'])
        expected|={key(p) for p in children}
    require(seen==expected,'Named output set differs')
    for r in outputs:unchanged(r,'output')
    return outputs

def identity_bytes(binary):
    digest=hashlib.sha256(Path(binary).read_bytes()).hexdigest()
    return ('#pragma once\ninline constexpr char WindowNrForwarderSha256[] = "'+digest+'";\n').encode('ascii')

def _rooted_link_command_parts(p,roots,values):
    """LINK may put later quoted root inputs on separate command-log lines.

    Preserve the opaque first part and all continuation parts. This does not
    reconstruct the executed argv or an unretained compiler response file.
    """
    if not p.name.casefold().startswith('link.command.') or len(roots)<2 or len(values)!=len(roots):return False
    expected={key(q) for q in roots};seen=set()
    if len(expected)!=len(roots):return False
    for line in values[1:]:
        match=re.fullmatch(r'"([^"\x00|]+)"',line)
        if not match:return False
        q=Path(match[1])
        if not q.is_absolute() or key(q) not in expected or key(q) in seen:return False
        seen.add(key(q))
    remaining=expected-seen
    first={key(Path(s)) for s in re.findall(r'"([^"\x00|]+)"',values[0]) if Path(s).is_absolute()}
    return len(remaining)==1 and remaining<=first

def tlog_records(p, *, command=False):
    # A ^ header is a source/rooting-marker key, never a write value.
    # Root paths remain authentic read provenance; write keys must bind to
    # matching read records before only their payloads are ownership-checked.
    result={}
    for line in p.read_text(encoding='utf-16').splitlines():
        if not line.strip():continue
        if line.startswith('^'):
            tokens=line[1:].split('|')
            require(all(token and token==token.strip() for token in tokens),'Malformed tracking record root')
            roots=[Path(token) for token in tokens]
            require(all(q.is_absolute() for q in roots),'Unmodeled tracking record root: '+line)
            marker=tuple(key(q) for q in roots)
            require(marker not in result,'Duplicate tracking record root: '+line)
            result[marker]=(roots,[])
        else:
            require(result,'Tracking payload precedes record root: '+str(p))
            if command:result[marker][1].append(line) # Opaque command, not paths split on |.
            else:
                q=Path(line.strip())
                require('|' not in line and q.is_absolute(),'Unmodeled tracking path: '+line)
                result[marker][1].append(q)
    require(result and all(values for roots,values in result.values()),'Empty native tracking record: '+str(p))
    if command:require(all(len(values)==1 or _rooted_link_command_parts(p,roots,values) for roots,values in result.values()),'Command tracking record needs one payload or exact rooted LINK continuations: '+str(p))
    return result

def check_native_write(repo,output,p):
    # Validate the effective destination before filters, retaining p's raw
    # trace spelling for producer rows and the existing ancestry policy.
    destination=Path(os.path.abspath(p))
    require(destination.is_relative_to(Path(os.path.abspath(output))),'Unowned native write: '+str(p))
    owned(repo,p)

def native_parent(repo,output,pins,root,selected):
    return _native_parent(repo,output,pins,root,selected,{key(r['path']):r for r in pins})

def _native_parent(repo,output,pins,root,selected,known):
    """Replay invocation-owned child forwarder native tracking before pinning.

    05 must clear only this target's tracking before the child build. No skipped
    producer reuse is accepted. Module/task/import joining remains mandatory.
    """
    output=Path(output);root=Path(root)
    pending=[];traces=[];made={};producers=[]
    for read in sorted(root.rglob('*.read.*.tlog')):
        stem=read.name.split('.read.',1)[0].lower()
        require(stem in ('cl','link','rc','masm'),'Unmodeled native producer: '+stem)
        command=read.with_name(read.name.replace('.read.','.command.'))
        write=read.with_name(read.name.replace('.read.','.write.'))
        require(command.is_file() and write.is_file(),'Missing native command/write tracking')
        pending.append((stem,read,command,write))
    require(any(s=='link' for s,*_ in pending),'Actual forwarder linker tracking missing')
    while pending:
        progress=False
        for family in pending[:]:
            stem,read,command,write=family;inputs=[];waiting=[]
            reads=tlog_records(read);writes=tlog_records(write)
            require(writes.keys()<=reads.keys(),'Write tracking root absent from read records: '+str(write))
            require(reads.keys()<=writes.keys(),'Read tracking root lacks write record: '+str(read))
            commands=tlog_records(command,command=True)
            require(commands.keys()==reads.keys(),'Command tracking roots differ: '+str(command))
            read_paths=[p for roots,values in reads.values() for p in roots+values]
            for p in dict.fromkeys(read_paths):
                k=key(p)
                if k in known:
                    require(key(known[k]['path'])==k,'Frozen native input path changed: '+str(p))
                    unchanged(known[k],'native input');inputs.append(known[k])
                elif k in made:inputs.append(made[k])
                elif p.is_relative_to(output):waiting.append(p)
                else:raise ValueError('Consumed input absent from freeze: '+str(p))
            if waiting:continue
            names={'cl':['cl.exe','c1xx.dll','c2.dll'],'link':['link.exe'],'rc':['rc.exe'],'masm':['ml64.exe']}[stem]
            tools=[]
            for name in names:
                matches=[r for r in pins if Path(r['path']).name.casefold()==name]
                require(len(matches)==1,'Actual native tool missing/ambiguous: '+name)
                unchanged(matches[0],'native tool');tools+=matches
            outputs=[]
            for p in dict.fromkeys(p for roots,values in writes.values() for p in values):
                check_native_write(repo,output,p)
                if p.is_file():outputs.append(row(p,'generated'))
            require(outputs,'Native output evidence missing')
            text=command.read_text(encoding='utf-16');require(text.strip(),'Empty native command evidence')
            producers.append(dict(kind=stem,command=text,inputs=inputs,tools=tools,outputs=outputs,exit_code=0))
            traces += [row(p,'trace') for p in (read,command,write)]
            for r in outputs:made[key(r['path'])]=r
            pending.remove(family);progress=True
        require(progress,'Consumed input absent or unproven generated native edge: '+str(waiting[0] if waiting else root))
    require(key(selected) in made,'Forwarder selected output not actually produced: '+str(selected))
    return dict(producers=producers,selected_outputs=[made[key(selected)]],trace_files=traces)

def cmake_inputs(reply_dir,trace_path,target='neurotic_prepared_flow'):
    """Read one explicitly selected File API reply index + expanded JSON trace.

    Returns paths/compile groups/artifacts; never implies configure/build pass.
    Trace supplements File API with file(READ)/configure_file template reads.
    """
    reply_dir=Path(reply_dir);indexes=list(reply_dir.glob('index-*.json'))
    require(len(indexes)==1,'Select exactly one current CMake reply index')
    index=json.loads(indexes[0].read_bytes());objects=index['objects']
    def read(kind):
        choices=[o for o in objects if o['kind']==kind]
        require(len(choices)==1,'Missing/ambiguous CMake '+kind)
        return json.loads((reply_dir/choices[0]['jsonFile']).read_bytes())
    files=read('cmakeFiles');model=read('codemodel')
    require(len(model['configurations'])==1,'Single selected CMake configuration required')
    config=model['configurations'][0];targets={t['id']:t for t in config['targets']}
    selected=[t for t in targets.values() if t['name']==target]
    require(len(selected)==1,'Selected flow target missing')
    paths={};generated=set();groups={};artifacts=[];visited=set()
    def add(path,kind,base):
        p=Path(path);p=p if p.is_absolute() else Path(base)/p
        paths[key(p)]=dict(path=str(p.absolute()),kind=kind)
    for r in files['inputs']:
        if not r.get('isGenerated'):add(r['path'],'cmake-import',files['paths']['source'])
    def visit(t):
        if t['id'] in visited:return
        visited.add(t['id']);doc=json.loads((reply_dir/t['jsonFile']).read_bytes())
        groups[t['name']]=doc.get('compileGroups',[])
        for r in doc.get('sources',[]):
            if r.get('isGenerated'):generated.add(str((Path(model['paths']['build'])/r['path']).absolute()))
            else:add(r['path'],'source',model['paths']['source'])
        for r in doc.get('artifacts',[]):artifacts.append(str((Path(model['paths']['build'])/r['path']).absolute()))
        for dep in doc.get('dependencies',[]):visit(targets[dep['id']])
    visit(selected[0])
    for line in Path(trace_path).read_text(encoding='utf-8-sig').splitlines():
        r=json.loads(line)
        if 'cmd' not in r:continue
        add(r['file'],'cmake-import',model['paths']['source'])
        args=r.get('args',[])
        if r['cmd'].lower()=='file' and args and args[0].upper()=='READ':
            require(Path(args[1]).is_absolute(),'Expanded file READ must be absolute')
            add(args[1],'generator-input',model['paths']['source'])
        if r['cmd'].lower()=='configure_file':add(args[0],'generator-input',Path(r['file']).parent)
    return dict(inputs=list(paths.values()),generated_inputs=sorted(generated),
                compile_groups=groups,artifacts=sorted(set(artifacts)))

def owned(repo,p,tree=False):
    """Use the existing shared ownership implementation after application."""
    sys.path.insert(0,str(Path(repo)/'tools/packaging/customer'))
    from OfflineDependencies import owned_output
    return owned_output(Path(repo),p,tree=tree)

def execute(session_path,action_id,command):
    """Run a declared local action; join module/native closure separately.

    Session file and manifest must themselves be pinned in the build request.
    External orchestration owns sequencing and supplies already observed parents.
    """
    session_path=Path(session_path);session_row=row(session_path)
    session=json.loads(session_path.read_bytes());repo=Path(session['repository'])
    require(session['scope']=='LOCAL_PRODUCER_CAPTURE_ONLY','Local capture session required')
    freeze=session['source_manifest'];unchanged(freeze,'freeze')
    pins=json.loads(Path(freeze['path']).read_bytes())['files']
    for r in pins:unchanged(r,'freeze input')
    # Share only within this pre-command phase, never across invocations.
    known=_frozen_inputs(pins)
    require(key(__file__) in known and key(sys.executable) in known,
            'Capture adapter and actual Python host must be frozen')
    choices=[a for a in session['actions'] if a['id']==action_id]
    require(len(choices)==1,'Action id missing/ambiguous');plan=choices[0]
    require(re.fullmatch(r'[A-Za-z0-9._-]+',action_id),'Invalid producer action id')
    require(command==plan['command'],'Command differs from frozen offering')
    require({'PATH','INCLUDE','LIB','LIBPATH','CL','_CL_','LINK','_LINK_',
             'VCToolsVersion','WindowsSDKVersion','VSLANG','TEMP','TMP'}.issubset(plan['environment']),
            'Effective tool environment offering incomplete')
    relevant_env={name:os.environ.get(name) for name in plan['environment']}
    require(relevant_env==plan['environment'],'Environment differs from frozen offering')
    produced=[]
    for parent_path in plan.get('parent_records',[]):
        parent=json.loads(Path(parent_path).read_bytes())
        parent_plan=next(a for a in session['actions'] if a['id']==parent['id'])
        produced+=_validate_action(parent_plan,parent,known,produced)
    native=None
    if plan['kind']=='forwarder-identity':
        native=_native_parent(repo,session['output_root'],pins,session['forwarder_tracking_root'],plan['inputs'][0],known)
        produced+=native['selected_outputs']
    parents={key(r['path']):r for r in produced}
    for kind,paths in [('tool',plan['tools']),('source',plan['inputs'])]:
        for p in paths:
            require(key(p) in known or (kind=='source' and key(p) in parents),'Unfrozen '+kind+': '+p)
            baseline=known.get(key(p)) or parents[key(p)]
            require(key(baseline['path'])==key(p),'Frozen '+kind+' input path changed: '+p)
            unchanged(baseline,kind)
    for p in plan['outputs']:owned(repo,p)
    output_tree=owned(repo,session['output_root'],tree=True)
    require(Path(plan['cwd']).resolve().is_relative_to(repo.resolve()) or plan['kind']=='ffx-depfile',
            'Unexpected action working directory')
    capture=owned(repo,session['capture_root'],tree=True)
    capture.mkdir(parents=True,exist_ok=True)
    log=capture/(action_id+'.log');result=capture/(action_id+'.json')
    for p in (log,result):owned(repo,p);require(not p.exists(),'No producer receipt overwrite/reuse: '+str(p))
    if plan['kind']=='dxc-direct':direct_shader_inputs(plan['inputs'][0])
    for p in plan['outputs']:Path(p).parent.mkdir(parents=True,exist_ok=True)
    before=[row(p,'tool' if key(p) in {key(q) for q in plan['tools']} else 'source') for p in plan['tools']+plan['inputs']]
    start=time.time_ns();invocation=str(uuid.uuid4())
    run=subprocess.run(command,cwd=plan['cwd'],stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    owned(repo,log);log.write_bytes(run.stdout)
    require(run.returncode==0,'Producer command failed: '+str(log))
    inputs=list(plan['inputs']);outputs=list(plan['outputs'])
    if plan['kind']=='ffx-depfile':
        primary,depfile=plan['outputs']
        inputs+=depfile_paths(Path(depfile).read_text(encoding='utf-8'),primary,plan['cwd'])
        outputs+=permutation_outputs(primary,plan['shader_name'])[1:]
    if plan['kind']=='msvc-showincludes':
        observed=re.findall(r'(?m)^\s*Note: including file:\s*(.+?)\s*$',run.stdout.decode('utf-8',errors='strict'))
        require(observed,'Actual flow compiler include observation missing')
        inputs+=observed
    if plan['kind']=='forwarder-identity':
        require(Path(outputs[0]).read_bytes()==identity_bytes(inputs[0]),'Forwarder identity content differs')
    for r in before:unchanged(r,'producer input')
    for r in pins:unchanged(r,'freeze input')
    unchanged(freeze,'freeze');unchanged(session_row,'capture session');owned(repo,output_tree,tree=True)
    record=dict(id=action_id,kind=plan['kind'],scope='LOCAL_PRODUCER_CAPTURE_ONLY',configuration=plan['configuration'],
        cwd=plan['cwd'],command=command,environment=relevant_env,exit_code=run.returncode,
        invocation_id=invocation,started_ns=start,ended_ns=time.time_ns(),session=session_row,
        source_manifest=freeze,tools=[row(p,'tool') for p in plan['tools']],
        inputs=[row(p) for p in dict.fromkeys(inputs)],outputs=[row(p,'generated') for p in dict.fromkeys(outputs)],
        trace_files=[row(log,'trace')],module_closure='REQUIRES_05_OBSERVED_MODULE_JOIN')
    if native:record['native_parent']=native
    # Re-resolve the full freeze after execution: aliases may have changed even
    # when bytes match. Retain duplicate detection and fresh observed keys.
    _validate_action(plan,record,_frozen_inputs(pins),produced)
    owned(repo,result);result.write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
    return record

def replay_actions(session_path):
    """Seed native tracking with verified named custom outputs, never a folder.

    Caller still must require 05's observed task/module join and target graph.
    No adaptation of this return value alone can emit OBSERVED_BUILD.
    """
    session_path=Path(session_path);session_row=row(session_path)
    session=json.loads(session_path.read_bytes());freeze=session['source_manifest']
    unchanged(freeze,'freeze');pins=json.loads(Path(freeze['path']).read_bytes())['files']
    produced=[];records=[];traces=[]
    # actions are explicitly in topological order in the frozen session
    for plan in session['actions']:
        path=Path(session['capture_root'])/(plan['id']+'.json')
        require(path.is_file(),'Actual custom producer record absent: '+plan['id'])
        record=json.loads(path.read_bytes())
        require(record['session']==session_row and record['source_manifest']==freeze,'Producer session/freeze differs')
        if plan['kind']=='forwarder-identity':
            native=native_parent(session['repository'],session['output_root'],pins,
                                 session['forwarder_tracking_root'],plan['inputs'][0])
            require(record.get('native_parent')==native,'Forwarder tracking producer differs')
            produced+=native['selected_outputs'];traces+=native['trace_files']
        produced+=validate_action(plan,record,pins,produced)
        for r in record['trace_files']:unchanged(r,'trace')
        records.append(record);traces += record['trace_files']+[row(path,'trace')]
    return dict(scope='LOCAL_PRODUCER_CAPTURE_ONLY',producers=records,generated_outputs=produced,
                trace_files=traces,requires='05_OBSERVED_TASK_MODULE_AND_NATIVE_TARGET_CLOSURE')

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--session',required=True)
    route=parser.add_mutually_exclusive_group(required=True)
    route.add_argument('--action');route.add_argument('--ffx',action='store_true');route.add_argument('--auto',action='store_true')
    parser.add_argument('command',nargs=argparse.REMAINDER);args=parser.parse_args()
    command=args.command[1:] if args.command[:1]==['--'] else args.command
    action=args.action
    if args.ffx:
        names=[s[6:] for s in command if s.startswith('-name=')]
        require(len(names)==1,'Exact FFX shader name required');action='flow-'+names[0]
    if args.auto:
        session=json.loads(Path(args.session).read_bytes())
        matches=[a['id'] for a in session['actions'] if a['command']==command]
        require(len(matches)==1,'Actual CMake command missing/ambiguous in prefreeze offering')
        action=matches[0]
    execute(args.session,action,command)
