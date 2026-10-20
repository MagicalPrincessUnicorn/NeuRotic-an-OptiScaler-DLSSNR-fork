"""Build concrete local action offerings from 05's actual evaluated properties.

This writes one proposed session, never invokes MSBuild/CMake/shader tools.
Freeze its hash separately in the build request (avoids a manifest hash cycle).
"""
from pathlib import Path
import argparse,json,hashlib,os,re,sys
from ProducerCapture import require,row,unchanged,key,owned

WORKER_PROPERTIES=('MSBuildProjectFullPath','MSBuildProjectDirectory','MSBuildThisFileDirectory',
    'OutDir','RetainedSourceRoot','DiagnosticShaderDir','NeuRoticCapturePowerShell',
    'NeuRoticWorkerSourceGraph','Configuration','Platform')
WORKER_ENVIRONMENT=('PATH','INCLUDE','LIB','LIBPATH','CL','_CL_','LINK','_LINK_',
    'VCToolsVersion','WindowsSDKVersion','VSLANG','TEMP','TMP','EXTERNAL_INCLUDE')
WORKER_ENVIRONMENT_SCOPE='EFFECTIVE_WORKER_ENVIRONMENT_OFFERING_ONLY'

def worker_properties(properties):
    p=properties.get('Properties',properties)
    require(all(isinstance(p.get(name),str) and p[name] for name in WORKER_PROPERTIES),
            'Effective worker environment evaluated property missing')
    require(p['Configuration']=='Release' and p['Platform']=='x64' and
            p['NeuRoticWorkerSourceGraph'] in ('Live','Shipping'),
            'Effective worker environment selected graph/configuration differs')
    return {name:p[name] for name in WORKER_PROPERTIES}

def worker_environment(properties,environment):
    """Collect the actual metadata helper process values, never predict SetEnv.

    The owner runs the explicit project target after PrepareForBuild and pins
    its source/invocation/report. This OFFERING_ONLY document is not proof that
    compilation or an observed build occurred. Actual producers still compare
    their effective environment exactly before launching any custom tool.
    """
    p=worker_properties(properties)
    actual={name:environment.get(name) for name in WORKER_ENVIRONMENT}
    require(all(value is None or isinstance(value,str) for value in actual.values()),
            'Effective worker environment value type differs')
    return dict(schema_version=1,scope=WORKER_ENVIRONMENT_SCOPE,stage='POST_PREPARE_FOR_BUILD',
        configuration='Release|x64',properties=p,environment=actual,
        project=row(p['MSBuildProjectFullPath'],'project'),capture_adapter=row(__file__,'capture-adapter'))

def effective_worker_environment(properties,report):
    p=worker_properties(properties)
    require(isinstance(report,dict) and report.get('schema_version')==1 and
            report.get('scope')==WORKER_ENVIRONMENT_SCOPE and
            report.get('stage')=='POST_PREPARE_FOR_BUILD' and
            report.get('configuration')=='Release|x64',
            'Effective worker environment offering required')
    require(report.get('properties')==p,'Effective worker environment evaluated properties differ')
    environment=report.get('environment')
    require(isinstance(environment,dict) and set(environment)==set(WORKER_ENVIRONMENT) and
            all(value is None or isinstance(value,str) for value in environment.values()),
            'Effective worker environment inventory differs')
    for name,path in [('project',p['MSBuildProjectFullPath']),('capture_adapter',__file__)]:
        pin=report.get(name)
        require(isinstance(pin,dict) and pin.get('path') and key(pin['path'])==key(path),
                'Effective worker environment capture source differs')
        unchanged(pin,'effective worker environment '+name)
    return dict(environment)

def worker_actions(properties,tools,environment):
    p=properties.get('Properties',properties)
    environment=effective_worker_environment(properties,environment)
    for name in ['MSBuildProjectDirectory','MSBuildThisFileDirectory','OutDir',
                 'RetainedSourceRoot','DiagnosticShaderDir','NeuRoticCapturePowerShell']:
        require(p.get(name),'Actual evaluated worker property absent: '+name)
    project_dir=p['MSBuildThisFileDirectory']
    dxc=project_dir+'..\\..\\..\\OptiScaler\\shaders\\shader_tools\\dxc.exe'
    source=p['RetainedSourceRoot']+'apps/NeuRoticWindowWorker/guidance/dav2/'
    actions=[]
    for name in ('PrepareInput','MaterializeDepth','EstimateRange','NormalizeRelative'):
        src=source+name+'.hlsl';out=p['DiagnosticShaderDir']+name+'.h'
        actions.append(dict(id='worker-'+name,kind='dxc-direct',configuration='Release|x64',
            cwd=p['MSBuildProjectDirectory'],
            command=[dxc,'-T','cs_6_0','-E','CSMain','-WX','-O3','-Fh',out,'-Vn','Dav2'+name,src],
            inputs=[src],outputs=[out],tools=tools['dxc'],environment=environment))
    binary=p['OutDir']+'nvngx.dll_dlssnr.dll'
    script=project_dir+'Pin-Forwarder.ps1'
    actions.insert(0,dict(id='worker-forwarder-identity',kind='forwarder-identity',configuration='Release|x64',
        cwd=p['MSBuildProjectDirectory'],command=[p['NeuRoticCapturePowerShell'],'-NoProfile','-ExecutionPolicy','Bypass','-File',script,
        '-Binary',binary,'-GeneratedDir',p['DiagnosticShaderDir']],
        inputs=[binary,script],outputs=[p['DiagnosticShaderDir']+'WindowNrForwarderIdentity.h'],
        tools=tools['identity'],environment=environment))
    return actions

