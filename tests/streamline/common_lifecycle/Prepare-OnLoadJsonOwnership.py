from pathlib import Path
import hashlib,json,subprocess,sys
root=Path(sys.argv[1]).resolve();out=Path(sys.argv[2]).resolve();out.mkdir(parents=True,exist_ok=True)
base='df4feef530dd6cddab8c4acb5a92b7f74df8c12d';rel='OptiScaler/hooks/Streamline_Hooks.cpp'
raw=subprocess.check_output(['git','--no-optional-locks','show',base+':'+rel],cwd=root)
text=raw.decode('utf-8');start=text.index('bool StreamlineHooks::hkcommon_slOnPluginLoad(')
end=text.index('\nstatic void NrTraceNativeMfgReceipt',start);body=text[start:end]
declaration='static std::string config;'
tail='config = configJson.dump();\n\n    *pluginJSON = config.c_str();\n\n    return result;'
assert body.count(declaration)==1 and body.count(tail)==1
code='static bool ReturnJson(bool result,const char** pluginJSON,std::string rendered)\n{\n    '+declaration+'\n    '+tail.replace('configJson.dump()','std::move(rendered)')+'\n}\n'
p=out/'old_onload_json_return.inc';p.write_text(code,encoding='utf-8')
print(json.dumps(dict(base=base,path=rel,source_sha256=hashlib.sha256(raw).hexdigest(),generated_sha256=hashlib.sha256(p.read_bytes()).hexdigest(),adaptation='Actual Common static storage declaration and final assignment/publication/return copied verbatim. JSON dump rvalue replaced by supplied string rvalue. Original callback/JSON parsing/version changes outside this ownership-only projection.')))
