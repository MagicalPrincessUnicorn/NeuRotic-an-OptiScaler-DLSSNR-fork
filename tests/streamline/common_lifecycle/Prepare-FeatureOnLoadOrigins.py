from pathlib import Path
import hashlib,json,subprocess,sys
root=Path(sys.argv[1]).resolve();out=Path(sys.argv[2]).resolve();out.mkdir(parents=True,exist_ok=True)
base='51fcba6c9acdf8b9339f780c061e01893e9b5b7f';rel='OptiScaler/hooks/Streamline_Hooks.cpp'
green=len(sys.argv)>3 and sys.argv[3]=='green'
raw=(root/rel).read_bytes() if green else subprocess.check_output(['git','--no-optional-locks','show',base+':'+rel],cwd=root)
text=raw.decode('utf-8').replace('\r\n','\n')
routes=[('Dlss','dlss'),('Reflex','reflex'),('Pcl','pcl')]
code=[]
if green:
    for domain in ['Dlss','Reflex','Pcl']:
        declaration='struct '+domain+'OnLoadDomain {};'
        assert declaration in text;code.append(declaration)
    start=text.index('template<class Bindings>\nvoid* NrBindFeatureOnLoad(')
    code.append(text[start:text.index('\n}',start)+2])
for title,name in routes:
    marker='void* StreamlineHooks::hk'+name+'_slGetPluginFunction('
    start=text.index(marker);branch=text.index('    if (strcmp(functionName, "slOnPluginLoad") == 0)',start)
    opening=text.index('{',branch);end=opening+1;depth=1
    while depth:
        if text[end]=='{':depth+=1
        if text[end]=='}':depth-=1
        end+=1
    body=text[branch:end]
    load=text.index('bool StreamlineHooks::hk'+name+'_slOnPluginLoad(')
    load_body=text[load:text.index('\n}',load)]
    if green:
        call='    auto result = invocation.original(params, loaderJSON, pluginJSON);'
        guard='    if(!result||!pluginJSON||!*pluginJSON)return result;'
        assert call in load_body and guard in load_body and '    auto* params = invocation.parameters;' in load_body
        assert 'NrBindFeatureOnLoad<Bindings>(original,&hk'+name+'_slOnPluginLoad' in body
        transform='const Invocation& invocation,const char* loaderJSON,const char** pluginJSON'
        original_field=''
        transformation='        RecordInvocation(invocation);\n    auto* params = invocation.parameters;\n'+call+'\n'+guard+'\n        return Neurotic::Hooks::StreamlineOwnedJson::PublishSuccessfulForProcess(result,pluginJSON,*pluginJSON);'
        body=body.replace('sl::param::IParameters','void')
    else:
        call='    auto result = o_'+name+'_slOnPluginLoad(params, loaderJSON, pluginJSON);'
        assert call in load_body
        assert 'o_'+name+'_slOnPluginLoad = (PFN_slOnPluginLoad) o_'+name+'_slGetPluginFunction(functionName);' in body
        transform='void* params,const char* loaderJSON,const char** pluginJSON'
        original_field='    static inline FixtureLoad o_'+name+'_slOnPluginLoad=nullptr;\n'
        transformation=call+'\n        return result;'
    code.append('struct '+title+'Projection\n{\n    using PFN_slOnPluginLoad=FixtureLoad;\n    static inline FixtureGetter o_'+name+'_slGetPluginFunction=nullptr;\n'+original_field+'    static void Select(FixtureGetter getter){o_'+name+'_slGetPluginFunction=getter;}\n    static bool hk'+name+'_slOnPluginLoad('+transform+')\n    {\n'+transformation+'\n    }\n    static void* Get(const char* functionName)\n    {\n'+body+'\n        return o_'+name+'_slGetPluginFunction(functionName);\n    }\n};\n')
p=out/'feature_onload_origins.inc';p.write_text('\n'.join(code),encoding='utf-8')
print(json.dumps(dict(base=None if green else base,path=rel,source_sha256=hashlib.sha256(raw).hexdigest(),generated_sha256=hashlib.sha256(p.read_bytes()).hexdigest(),routes=[x[0] for x in routes],adaptation='Actual three getter OnLoad branches and original-call expressions copied into authored fixture classes. GREEN additionally copies complete actual binding helper and domain declarations, actual params alias/false-null guard, and uses actual binding/owned-text headers; only opaque params become void and caps/JSON/config transformation becomes process-owned raw text with fixture context observation. RED returns original raw result/text with A/B held loaded. No physical hook/provider/game/GPU. DLSSG/local MFG side effects outside this finite cohort.')))