def flow_shader_actions(trace,tools,environment):
    actions=[]
    for line in Path(trace).read_text(encoding='utf-8-sig').splitlines():
        event=json.loads(line)
        if event.get('cmd','').lower()!='add_custom_command':continue
        # CMake's unquoted COMMAND ${EXECUTABLE} expands its semicolon list.
        args=[part for arg in event['args'] for part in arg.split(';')]
        if '--ffx' not in args:continue
        require(args[0]=='OUTPUT' and args.count('COMMAND')==1,'Unmodeled SDK custom command')
        start=args.index('COMMAND')+1;end=args.index('WORKING_DIRECTORY')
        launcher=args[start:end];split=launcher.index('--')
        command=launcher[split+1:]
        names=[arg[6:] for arg in command if arg.startswith('-name=')]
        require(len(names)==1,'SDK shader name missing')
        output=args[1];depfile=args[args.index('DEPFILE')+1]
        inputs=args[args.index('DEPENDS')+1:args.index('DEPFILE')]
        actions.append(dict(id='flow-'+names[0],kind='ffx-depfile',shader_name=names[0],configuration='Release|x64',
          cwd=args[end+1],command=command,inputs=inputs,outputs=[output,depfile],tools=tools['ffx'],environment=environment))
    require(len(actions)==28,'Expected actual selected seven-pass/four-variant SDK shader graph')
    require(len({a['id'] for a in actions})==28,'Duplicate SDK shader action')
    return actions

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--kind',choices=['worker','flow-shaders','worker-environment'],required=True)
    for name in ['evaluation','tool-offerings','environment','manifest','repository','output-root','capture-root','result']:
        p.add_argument('--'+name)
    p.add_argument('--property',action='append',default=[])
    args=p.parse_args()
    if args.kind=='worker-environment':
        require(args.repository and args.output_root and args.result,'Effective worker environment owned output required')
        properties={}
        for item in args.property:
            require('=' in item,'Effective worker environment property pair required')
            name,value=item.split('=',1)
            require(name in WORKER_PROPERTIES and name not in properties,'Effective worker environment property duplicate/unknown')
            # Directory values carry one terminal dot so cmd/CRT quoting never
            # turns an actual trailing backslash into an escaped closing quote.
            if name in ('MSBuildThisFileDirectory','OutDir','RetainedSourceRoot','DiagnosticShaderDir'):
                require(value.endswith(('\\.','/.')),'Effective worker environment directory sentinel missing')
                value=value[:-1]
            properties[name]=value
        repo=Path(args.repository).resolve();output=owned(repo,args.output_root,tree=True).resolve()
        result=owned(repo,args.result)
        require(result.resolve().is_relative_to(output) and not result.exists(),
                'Effective worker environment report output differs or exists')
        require(key(properties.get('MSBuildProjectFullPath',''))==key(repo/'apps/NeuRoticWindowWorker/shipping/NeuRoticWindowWorker.vcxproj') and
                Path(properties.get('OutDir','')).resolve().parent==output,
                'Effective worker environment repository/output differs')
        report=worker_environment(properties,os.environ)
        report['helper_invocation']=dict(argv=[sys.executable,*sys.argv],python=row(sys.executable,'tool'),
            limits='Actual helper argv only. Owner must separately retain authentic MSBuild metadata invocation/source/exit and freeze this report. No build qualification.')
        result.parent.mkdir(parents=True,exist_ok=True)
        with result.open('x',encoding='utf-8') as handle:json.dump(report,handle,indent=2);handle.write('\n')
        print(json.dumps({'status':WORKER_ENVIRONMENT_SCOPE,'path':str(result),'sha256':row(result)['sha256']}))
        sys.exit(0)
    require(all(getattr(args,name.replace('-','_')) for name in
        ['evaluation','tool-offerings','environment','manifest','repository','output-root','capture-root','result']),
        'Complete action offering arguments required')
    tools=json.loads(Path(args.tool_offerings).read_bytes());environment=json.loads(Path(args.environment).read_bytes())
    if args.kind=='worker':
        pins={key(r['path']):r for r in json.loads(Path(args.manifest).read_bytes())['files']}
        for path in (args.environment,environment.get('project',{}).get('path'),environment.get('capture_adapter',{}).get('path')):
            require(path and key(path) in pins,'Effective worker environment capture input absent from freeze')
            unchanged(pins[key(path)],'effective worker environment freeze')
    actions=worker_actions(json.loads(Path(args.evaluation).read_bytes()),tools,environment) if args.kind=='worker' else flow_shader_actions(args.evaluation,tools,environment)
    session=dict(schema_version=1,scope='LOCAL_PRODUCER_CAPTURE_ONLY',repository=args.repository,
        source_manifest=row(args.manifest,'freeze'),output_root=args.output_root,capture_root=args.capture_root,
        forwarder_tracking_root=str(Path(args.output_root)/'forwarder-obj'),actions=actions)
    if args.kind=='worker':session['effective_environment_offering']=row(args.environment,'environment-offering')
    result=Path(args.result);require(not result.exists(),'Do not overwrite a frozen session')
    result.write_text(json.dumps(session,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'status':'OFFERING_ONLY_NOT_BUILT','actions':len(actions),'path':str(result),'sha256':row(result)['sha256']}))
