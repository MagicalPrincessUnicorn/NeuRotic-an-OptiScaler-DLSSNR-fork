from pathlib import Path
import hashlib,json,subprocess,sys
root=Path(sys.argv[1]).resolve();out=Path(sys.argv[2]).resolve();out.mkdir(parents=True,exist_ok=True)
base='24e33b34755f3d93d9e958553f3c335d1ebc4745';rel='OptiScaler/hooks/Streamline_Hooks.cpp'
green=len(sys.argv)>3 and sys.argv[3]=='green'
raw=(root/rel).read_bytes() if green else subprocess.check_output(['git','--no-optional-locks','show',base+':'+rel],cwd=root)
text=raw.decode('utf-8').replace('\r\n','\n')
def function(marker):
    start=text.index(marker);opening=text.index('{',start)
    return text[start:text.index('\n}',opening)+len('\n}')]
predicate=function('static bool IsSL1AndFGActive()')
patch=function('static void PatchSL1PluginJson(nlohmann::json& configJson)')
explicit_patch=function('static void PatchSL1PluginJson(nlohmann::json& configJson, bool legacyExternalFg)') if green else ''
legacy=function('const char* StreamlineHooks::hkdlssg_slGetPluginJSONConfig_sl1()')
body=legacy[legacy.index('{'):]
if green:
    call='Neurotic::Hooks::StreamlineOwnedJson::TryCopyForProcess(patchedConfig)'
    assert body.count(call)==1 and 'static std::string patchedConfig;' not in body
else:
    assert 'static std::string patchedConfig;' in body and 'return patchedConfig.c_str();' in body
assert body.endswith('        return originalConfig;\n    }\n}')
current=(root/rel).read_bytes().decode('utf-8').replace('\r\n','\n')
assert predicate in current and patch in current and body in current
if green:assert explicit_patch in current
if green:body=body.replace(call,'FixtureTryCopy(patchedConfig)')
code=predicate+'\n\n'+(explicit_patch+'\n\n' if green else '')+patch+'\n\nstatic const char* LegacyGetter()\n'+body+'\n'
p=out/('green_legacy_json_return.inc' if green else 'old_legacy_json_return.inc');p.write_text(code,encoding='utf-8')
print(json.dumps(dict(base=None if green else base,path=rel,source_sha256=hashlib.sha256(raw).hexdigest(),generated_sha256=hashlib.sha256(p.read_bytes()).hexdigest(),adaptation='Actual entire legacy JSON getter body and actual predicate/patch copied, wrapper name removed from class scope. GREEN copies BOTH complete actual helper overloads; historical RED retains its original helper. GREEN only TryCopy expression selects actual default process-heap helper or a failure-only test allocation policy. Authored const-char originals/minimalState, actual nlohmann; diagnostic stub consumes arguments and can inject logging failure. Complete terminal catch/original return/real brace asserted/currentbodyidentity. No provider code retirement/global-origin/fullOTA proof.')))
