from pathlib import Path
import hashlib,json,subprocess,sys
root=Path(sys.argv[1]).resolve();out=Path(sys.argv[2]).resolve();out.mkdir(parents=True,exist_ok=True)
base='3a28c1954fe8d6c38286fe47db2a3fbd44b3c79c';rel='OptiScaler/hooks/Streamline_Hooks.cpp'
if len(sys.argv)>3 and sys.argv[3]=='green':
    raw=(root/rel).read_bytes();text=raw.decode('utf-8').replace('\r\n','\n')
    start=text.index('void* StreamlineHooks::hkcommon_slGetPluginFunction(')
    branch=text.index('    if (strcmp(functionName, "slOnPluginLoad") == 0)',start)
    opening=text.index('{',branch);end=opening+1;depth=1
    while depth:
        if text[end]=='{':depth+=1
        if text[end]=='}':depth-=1
        end+=1
    body=text[branch:end]
    assert 'Bindings::Bind(original,&hkcommon_slOnPluginLoad)' in body
    assert 'o_common_slOnPluginLoad' not in body
    load=text.index('bool StreamlineHooks::hkcommon_slOnPluginLoad(')
    loadBody=text[load:text.index('\n}',load)]
    call='    auto result = invocation.original(invocation.parameters, loaderJSON, pluginJSON);'
    guard='    if(!result||!pluginJSON||!*pluginJSON)return result;'
    assert call in loadBody and guard in loadBody
    code='''struct OriginProjection
{
    using PFN_slOnPluginLoad=FixtureLoad;
    static inline FixtureGetter o_common_slGetPluginFunction=nullptr;
    static bool hkcommon_slOnPluginLoad(const Invocation& invocation,const char* loaderJSON,const char** pluginJSON)
    {
        CaptureInvocation capture(invocation);
'''+call+'\n'+guard+'''
        return Neurotic::Hooks::StreamlineOwnedJson::PublishSuccessfulForProcess(result,pluginJSON,*pluginJSON);
    }
    static void* hkcommon_slGetPluginFunction(const char* functionName)
    {
        if(Neurotic::Runtime::IsProcessExiting())return nullptr;
'''+body.replace('sl::param::IParameters','void')+'''
        return o_common_slGetPluginFunction(functionName);
    }
};
'''
    p=out/'green_common_callback_origin.inc';p.write_text(code,encoding='utf-8')
    print(json.dumps(dict(path=rel,source_sha256=hashlib.sha256(raw).hexdigest(),generated_sha256=hashlib.sha256(p.read_bytes()).hexdigest(),adaptation='Actual live Common getter OnLoad binding branch verbatim except opaque Parameters template argument becomes authored void. Actual bound original-call and false/null guard copied. Transform replaces product JSON/version policy with exact raw-text process ownership (actual existing owner helper), testing origin/lifetime only; actual full JSON body separately source-reviewed/TUcompiled. Diagnostic LOG_WARN is a fixture printer, not provider execution. Physical global getter origin remains unresolved.')))
    sys.exit(0)
raw=subprocess.check_output(['git','--no-optional-locks','show',base+':'+rel],cwd=root);text=raw.decode('utf-8')
start=text.index('void* StreamlineHooks::hkcommon_slGetPluginFunction(')
branch=text.index('    if (strcmp(functionName, "slOnPluginLoad") == 0)',start)
end=text.index('\n    }',branch)+len('\n    }');body=text[branch:end]
assert body.count('o_common_slOnPluginLoad = (PFN_slOnPluginLoad) o_common_slGetPluginFunction(functionName);')==1
assert body.count('return &hkcommon_slOnPluginLoad;')==1
load=text.index('bool StreamlineHooks::hkcommon_slOnPluginLoad(')
call='    auto result = o_common_slOnPluginLoad(params, loaderJSON, pluginJSON);'
assert text[load:text.index('\n}',load)].count(call)==1
code='''struct OriginProjection
{
    using PFN_slOnPluginLoad=FixtureLoad;
    static inline FixtureGetter o_common_slGetPluginFunction=nullptr;
    static inline FixtureLoad o_common_slOnPluginLoad=nullptr;
    static bool hkcommon_slOnPluginLoad(void* params,const char* loaderJSON,const char** pluginJSON)
    {
'''+call+'''
        return result;
    }
    static void* hkcommon_slGetPluginFunction(const char* functionName)
    {
'''+body+'''
        return o_common_slGetPluginFunction(functionName);
    }
};
'''
p=out/'old_common_callback_origin.inc';p.write_text(code,encoding='utf-8')
print(json.dumps(dict(base=base,path=rel,source_sha256=hashlib.sha256(raw).hexdigest(),generated_sha256=hashlib.sha256(p.read_bytes()).hexdigest(),adaptation='Actual Common OnLoad getter branch and original-call expression verbatim, in authored static fixture class. PFN alias matches FixtureLoad bool(void*,const char*,const char**). No parameter/caps/JSON/version transformation or physical hook installation; sole global original-target binding projection. Other getter branches excluded; normal fallback calls actual authored getter.')))
