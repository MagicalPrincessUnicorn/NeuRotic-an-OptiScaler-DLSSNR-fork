from pathlib import Path
import hashlib,json,subprocess,sys
root=Path(sys.argv[1]).resolve();out=Path(sys.argv[2]).resolve();out.mkdir(parents=True,exist_ok=True)
base='01def38be85c69c8106b95a5ea05eecd1125594c';rel='OptiScaler/hooks/Streamline_Hooks.cpp'
green=len(sys.argv)>3 and sys.argv[3]=='green'
raw=(root/rel).read_bytes() if green else subprocess.check_output(['git','--no-optional-locks','show',base+':'+rel],cwd=root);text=raw.decode('utf-8').replace('\r\n','\n')
code=[]
if green:
    for domain in ['Dlssg','LocalDlssg']:
        declaration='struct '+domain+'OnLoadDomain {};';assert declaration in text;code.append(declaration)
    start=text.index('template<class Bindings>\nvoid* NrBindFeatureOnLoad(')
    code.append(text[start:text.index('\n}',start)+2])
for title,name in [('Game','dlssg'),('Local','local_dlssg')]:
    start=text.index('void* StreamlineHooks::hk'+name+'_slGetPluginFunction(')
    branch=text.index('    if (strcmp(functionName, "slOnPluginLoad") == 0',start)
    opening=text.index('{',branch);end=opening+1;depth=1
    while depth:
        if text[end]=='{':depth+=1
        if text[end]=='}':depth-=1
        end+=1
    body=text[branch:end]
    load=text.index('bool StreamlineHooks::hk'+name+'_slOnPluginLoad(');load_body=text[load:text.index('\n}',load)]
    call='    auto result = '+('invocation.original' if green else 'o_'+name+'_slOnPluginLoad')+'(params, loaderJSON, pluginJSON);';assert call in load_body
    prepare='    prepareNativeMfgCapabilities();\n' if title=='Game' else ''
    if prepare:assert prepare.strip() in load_body and load_body.index(prepare.strip())<load_body.index(call.strip())
    if title=='Game':assert 'bindNativeMfgWrapper(reinterpret_cast<const void*>('+('original' if green else 'o_dlssg_slOnPluginLoad')+'));' in body
    else:assert 'State::Instance().activeFgNvngx != FGNvngxReplacement::None' in body
    if green:
        guard='    if(!result||!pluginJSON||!*pluginJSON)return result;';assert guard in load_body and '    auto* params = invocation.parameters;' in load_body
        transform='const Invocation& invocation,const char* loaderJSON,const char** pluginJSON'
        original_field='';prefix='        RecordInvocation(invocation);\n    auto* params = invocation.parameters;\n'
        returned=guard+'\n        return Neurotic::Hooks::StreamlineOwnedJson::PublishSuccessfulForProcess(result,pluginJSON,*pluginJSON);'
        body=body.replace('sl::param::IParameters','void')
    else:
        transform='void* params,const char* loaderJSON,const char** pluginJSON';original_field='    static inline FixtureLoad o_'+name+'_slOnPluginLoad=nullptr;\n';prefix='';returned='        return result;'
    call_observation='    projectedOriginals.fetch_add(1);\n' if green else ''
    code.append('struct '+title+'Projection\n{\n    using PFN_slOnPluginLoad=FixtureLoad;\n    static inline FixtureGetter o_'+name+'_slGetPluginFunction=nullptr;\n'+original_field+'    static void Select(FixtureGetter value){o_'+name+'_slGetPluginFunction=value;}\n    static bool hk'+name+'_slOnPluginLoad('+transform+')\n    {\n'+prefix+prepare+call_observation+call+'\n'+returned+'\n    }\n    static void* Get(const char* functionName)\n    {\n'+body+'\n        return o_'+name+'_slGetPluginFunction(functionName);\n    }\n};\n')
p=out/'dlssg_onload_origins.inc';p.write_text('\n'.join(code),encoding='utf-8')
print(json.dumps(dict(base=None if green else base,path=rel,source_sha256=hashlib.sha256(raw).hexdigest(),generated_sha256=hashlib.sha256(p.read_bytes()).hexdigest(),adaptation='Actual game/local DLSSG getter branches and original operands, actual game prepare-before-original statement/order and local activation predicate copied. GREEN also complete binding helper/domains/params alias/false-null guard with actual binding/owned-text headers; transform policy becomes process-owned raw text plus context observation. Native MFG bind/prepare recording stubs only: NOT owned transaction/caps/rendering qualification. Actual M1A/B held/reused; no private/game/GPU/physical hook.')))
