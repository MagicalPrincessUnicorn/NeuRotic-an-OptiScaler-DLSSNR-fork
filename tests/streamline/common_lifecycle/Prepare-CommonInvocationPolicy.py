from pathlib import Path
import hashlib,json,sys
root=Path(sys.argv[1]).resolve();out=Path(sys.argv[2]).resolve();out.mkdir(parents=True,exist_ok=True)
rel='OptiScaler/hooks/Streamline_Hooks.cpp';raw=(root/rel).read_bytes();text=raw.decode('utf-8').replace('\r\n','\n')
def function(marker):
    start=text.index(marker);opening=text.index('{',start)
    return text[start:text.index('\n}',opening)+2]
predicate=function('static bool IsSL1AndFGActive()')
explicit=function('static void PatchSL1PluginJson(nlohmann::json& configJson, bool legacyExternalFg)')
compat=function('static void PatchSL1PluginJson(nlohmann::json& configJson)')
common=function('bool StreamlineHooks::hkcommon_slOnPluginLoad(')
body=common[common.index('{'):]
assert 'invocation.original(invocation.parameters, loaderJSON, pluginJSON)' in body
assert 'PatchSL1PluginJson(configJson, commonRelease.major == 1 && State::Instance().activeFgInput == FGInput::DLSSG)' in body
assert body.endswith('    return NrOwnedOnLoadJson(result,pluginJSON,config,"Common");\n}')
code='\n\n'.join([predicate,explicit,compat,'static bool CommonTransform(const Invocation& invocation,const char* loaderJSON,const char** pluginJSON)\n'+body])+'\n'
p=out/'common_invocation_policy.inc';p.write_text(code,encoding='utf-8')
print(json.dumps(dict(path=rel,source_sha256=hashlib.sha256(raw).hexdigest(),generated_sha256=hashlib.sha256(p.read_bytes()).hexdigest(),adaptation='Entire actual Common transform body and both actual JSON helper overloads/predicate. Qualified member signature becomes authored free fixture function with void parameters. Minimal authored State/feature_version/logs; actual nlohmann JSON and process-owned text. Identity/lifetime verified separately with production binding header and real authored fixture modules. Compatibility global write deliberately retained; this does NOT turn version-overwrite RED8ff7 green.')))
